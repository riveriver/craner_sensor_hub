# Zephyr 传感器中枢 RTU 采集说明

## 1. 这个示例实现了什么

当前工程把 `craner_general_stm32h743vit6` 板卡做成 4 路 Modbus RTU 传感器中枢：3 路编码器和 1 路风速仪分别通过独立 UART 轮询，从 RTU 从站读取 Holding Register，再写入本机共享的 Input Register 表。上位机或第三方系统只需要连接 Modbus TCP `502` 端口，用 FC04 读取 Input Register，就能拿到传感器数据和通信状态。

当前 RTU 采集参数在 `src/modbus_rtu_client_app.c` 中定义：

| 传感器 | UART | DTS alias | Unit ID | 远端起始地址 | 数量 | 周期 | 串口格式 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 回转编码器 | UART7 PE8/PE7 | `modbus-slewing-encoder` | `1` | `0x0002` | `2` | `50 ms` | `115200 8N1` |
| 变幅编码器 | UART8 PE1/PE0 | `modbus-luffing-encoder` | `3` | `0x0002` | `2` | `50 ms` | `115200 8N1` |
| 起升/吊钩编码器 | UART4 PD1/PD0 | `modbus-hook-encoder` | `1` | `0x0002` | `2` | `50 ms` | `115200 8N1` |
| 风速仪 | USART6 PC6/PC7 | `modbus-anemometer` | `4` | `0x0009` | `5` | `50 ms` | `115200 8N1` |

编码器的两个业务寄存器按顺序写入 `*_TRUN_CNT` 和 `*_SINAGLE_VAL`。风速仪的 5 个业务寄存器按顺序写入温度、湿度、气压、风速和风向；温度当前按 `regs[0] - 4000` 转成本机寄存器值。

## 2. 怎么使用

编译：

```powershell
.\build.ps1
```

烧录：

```powershell
.\flash.ps1
```

串口 Shell 使用 UART5，默认 `115200 8N1`。查看 RTU 采集统计：

```text
show_encoder_stats
```

清空 RTU 采集统计：

```text
clear_encoder_stats
```

上位机通过 Modbus TCP 读取：

| 参数 | 值 |
| --- | --- |
| 协议 | Modbus TCP |
| TCP 端口 | `502` |
| 功能码 | FC04 Read Input Registers |
| Unit ID | `1` |
| 数据格式 | 16 位无符号寄存器，高字节在前 |

一次读取完整传感器区：

```text
Function Code: 04
Start Address: 0x0000
Quantity: 27
```

## 3. 前置条件

| 项目 | 说明 |
| --- | --- |
| RS-485 收发器 | MCU UART 不能直接接 A/B 总线，需要外部 RS-485 收发器 |
| 从站地址 | 现场设备 Unit ID 必须和固件定义一致 |
| 串口参数 | 现场设备需要配置为 `115200 8N1` |
| 网络 | 以太网需要正常获取或配置 IP，Modbus TCP server 才能对外服务 |
| Kconfig | `CONFIG_CRANER_ENABLE_READ_*_THREAD=y` 才会创建对应采集线程 |

## 4. Devicetree：硬件描述

板级 DTS 通过 alias 给业务代码提供稳定入口：

```dts
aliases {
	modbus-slewing-encoder = &modbus_slewing_encoder;
	modbus-luffing-encoder = &modbus_luffing_encoder;
	modbus-hook-encoder = &modbus_hook_encoder;
	modbus-anemometer = &modbus_anemometer;
};
```

每路 UART 下都有一个 `zephyr,modbus-serial` 子节点。例如风速仪在 USART6：

```dts
&usart6 {
	pinctrl-0 = <&usart6_tx_pc6 &usart6_rx_pc7>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";

	modbus_anemometer: modbus-anemometer {
		compatible = "zephyr,modbus-serial";
		status = "okay";
	};
};
```

`src/modbus_rtu_client_app.c` 会在对应线程启用时用 `BUILD_ASSERT()` 检查 alias 是否存在，避免裁剪 DTS 时把硬件入口漏掉。

## 5. Kconfig/prj.conf：软件配置

项目用这些开关决定是否创建各路采集线程：

```conf
CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD=y
CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD=y
CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD=y
CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD=y
```

Modbus RTU/TCP 共用 Zephyr Modbus 子系统：

```conf
CONFIG_MODBUS=y
CONFIG_MODBUS_ROLE_CLIENT_SERVER=y
CONFIG_MODBUS_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
```

## 6. 业务/应用代码

RTU 线程启动后先通过 DTS alias 找到 Modbus interface，再初始化 client：

```c
encoder->iface = modbus_iface_get_by_name(encoder->iface_name);
modbus_init_client(encoder->iface, modbus_encoder_param);
```

每个轮询周期调用 FC03：

```c
err = modbus_read_holding_regs(client->iface,
			       client->unit_id,
			       client->start_addr,
			       regs,
			       client->register_count);
```

成功时写入时间戳、错误码 `0`、离线状态 `0` 和业务数据。失败时只更新错误码和离线状态，保留上一帧成功业务数据，方便上位机区分“设备离线”和“最后有效值”。

## 7. 如何扩展

| 需求 | 修改位置 |
| --- | --- |
| 改从站地址 | `src/modbus_rtu_client_app.c` 中对应 `*_UNIT_ID` |
| 改轮询周期 | `MODBUS_ENCODER_POLL_PERIOD_MS` 或 `MODBUS_ANEMOMETER_POLL_PERIOD_MS` |
| 改串口参数 | DTS 的 `current-speed` 和 C 代码中的 Modbus 参数 |
| 增加新传感器 | DTS 增加 UART 子节点和 alias，Kconfig 增加线程开关，寄存器表增加 Input Register，CMake 加入新模块 |
| 改对外寄存器 | `src/modbus_register_map.c` 和 RTU client 中的寄存器名字字段 |

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 编译报 missing alias | 确认对应 DTS alias 和 `zephyr,modbus-serial` 子节点存在且 `status = "okay"` |
| `ERROR_CODE=116` | RTU 超时，检查 Unit ID、波特率、A/B 接线、供电和终端电阻 |
| FC04 读到业务值但 `OFFLINE_STATUS=1` | 这是保留的最后成功值，当前链路仍异常 |
| 上位机读不到 `0x001A` | 确认读取数量覆盖 27 个 Input Register，或工具没有使用 30001 偏移地址 |
| Shell 无统计输出 | 确认 `CONFIG_SHELL=y` 且对应采集线程开关为 `y` |

生成文件检查：

```powershell
Select-String build\craner_general_stm32h743vit6\craner_encoder_hub\zephyr\zephyr.dts -Pattern "modbus-anemometer|usart6|zephyr,modbus-serial"
Select-String build\craner_general_stm32h743vit6\craner_encoder_hub\zephyr\.config -Pattern "CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD|CONFIG_MODBUS|CONFIG_UART_INTERRUPT_DRIVEN"
```
