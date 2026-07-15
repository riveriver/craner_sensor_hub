# 持久化存储服务设计方案

## 1. 设计目标

本方案用于规划 `craner_encoder_hub` 项目的持久化存储服务。项目同时具备 STM32H743 内部 Flash 和外部 W25Q64JVSSIQ QSPI Flash。内部 Flash 当前承担 MCUboot、主固件、升级固件、scratch 和少量应用存储；外部 W25Q64 容量为 64Mbit，也就是 8MB，适合承载更大容量、更频繁更新的业务数据。

本方案覆盖设备参数、Modbus 持久化线圈和 Holding Register、CoreDump、诊断摘要、外部 Flash 分区规划，以及后续实现路径。

## 2. 已确定的方案选择

1. 内部 Flash 采用 `128KB CoreDump + 128KB app-storage` 的拆分方案。

2. 设备参数使用 Zephyr `settings + NVS`，保存在内部 Flash 的 `app-storage`。

3. Modbus 持久化数据全部放外部 W25Q64，v1 保存线圈和 Holding Register。

4. W25Q64 v1 只启用 `modbus-store` 一个分区，暂不启用参数备份区。

5. MQTT 远程维护 v1 允许查询状态，也允许执行格式化、清除、恢复出厂和完整 CoreDump 导出等维护动作。此类高风险动作需要明确白名单、操作日志和权限控制，不能做成任意 shell 透传。

## 3. settings/NVS 和 ZMS 的关系

Zephyr `settings` 不是具体的 Flash 存储算法，而是一套 key-value 配置接口。应用通过 `settings` 保存和加载配置项，底层可以选择不同后端。

`NVS` 和 `ZMS` 是两种不同的 Flash 存储后端：

```text
settings + NVS：使用 settings API，底层由 NVS 写 Flash
settings + ZMS：使用 settings API，底层由 ZMS 写 Flash
```

本项目 v1 选择 `settings + NVS`。原因是设备参数数量少、写入频率低、key-value 模型清晰，NVS 在 Zephyr 项目中使用成熟，足够满足设备参数保存需求。ZMS 是 Zephyr 较新的 settings 后端，目标也是替代或改进部分传统 NVS/FCB 场景，但对本项目 v1 来说，设备参数规模很小，选择 NVS 的风险更低、验证路径更短、资料和示例更多。ZMS 可以作为后续评估项，但 v1 不混用 NVS 和 ZMS，避免存储路径复杂化。

## 4. 总体原则

1. 内部 Flash 保存启动必须依赖、丢失影响大、写入频率低、故障时仍必须可靠的数据。

2. 外部 W25Q64 保存容量较大、写入较频繁、可恢复或可重建的业务数据。

3. CoreDump 必须独立保存在内部 Flash，不依赖外部 Flash、网络、MQTT、文件系统或普通业务线程。

4. 所有业务读写先操作 RAM 镜像，Flash 只作为持久化副本。不要在 Modbus 写线圈或寄存器的同步路径里直接擦写 Flash。

5. 所有持久化数据必须带版本号、长度、CRC 和默认值策略，方便未来固件升级后做数据迁移。

6. 存储服务要提供统一状态查询、错误码、格式化、擦除和恢复出厂能力，避免各业务模块直接操作 Flash。

## 5. 配置来源和保存位置

设备配置会来自三个入口：Kconfig、Shell 和 Modbus。它们的保存位置不同。

1. Kconfig 是编译期默认值，保存在固件镜像里。固件镜像位于内部 Flash 的 `slot0_partition`，升级镜像位于 `slot1_partition`。例如 MQTT broker、NTP server、默认 DHCP 模式、默认设备类型等，都可以先由 Kconfig 给出出厂默认值。Kconfig 值不能在运行时真正修改，修改 Kconfig 需要重新编译和烧录固件。

2. Shell 设置的是运行时配置。如果该配置需要断电保持，应保存到内部 Flash 的 `app-storage` 分区，后端采用 Zephyr `settings + NVS`。例如 `mqtt/host`、`mqtt/port`、`time/sync_mode`、`network/dhcp_mode`、`device/hostname`、编码器校准参数等，都属于设备参数类。

3. Modbus 写入的是业务数据。v1 只持久化线圈和 Holding Register 中带 `persistent` 属性的数据，保存到外部 W25Q64 的 `modbus-store` 分区。实时状态、输入寄存器、错误计数、编码器实时值不保存。

推荐模型：

```text
Kconfig 默认值 -> 启动时生成默认配置
app-storage settings/NVS -> 覆盖 Kconfig 默认值
外部 modbus-store -> 覆盖 Modbus persistent 线圈和 Holding Register 默认值
RAM 镜像 -> 运行时所有业务实际读取的当前值
```

启动加载顺序：

```text
1. 加载 Kconfig 默认值
2. 从内部 app-storage 读取 Shell/参数服务保存的设备配置
3. 有保存值则覆盖默认值，没有保存值则继续使用 Kconfig 默认值
4. 从外部 W25Q64 读取 Modbus persistent 快照
5. 快照有效则覆盖 persistent 线圈和 Holding Register，无效则使用默认 Modbus 表
```

## 6. 内部 Flash 分区方案

当前内部 Flash 布局：

```text
0x00000000 ~ 0x0001ffff   mcuboot          128KB
0x00020000 ~ 0x000dffff   image-0          768KB
0x000e0000 ~ 0x0019ffff   image-1          768KB
0x001a0000 ~ 0x001bffff   image-scratch    128KB
0x001c0000 ~ 0x001fffff   app-storage      256KB
```

确定拆分为：

```text
0x001c0000 ~ 0x001dffff   coredump-partition   128KB
0x001e0000 ~ 0x001fffff   app-storage          128KB
```

DTS 草案：

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

`coredump_partition` 只给 Zephyr CoreDump 使用，不挂载 settings，不保存业务数据。`app_storage_partition` 用于设备关键参数、存储服务元数据、上次重启原因等小数据。

## 7. 外部 W25Q64 分区方案

W25Q64 容量为 8MB。v1 只启用 `modbus-store`：

```text
0x000000 ~ 0x1fffff   modbus-store   2MB
0x200000 ~ 0x7fffff   reserved       6MB
```

`modbus-store` 预留 2MB，是为了后续线圈和 Holding Register 数量持续扩展时不频繁调整分区。`reserved` 暂不格式化，后续可用于参数备份、诊断历史、MQTT 离线队列或新的业务数据区。

## 8. W25Q64 硬件接入方案

外部 Flash 型号为 `W25Q64JVSSIQ`，容量 64Mbit，接口为 STM32H743 QSPI。

引脚如下：

```text
PB10  QSPI_NCS
PB2   QSPI_CLK / BOOT1_CLK
PD11  QSPI_IO0 / DI
PD12  QSPI_IO1 / DO
PE2   QSPI_IO2 / WP#
PD13  QSPI_IO3 / HOLD# or RESET#
```

注意事项：

1. `PB2` 与 BOOT1 相关，硬件上必须确认启动时电平不会导致 MCU 进入错误启动模式。

2. `WP#` 和 `HOLD#/RESET#` 作为 QSPI IO2/IO3 使用时，需要确认外部上拉和 Flash 工作模式。

3. DTS 中应使用 STM32H743 的 `quadspi` 控制器和 `jedec,spi-nor` 兼容节点。

4. 首版频率建议保守，例如 24MHz 或更低，验证稳定后再提高。

5. 上电后应先读取 JEDEC ID，确认 W25Q64 存在且容量匹配，再允许业务存储服务使用外部 Flash。

## 9. 软件模块组织

建议新增以下模块：

```text
src/storage_service.c
src/storage_service.h
src/device_param_store.c
src/device_param_store.h
src/modbus_register_store.c
src/modbus_register_store.h
src/coredump_service.c
src/coredump_service.h
```

职责划分：

1. `storage_service`：统一初始化内部 Flash、外部 Flash、settings/NVS、外部分区状态；提供健康状态、格式化、错误码和互斥保护。

2. `device_param_store`：负责设备参数 schema、默认值、加载、保存、迁移、恢复出厂。

3. `modbus_register_store`：负责 Modbus 持久化线圈和 Holding Register 的 RAM 镜像同步、脏标记、延迟保存、批量保存、CRC 校验和版本迁移。

4. `coredump_service`：负责启动后检查 CoreDump 是否存在、校验、状态输出、擦除和 Shell/MQTT 诊断封装。

5. `modbus_register_map`：继续只负责寄存器和线圈定义、读写和属性标记，不直接调用 Flash API。

6. `main.c`：只调用服务初始化函数，不放具体存储逻辑。

## 10. 初始化顺序

推荐启动顺序：

1. `device_identity_service_init()` 生成设备身份。

2. `storage_service_init_internal()` 初始化内部 Flash 和 app-storage。

3. `coredump_service_check_on_boot()` 检查内部 Flash 是否存在 CoreDump。

4. `device_param_store_load()` 先填充 Kconfig 默认值，再从内部 app-storage 加载 Shell 保存的设备参数覆盖默认值。

5. `storage_service_init_external()` 初始化 QSPI 和 W25Q64，读取 JEDEC ID，检查外部分区版本。

6. `modbus_register_store_load()` 从外部 Flash 加载 Modbus 持久化线圈和 Holding Register。

7. 网络、MQTT、NTP、Modbus TCP 等业务服务启动。

8. 后台存储工作线程开始处理延迟保存和诊断记录。

如果外部 Flash 初始化失败，系统仍应继续启动，但 Modbus 持久化数据使用默认值，并通过日志、Shell、MQTT 状态上报报警。

## 11. 设备参数保存策略

设备参数使用内部 app-storage 保存，采用 Zephyr `settings + NVS`。

参数示例：

```text
device/company
device/project
device/type
device/hostname
network/dhcp_mode
network/static_ip
mqtt/host
mqtt/port
mqtt/username
time/sync_mode
time/ntp_server
encoder/hoist_zero
encoder/luffing_zero
encoder/slewing_zero
modbus/tcp_max_clients
```

保存策略：

1. 每个参数都有 Kconfig 默认值或代码默认值。

2. Shell 修改后写入 settings/NVS，重启后由 settings 覆盖默认值。

3. 保存前检查长度、范围和合法性。

4. 参数 schema 带版本号，固件升级时执行迁移。

5. v1 不做外部参数备份，设备参数只保存在内部 app-storage。

6. 恢复出厂只清除设备参数和业务参数，不清除 CoreDump，除非用户明确执行 CoreDump 清除。

## 12. Modbus 线圈和 Holding Register 保存策略

Modbus 持久化范围确定为线圈和 Holding Register。输入线圈、输入寄存器、实时编码器值、在线状态、错误计数等运行态数据不保存。

Modbus 数据分为三类：

1. `volatile`：运行时状态，不保存，例如实时编码器值、在线状态、错误计数。

2. `persistent`：断电保持参数，需要保存，例如线圈配置开关、零点、倍率、限位、报警阈值。

3. `readonly`：只读数据，通常不接受外部写入，也不需要从 Flash 恢复。

写入流程：

```text
1. Modbus TCP/RTU 收到写线圈或 Holding Register 请求
2. modbus_register_map 校验地址、权限、范围
3. 写入 RAM 镜像
4. 如果对象带 persistent 属性，设置 dirty 标志
5. modbus_register_store 启动延迟保存定时器
6. 到达延迟时间后批量保存 persistent 数据快照
7. 保存完成后清除 dirty 标志
```

建议延迟保存时间：

```text
普通参数写入后延迟保存：2s ~ 5s
连续写入时合并保存：是
关机/休眠/重启前强制保存：是
保存失败重试间隔：指数退避，最大 60s
```

不建议每写一个 Modbus 线圈或寄存器就立即擦写 Flash。

## 13. Modbus 持久化数据格式

后续线圈和 Holding Register 个数会持续扩展，因此不建议使用固定 C 结构体直接保存全部数据。固定结构体在寄存器增加、删除、重排后容易造成版本兼容困难。v1 推荐使用“分段目录 + TLV 记录 + 双 bank 提交”的格式。

`modbus-store` 内部划分为两个等大的 bank：

```text
bank A：1MB
bank B：1MB
```

每次保存写入非活动 bank，写完并校验通过后，新 bank 成为活动 bank。这样可以抵抗写入中途断电。

bank header：

```c
struct modbus_store_header {
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;
	uint32_t sequence;
	uint32_t payload_size;
	uint32_t payload_crc32;
	uint32_t coil_count;
	uint32_t holding_count;
	uint32_t header_crc32;
};
```

payload 使用 TLV 记录，支持未来扩展：

```text
record_type
address
width
value_length
value
```

record_type 建议：

```text
1 = coil
2 = holding_register_u16
3 = holding_register_u32
4 = holding_register_blob
```

保存规则：

1. 只保存带 `persistent` 属性的线圈和 Holding Register。

2. 线圈按 bitset 或 TLV 都可以。若线圈地址连续且数量多，优先用 bitset 压缩；若地址稀疏，优先用 TLV。

3. Holding Register 默认以地址和值保存，不依赖编译时数组顺序。

4. 新增寄存器时，只要地址不变，旧固件保存的数据仍可被新固件按地址加载。

5. 删除寄存器时，加载流程忽略未知地址。

6. 改变寄存器含义、单位、比例系数或数据类型时，必须提升 schema version，并在 `modbus_register_store` 中做迁移。

schema version 迁移的意思是：Flash 里保存的是旧版本数据，固件升级后新代码知道旧版本数据如何转换成新版本数据。例如旧版本地址 `40010` 表示温度，单位是 0.1 摄氏度，保存值 `253` 表示 25.3 摄氏度；新版本同一地址改成 0.01 摄氏度，那么加载旧数据时不能直接把 `253` 当成 2.53 摄氏度，而要迁移成 `2530`。再比如旧版本某个 Holding Register 是 16-bit，后续改成 32-bit，也需要迁移逻辑把旧值扩展成新格式。只新增寄存器或删除寄存器通常不需要复杂迁移；改变已有地址的业务含义才需要迁移。

加载时读取 bank A 和 bank B，检查 magic、version、length、CRC，选择 sequence 最新且 CRC 正确的 bank。如果两个 bank 都无效，使用默认线圈和默认寄存器值并报警。

保存时写入当前非活动 bank，写完整 payload 和 header，校验成功后更新 sequence。

## 14. CoreDump 保存策略

CoreDump 属于特殊持久化数据，优先级高于普通业务数据。

1. CoreDump 保存在内部 Flash `coredump_partition`。

2. CoreDump 不经过 `storage_service` 的普通写队列。

3. CoreDump 不写外部 W25Q64。

4. CoreDump 不参与 settings/NVS。

5. CoreDump 默认只保留最近一次。

6. 设备正常启动后由 `coredump_service` 查询和校验，但不主动通过串口或 MQTT 上报；需要人工通过 Shell 命令查询或上报。

7. MQTT 远程诊断 v1 只允许手动上报 CoreDump 是否存在、大小、校验结果，不直接导出完整内容。

## 15. Shell 和 MQTT 诊断命令

建议新增本地 Shell 命令：

```text
storage_status
storage_format_external
storage_errors

param_get <key>
param_set <key> <value>
param_save
param_factory_reset

modbus_store_status
modbus_store_save
modbus_store_load
modbus_store_clear

coredump_status
coredump_report
coredump_export
coredump_clear
```

其中 `storage_format_external`、`param_factory_reset`、`modbus_store_clear`、`coredump_clear` 都是破坏性维护命令，允许本地串口和 MQTT 远程维护执行，但必须通过白名单服务入口，不允许直接暴露通用 shell。

MQTT shell v1 开放状态查询、维护动作、手动 CoreDump 状态上报和完整 CoreDump 导出：

```text
storage_status
storage_format_external
modbus_store_status
modbus_store_clear
coredump_status
coredump_report
coredump_export
coredump_clear
param_get
param_factory_reset
```

MQTT 远程维护命令是否实际可用取决于对应服务是否已经实现；当前已实现的 CoreDump 远程命令包括 `coredump_status`、`coredump_report`、`coredump_export` 和 `coredump_clear`。CoreDump 不在开机时主动上报，只有执行 `coredump_report` 后才发布摘要报告，执行 `coredump_export` 后才导出完整 CoreDump。默认发布主题为 `craner/test/log/emb`，可通过 `CONFIG_CRANER_COREDUMP_SERVICE_MQTT_TOPIC` 修改。完整导出使用简单文本分片格式：`#CD:BEGIN#`、多行 `#CD:<hex>`、`#CD:END#`。

## 16. Kconfig 建议

项目级开关：

```conf
CONFIG_CRANER_ENABLE_STORAGE_SERVICE=y
CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE=y
CONFIG_CRANER_ENABLE_MODBUS_REGISTER_STORE=y
CONFIG_CRANER_ENABLE_COREDUMP_SERVICE=y

CONFIG_CRANER_STORAGE_EXTERNAL_FLASH=y
CONFIG_CRANER_STORAGE_MODBUS_SAVE_DELAY_MS=3000
CONFIG_CRANER_STORAGE_SAVE_RETRY_INITIAL_MS=5000
CONFIG_CRANER_STORAGE_SAVE_RETRY_MAX_MS=60000
```

settings/NVS：

```conf
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_NVS=y
```

外部 Flash 接入后可能需要：

```conf
CONFIG_SPI=y
CONFIG_FLASH_JESD216=y
CONFIG_SPI_NOR=y
```

CoreDump：

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

实际符号需要以 Zephyr 4.4.1 的 Kconfig 和构建结果为准。

## 17. 整体实现路径

### 17.1 第一步：确定分区和最小存储服务骨架

修改 board DTS，将内部 256KB `app-storage` 拆为 `coredump_partition` 和新的 `app_storage_partition`。新增 `storage_service.c/.h`，只做内部 Flash 分区检查和状态输出，不保存业务数据。新增 `storage_status` Shell 命令，输出内部分区地址、大小、ready 状态、最近错误。

验收标准：

```text
1. west build 通过
2. zephyr.dts 中存在 coredump_partition 和 app_storage_partition
3. storage_status 能看到内部 Flash 分区信息
```

### 17.2 第二步：实现 CoreDump 内部 Flash 保存

启用 Zephyr CoreDump flash partition backend。新增 `coredump_service.c/.h`，封装 CoreDump 是否存在、大小、校验、清除。添加 `coredump_status` 和 `coredump_clear` 命令。测试构建中增加 fault 注入命令，正式构建关闭。

### 17.3 第三步：实现设备参数服务

启用 `settings + NVS`，将 settings partition 指向内部 `app_storage_partition`。新增 `device_param_store.c/.h`，定义参数表、类型、默认值、范围、settings key 和迁移版本。Shell 命令 `param_get`、`param_set`、`param_save`、`param_factory_reset` 通过参数服务访问，不直接访问 settings API。

加载规则：

```text
1. 先填入 Kconfig 默认值
2. settings 存在则覆盖默认值
3. settings 缺失则继续使用默认值
4. settings 值非法则丢弃并报警
```

### 17.4 第四步：接入外部 W25Q64

在 board DTS 中补充 QSPI pinctrl、`quadspi` 节点和 `jedec,spi-nor` 子节点。先不承载业务，只做 `storage_service_init_external()`，读取 JEDEC ID、容量、erase/write/read 能力。增加 `storage_status` 中的 external flash 状态。

### 17.5 第五步：实现外部 Flash 分区和参数备份

在 W25Q64 下定义 `modbus-store`。`storage_service` 初始化外部分区版本和 magic。v1 不启用 `param-backup`，设备参数仍只保存到内部 app-storage。

### 17.6 第六步：实现 Modbus 持久化

给 Modbus 线圈和 Holding Register 定义增加属性字段，例如 `readonly`、`volatile`、`persistent`。新增 `modbus_register_store.c/.h`，实现 dirty 标记、延迟保存、强制保存、双 bank 快照、CRC 校验和版本迁移。

保存路径：

```text
Modbus write -> RAM map -> dirty flag -> delay work -> W25Q64 modbus-store
```

验收标准：

```text
1. 写 persistent 线圈或 Holding Register 后，modbus_store_status 显示 dirty=yes
2. 延迟时间后自动保存，dirty=no
3. 重启后 persistent 数据恢复为保存值
4. 写 volatile 数据不会触发保存
5. 保存过程中断电，重启后不会加载损坏 bank
6. 新增寄存器后，旧数据仍能按地址恢复
```

### 17.7 第七步：诊断摘要和远程状态

新增诊断摘要保存：上次重启原因、CoreDump 状态、外部 Flash 最近错误、Modbus 保存失败次数、MQTT/NTP 最近错误。MQTT shell 白名单只开放状态查询命令。

## 18. 测试计划

1. 内部分区检查：构建后检查 `zephyr.dts` 中 `coredump_partition` 和 `app_storage_partition` 地址是否正确。

2. CoreDump 测试：人工触发 fault，重启后确认 `coredump_status` 能发现并校验。

3. 设备参数测试：通过 Shell 设置参数，重启和断电重启后确认参数保持。

4. 默认值覆盖测试：清空 settings 后确认系统恢复使用 Kconfig 默认值。

5. 恢复出厂测试：执行恢复出厂后，设备参数恢复默认，CoreDump 不被误删。

6. 外部 Flash JEDEC 测试：读取 ID，确认容量为 W25Q64。

7. 外部 Flash 擦写测试：对测试分区执行 erase/write/read/verify。

8. Modbus 持久化测试：写 persistent 线圈和 Holding Register，等待保存，重启后确认恢复。

9. 高频写入测试：连续写 Modbus 参数，确认只发生合并保存，不出现每次写都擦 Flash。

10. 掉电测试：保存过程中断电，重启后确认选择旧有效 bank 或新有效 bank，不加载损坏数据。

11. 外部 Flash 故障测试：断开或禁用 W25Q64，设备仍能启动，CoreDump 和关键参数不受影响。

## 19. 待审核问题

1. MQTT 远程维护允许格式化、清除、恢复出厂和完整 CoreDump 导出时，权限认证、操作审计和二次确认机制如何设计？

2. 线圈地址是否预计连续？如果连续，线圈持久化优先用 bitset；如果稀疏，优先用 TLV。

3. Holding Register 是否只保存 16-bit，还是需要支持 32-bit、float、字符串或 blob？
