# 内部 Flash CoreDump 设计方案

## 1. 设计目标

本方案用于在 `craner_general_stm32h743vit6` 板卡上实现 Zephyr CoreDump 持久化保存。CoreDump 必须保存在 STM32H743 内部 Flash，不依赖外部 W25Q64、网络、MQTT、文件系统或复杂业务线程。原因是系统发生 HardFault、MPU Fault、Stack Overflow、内存破坏或驱动异常时，外部 Flash 驱动、QSPI 总线、业务存储服务和网络服务都不一定仍然可靠；CoreDump 的职责是尽量保住第一故障现场，因此存储链路越短越好。

本方案先作为设计文档供审核，暂不直接修改代码。

## 2. 设计原则

1. CoreDump 使用内部 Flash 独立分区，不与设备参数、Modbus 寄存器、MQTT 离线缓存、业务日志混用。

2. CoreDump 保存路径只依赖 Zephyr fatal error 流程、内部 Flash driver、flash map 和 stream flash，避免依赖外部 Flash、文件系统、动态内存、网络和普通工作队列。

3. CoreDump 分区只保存最近一次崩溃现场。下一次崩溃可以覆盖上一次，但正常启动后不得自动擦除，必须由测试、维护或远程诊断明确确认后再清除。

4. CoreDump 内容优先保证“能定位问题”，不追求完整 dump 全部 RAM。v1 以寄存器、异常栈、当前线程、关键线程元数据、有限栈顶内容为主，避免 CoreDump 过大导致写入时间长、分区不够或再次异常。

5. CoreDump 读取和上报发生在下一次正常启动后。崩溃当下只做保存，不做 MQTT 上报，不做外部 Flash 复制，不做复杂日志打印。

## 3. 当前内部 Flash 布局

当前 DTS 中 `flash0` 分区如下：

```dts
boot_partition: partition@0 {
	label = "mcuboot";
	reg = <0x00000000 0x00020000>;
};

slot0_partition: partition@20000 {
	label = "image-0";
	reg = <0x00020000 0x000c0000>;
};

slot1_partition: partition@e0000 {
	label = "image-1";
	reg = <0x000e0000 0x000c0000>;
};

scratch_partition: partition@1a0000 {
	label = "image-scratch";
	reg = <0x001a0000 0x00020000>;
};

app_storage_partition: partition@1c0000 {
	label = "app-storage";
	reg = <0x001c0000 0x00040000>;
};
```

内部 Flash 总空间为 2MB。当前 `app-storage` 占用最后 256KB，地址范围为 `0x001c0000` 到 `0x00200000`。建议从该区域拆出独立 CoreDump 分区。

## 4. 推荐分区方案

v1 推荐方案：

```dts
coredump_partition: partition@1c0000 {
	label = "coredump-partition";
	reg = <0x001c0000 0x00020000>;
};

app_storage_partition: partition@1e0000 {
	label = "app-storage";
	reg = <0x001e0000 0x00020000>;
};
```

含义如下：

1. `coredump_partition`：128KB，内部 Flash，专用于 Zephyr CoreDump。

2. `app_storage_partition`：128KB，内部 Flash，继续用于设备关键参数、settings/NVS、RTC trust record 之外的少量持久化配置。

3. 外部 W25Q64：后续用于 Modbus 持久化寄存器、大容量参数备份、诊断历史、离线缓存等，不用于 CoreDump 主保存路径。

Zephyr 的 flash partition CoreDump backend 要求存在 node label `coredump_partition`，并且分区 label 为 `"coredump-partition"`。Zephyr 4.4.1 的 `coredump_backend_flash_partition.c` 里也明确说明当前 flash partition backend 不支持 external memories，因此内部 Flash 是更匹配官方实现的选择。

## 5. CoreDump 内容策略

不建议启用完整 RAM dump。完整 RAM dump 会尝试保存 linker RAM 区域，当前项目 RAM 使用、网络栈、MQTT、Shell、Modbus 多线程栈加起来后，128KB 分区很容易不够。CoreDump 分区写满后可能只得到不完整现场，反而影响分析。

v1 建议启用线程级或最小级 CoreDump：

1. 首选 `CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_THREADS=y`，保存线程结构、线程栈和线程调试元数据，适合分析 stack overflow、死线程、错误线程上下文。

2. 如果实际生成数据超过 128KB，则降级为 `CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_MIN=y`，只保存当前异常线程和最小必要信息。

3. 启用栈顶限制，避免把所有线程完整栈都写入 Flash。建议当前线程栈顶保存 2048 字节，其他线程栈顶保存 1024 字节。后续可根据真实 CoreDump 大小调整。

建议配置草案：

```conf
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION=y
CONFIG_DEBUG_COREDUMP_SHELL=y
CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_THREADS=y
CONFIG_DEBUG_COREDUMP_THREAD_STACK_TOP=y
CONFIG_DEBUG_COREDUMP_THREAD_STACK_TOP_LIMIT_FOR_CURRENT=2048
CONFIG_DEBUG_COREDUMP_THREAD_STACK_TOP_LIMIT=1024
CONFIG_DEBUG_COREDUMP_FLASH_CHUNK_SIZE=64
```

如果构建或实际测试发现 `MEMORY_DUMP_THREADS` 在当前架构或配置下不可用，则使用：

```conf
CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_MIN=y
```

## 6. 崩溃时写入流程

系统发生 fatal error 后，Zephyr 进入 CoreDump 流程：

1. Zephyr fatal error 捕获异常原因、寄存器和异常栈。

2. CoreDump core 生成标准 coredump header、架构信息、线程信息和内存片段。

3. flash partition backend 打开 `coredump_partition`。

4. backend 先写入 header，再按 chunk 流式写入 CoreDump 数据。

5. 写入结束后保存大小、校验和和错误码。

6. 系统 halt 或 reboot，具体行为由 fatal error 策略决定。

崩溃当下不做以下动作：

1. 不连接 MQTT。

2. 不写外部 W25Q64。

3. 不访问 Modbus 寄存器持久化服务。

4. 不启动复杂工作队列。

5. 不进行大量日志输出。

## 7. 启动后处理流程

设备下一次正常启动后执行 CoreDump 检查：

1. 启动早期初始化 Shell、日志和基础存储。

2. 查询 `COREDUMP_QUERY_HAS_STORED_DUMP`，判断内部 Flash 是否存在 CoreDump。

3. 如果存在，记录高优先级日志，例如 `Stored coredump found in internal flash`。

4. 查询 `COREDUMP_QUERY_GET_STORED_DUMP_SIZE`，输出 CoreDump 大小。

5. 执行 `COREDUMP_CMD_VERIFY_STORED_DUMP` 校验有效性。

6. 在 Shell 中允许本地导出；在 MQTT shell 白名单中只允许状态查询，不建议直接远程打印完整 CoreDump，避免消息过大和泄露敏感信息。

7. 维护人员确认已导出后，执行擦除命令清除 CoreDump。

## 8. Shell 和远程诊断命令

Zephyr 自带 `CONFIG_DEBUG_COREDUMP_SHELL=y` 后会注册 `coredump` 命令。建议开放以下本地串口 Shell 操作：

```text
coredump has_stored_dump
coredump verify_stored_dump
coredump print_stored_dump
coredump erase_stored_dump
coredump error get
coredump error clear
```

项目业务 Shell 可再包一层更友好的命令：

```text
coredump_status
coredump_export
coredump_clear
```

建议 MQTT 远程诊断 v1 只开放：

```text
coredump_status
```

不建议 v1 通过 MQTT 直接导出完整 CoreDump。CoreDump 可能包含栈内容、设备参数、网络状态、MQTT 用户名片段、业务数据或未初始化内存，远程导出需要额外权限控制、分片、加密和审计。

## 9. 与设备参数和 Modbus 持久化的关系

内部 Flash 的职责分配如下：

1. `coredump_partition`：保存最近一次崩溃现场，优先级最高，不能被 settings/NVS 使用。

2. `app_storage_partition`：保存设备参数类数据，例如设备配置、网络策略、时间同步模式、校准参数索引等。

3. RTC trust record：当前使用 STM32 backup register，不占用内部 Flash。

4. Modbus 持久化寄存器：建议后续放外部 W25Q64，运行态仍使用 RAM 镜像，Flash 只做延迟批量保存。

CoreDump 分区不参与 wear leveling，不作为普通日志区，不保存业务历史数据。它是故障现场保险箱。

## 10. 容量评估

128KB CoreDump 分区可以覆盖以下 v1 场景：

1. Cortex-M 异常寄存器和 Zephyr coredump header。

2. 当前异常线程的关键栈内容。

3. 多个线程的有限栈顶内容。

4. 线程元数据，用于判断哪个线程崩溃、栈是否溢出、线程状态是否异常。

128KB 不适合以下场景：

1. 完整 dump 所有 RAM。

2. 保存所有网络 buffer。

3. 保存完整 MQTT/Modbus 应用缓存。

4. 多次崩溃历史记录。

如果后续要求完整 RAM dump 或保留多次崩溃记录，需要重新设计内部 Flash 空间，可能要缩小 OTA slot、取消 scratch 方案、迁移部分存储到外部 Flash，或增加专用内部 Flash 保留区。

## 11. 安全和隐私

CoreDump 可能包含敏感数据：

1. MQTT 用户名、密码或 token 的内存残留。

2. 设备 UID、MAC、hostname。

3. Modbus 寄存器中的业务参数。

4. 栈上的临时命令、网络报文、诊断输出。

因此处理策略如下：

1. 本地串口可导出完整 CoreDump，但应在受控环境下使用。

2. MQTT 默认只上报是否存在 CoreDump、大小、校验结果和错误码。

3. 如需远程导出，必须设计分片协议、访问控制、传输加密和操作审计。

4. 维护人员确认导出后应擦除 CoreDump，避免长期保留敏感现场。

## 12. 测试计划

1. 构建检查：确认 `.config` 中启用了 `CONFIG_DEBUG_COREDUMP=y`、`CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION=y`、`CONFIG_DEBUG_COREDUMP_SHELL=y`。

2. DTS 检查：确认生成的 `zephyr.dts` 中存在 `coredump_partition`，地址为内部 Flash，label 为 `"coredump-partition"`。

3. 正常启动检查：执行 `coredump has_stored_dump`，首次应显示没有已保存 CoreDump。

4. 人工触发 fault：增加临时测试命令，例如空指针写入或 `k_oops()`，确认系统进入 fatal error 并写入 CoreDump。

5. 重启后检查：执行 `coredump has_stored_dump`，应显示找到 CoreDump。

6. 校验检查：执行 `coredump verify_stored_dump`，应显示校验通过。

7. 导出检查：执行 `coredump print_stored_dump`，确认能输出 `#CD:BEGIN#` 到 `#CD:END#` 格式数据。

8. 擦除检查：执行 `coredump erase_stored_dump` 后再次查询，应显示没有 CoreDump。

9. 断电场景：触发 fault 写入后立即断电，重复多次，确认不会影响 MCUboot、slot0、slot1 和 app-storage。

10. 容量边界：增加线程数和栈使用压力，观察 CoreDump 是否完整。如果出现写满或校验失败，降低 dump 内容或增大分区。

## 13. 实施步骤

建议按以下顺序实施：

1. 修改 board DTS，从 `app-storage` 拆出 `coredump_partition`。

2. 修改 `prj.conf`，启用 Zephyr CoreDump flash partition backend 和 shell。

3. 完整构建，检查 `.config` 和 `zephyr.dts`。

4. 增加一个仅测试构建启用的 fault 注入 Shell 命令，避免正式版本误触发。

5. 在 `shell_app` 或独立 `coredump_service` 中增加简化状态命令。

6. 在 MQTT shell 白名单中只加入 `coredump_status`。

7. 完成 fault 注入、重启校验、导出和擦除测试。

## 14. 待审核问题

1. CoreDump 分区 v1 是否确定为 128KB？

2. `app-storage` 从 256KB 缩小到 128KB 是否满足设备参数类需求？

3. v1 是否接受只保存最近一次 CoreDump？

4. v1 是否默认禁止 MQTT 远程导出完整 CoreDump？

5. fault 注入命令是否只允许 debug/test 构建启用？

