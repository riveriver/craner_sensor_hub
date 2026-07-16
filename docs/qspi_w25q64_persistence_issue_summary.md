# W25Q64 QSPI 持久化写入失败问题总结

## 1. 问题现象

项目将设备参数和 Modbus 持久化寄存器放在外部 Flash W25Q64 中：

1. 设备参数通过 `settings/NVS` 保存到 `param-store` 分区。
2. Modbus 持久化寄存器通过项目自管双 bank 存储保存到 `modbus-store` 分区。

现场表现为：

1. `param_set device/project tm101` 后，`param_get device/project` 可以在 RAM 中读到 `tm101`。
2. 执行 `param_save` 失败，重启后参数恢复为默认值 `project`。
3. 执行 `modbus_store_clear` 成功。
4. 执行 `modbus_store_save` 失败。

说明外部 Flash 设备可以打开，分区也存在，擦除操作也能成功，但写入后无法正确读回。

## 2. 定位过程

最初 `modbus_store_save` 只返回 `-22`，无法判断失败发生在构造 payload、擦除、写入还是读回校验阶段。

为定位问题，给 Modbus 持久化服务增加了诊断字段：

1. `bank_size`：当前分区双 bank 后单个 bank 的大小。
2. `last_payload_size`：最近一次保存生成的 payload 大小。
3. `last_stage`：最近一次失败发生的阶段。
4. `last_error`：最近一次失败错误码。

增加诊断后，现场输出为：

```text
modbus_store_save
save modbus store failed: -71

modbus_store_status
initialized=yes
dirty=no
active_bank_valid=no
active_bank=0
active_sequence=0
bank_size=65536
payload_size=0
last_payload_size=8
load_count=0
save_count=0
clear_count=1
fail_count=1
last_stage=11
last_error=-71
```

其中：

1. `last_stage=11` 表示保存流程已经完成擦除、写 payload、写 header，失败发生在最后的读回校验阶段。
2. `last_error=-71` 表示读回的 header 不符合预期格式，也就是刚写入的数据没有按预期读回来。

因此问题不是 Modbus 持久化数据结构错误，也不是分区不存在，而是外部 W25Q64 的 QSPI 写入命令配置不匹配。

## 3. 根本原因

板卡 DTS 中 W25Q64 原配置为：

```dts
w25q64: qspi-nor-flash@0 {
	compatible = "st,stm32-qspi-nor";
	reg = <0>;
	size = <DT_SIZE_M(64)>;
	qspi-max-frequency = <40000000>;
	spi-bus-width = <4>;
	status = "okay";
};
```

`spi-bus-width = <4>` 启用四线 QSPI 模式。

Zephyr 的 `st,stm32-qspi-nor` 驱动在四线模式下，如果没有显式配置 `writeoc`，默认使用 `PP_1_4_4` 写命令。该模式表示：

1. opcode 使用 1 线。
2. address 使用 4 线。
3. data 使用 4 线。

当前 W25Q64JVSSIQ 更适合使用 `PP_1_1_4` Quad Page Program 模式：

1. opcode 使用 1 线。
2. address 使用 1 线。
3. data 使用 4 线。

由于写命令模式不匹配，驱动层 `flash_area_write()` 返回成功，但实际写入内容无法按预期读回，最终表现为设备参数和 Modbus 持久化都保存失败。

## 4. 解决方案

在板卡 DTS 的 W25Q64 节点中显式指定写命令：

```dts
w25q64: qspi-nor-flash@0 {
	compatible = "st,stm32-qspi-nor";
	reg = <0>;
	size = <DT_SIZE_M(64)>;
	qspi-max-frequency = <40000000>;
	spi-bus-width = <4>;
	writeoc = "PP_1_1_4";
	status = "okay";
};
```

对应修改文件：

```text
boards/craner/craner_general_stm32h743vit6/craner_general_stm32h743vit6.dts
```

## 5. 验证结果

修复后现场验证：

```text
modbus_store_clear
status=ok

modbus_store_save
status=ok

modbus_store_status
initialized=yes
dirty=no
active_bank_valid=yes
active_bank=0
active_sequence=1
bank_size=65536
payload_size=8
last_payload_size=8
load_count=0
save_count=1
clear_count=1
fail_count=0
last_stage=0
last_error=0
```

设备参数保存也恢复正常：

```text
param_set device/project tm101
status=ok
device/project=tm101
dirty=yes

param_save
status=ok

reboot

param_get device/project
device/project=tm101
```

验证结论：

1. 外部 W25Q64 的擦除、写入、读回链路恢复正常。
2. Modbus 持久化寄存器可以成功保存。
3. 设备参数可以成功保存，并且重启后仍能恢复。

## 6. 经验总结

1. 外部 Flash `device_ready=yes` 只能说明设备节点初始化成功，不能证明擦写读链路完全正确。
2. `flash_area_erase()` 成功也不能证明 `flash_area_write()` 的命令模式正确，因为擦除和写入使用的 NOR 命令不同。
3. 对持久化服务必须做写后读回校验，不能只相信 `flash_area_write()` 的返回值。
4. QSPI NOR 使用四线模式时，必须确认芯片支持的 read/program opcode 与 Zephyr DTS 配置一致。
5. W25Q64JVSSIQ 在本项目中应显式配置 `writeoc = "PP_1_1_4"`。
6. 对 settings/NVS、Modbus store 这类共享外部 Flash 的功能，如果两个模块同时出现保存失败，应优先怀疑底层 Flash 配置或硬件链路。

## 7. 后续建议

1. 保留 `modbus_store_status` 中的 `last_stage` 和 `last_error`，方便现场快速定位持久化问题。
2. 后续如调整 QSPI 频率、读命令、Quad Enable 或更换 Flash 型号，需要重新执行设备参数保存和 Modbus 持久化保存测试。
3. 建议将外部 Flash 基础读写测试纳入产测或研发回归测试，至少覆盖擦除、写入、读回、重启后恢复。
