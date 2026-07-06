# Zephyr Modbus RTU Client 示例：串口轮询寄存器

## 1. 示例实现了什么

本示例演示如何在 Zephyr 中使用 Modbus RTU client，通过板卡上的 Modbus 串口周期读取从站 Holding Register。

当前业务参数在 `src/modbus_rtu_client_app.c`：

| 项目 | 值 |
| --- | --- |
| Unit ID | `1` |
| 功能码 | FC03 Read Holding Registers |
| 起始地址 | `0x0002` |
| 数量 | `2` |
| 轮询周期 | `40 ms`，即 25 Hz |
| 串口格式 | `115200 8N1` |

两块板的 Modbus 串口不同：

| 板卡 | Modbus UART | TX | RX |
| --- | --- | --- | --- |
| `craner_general_stm32h743vit6` | USART6 | PC6 | PC7 |
| `mp_rs485x4_stm32h743vit6` | UART7 | PE8 | PE7 |

注意：当前 `K_THREAD_DEFINE(modbus_rtu_client_tid, ...)` 是注释状态，所以默认固件不会自动启动 RTU client。需要启用该宏后才会周期读取。

## 2. 怎么使用

启用线程：

```c
K_THREAD_DEFINE(modbus_rtu_client_tid, MODBUS_CLIENT_STACK_SIZE,
		modbus_rtu_client_thread, NULL, NULL, NULL,
		MODBUS_CLIENT_PRIORITY, 0, 0);
```

编译：

```powershell
.\build.ps1
```

连接 Modbus RTU 从站，参数设置为：

| 参数 | 值 |
| --- | --- |
| Slave ID | `1` |
| Baudrate | `115200` |
| Data bits | `8` |
| Parity | None |
| Stop bits | `1` |

读取成功时日志类似：

```text
FC03 addr=0x0002 qty=2 value[0]=0x1234 value[1]=0x5678
```

读取失败时日志类似：

```text
FC03 addr=0x0002 qty=2 failed: -116
```

## 3. 前置条件

需要：

| 项目 | 说明 |
| --- | --- |
| RS-485 收发器 | MCU UART 不能直接接 A/B 总线 |
| 从站设备 | Unit ID 为 `1`，支持 FC03 |
| 串口参数 | `115200 8N1` |
| 接线 | TX/RX 或 RS-485 A/B 正确连接，GND 共地 |
| 方向控制 | 当前 DTS 未配置 DE/nRE GPIO，如硬件需要需补充 |

如果硬件是 RS-485 半双工且需要 MCU 控制方向脚，需要在 `modbus0` 节点增加方向控制 GPIO。

## 4. 设备树：硬件描述

Zephyr Modbus 串口接口由 UART 子节点描述：

Craner 板：

```dts
&usart6 {
	pinctrl-0 = <&usart6_tx_pc6 &usart6_rx_pc7>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";

	modbus0 {
		compatible = "zephyr,modbus-serial";
		status = "okay";
	};
};
```

MP 板：

```dts
&uart7 {
	pinctrl-0 = <&uart7_tx_pe8 &uart7_rx_pe7>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";

	modbus0 {
		compatible = "zephyr,modbus-serial";
		status = "okay";
	};
};
```

关键点：

| DTS 项 | 作用 |
| --- | --- |
| `pinctrl-0` | 把 UART TX/RX 绑定到具体引脚 |
| `current-speed` | UART 默认波特率 |
| `status = "okay"` | 启用该 UART |
| `compatible = "zephyr,modbus-serial"` | 让 Zephyr 创建 Modbus serial interface |

业务代码通过这个宏找到第一个可用的 Modbus serial 节点：

```c
#define MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)
```

## 5. Kconfig/prj.conf：软件配置

相关配置：

```conf
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_UART_LINE_CTRL=n

CONFIG_MODBUS=y
CONFIG_MODBUS_ROLE_CLIENT_SERVER=y
CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE=y
```

含义：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_UART_INTERRUPT_DRIVEN=y` | Modbus serial 后端使用中断驱动 UART |
| `CONFIG_MODBUS=y` | 启用 Modbus 子系统 |
| `CONFIG_MODBUS_ROLE_CLIENT_SERVER=y` | 同时编译 client 和 server 能力 |
| `CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE=y` | 允许 RTU 使用 8N1 的 1 个停止位 |

Modbus 标准在无校验时常见要求 2 个停止位，但很多设备使用 8N1。本项目显式设置 `UART_CFG_STOP_BITS_1`，所以打开 non-compliant 模式。

## 6. 业务/应用代码

初始化参数：

```c
static const struct modbus_iface_param modbus_client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = MODBUS_CLIENT_RX_TIMEOUT_US,
	.serial = {
		.baud = MODBUS_CLIENT_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};
```

初始化流程：

```c
modbus_client_iface = modbus_iface_get_by_name(iface_name);
modbus_init_client(modbus_client_iface, modbus_client_param);
```

读取寄存器：

```c
err = modbus_read_holding_regs(modbus_client_iface,
			       MODBUS_CLIENT_UNIT_ID,
			       MODBUS_CLIENT_START_ADDR,
			       regs,
			       ARRAY_SIZE(regs));
```

线程用 `next_poll_time` 控制 40 ms 周期，避免每次读操作耗时造成长期漂移。

## 7. 如何扩展

常见修改：

| 需求 | 修改位置 |
| --- | --- |
| 改从站地址 | `MODBUS_CLIENT_UNIT_ID` |
| 改读取地址 | `MODBUS_CLIENT_START_ADDR` |
| 改读取数量 | `MODBUS_CLIENT_REGISTER_COUNT` |
| 改频率 | `MODBUS_CLIENT_POLL_PERIOD_MS` |
| 改波特率 | DTS 的 `current-speed` 和 `MODBUS_CLIENT_BAUDRATE` |
| 读 Input Register | 改用 `modbus_read_input_regs()` |

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 没有任何 RTU 日志 | `K_THREAD_DEFINE()` 当前是否仍注释 |
| 返回超时 | 从站 ID、波特率、A/B 接线、方向控制 |
| 编译找不到 Modbus 节点 | DTS 是否有 `zephyr,modbus-serial` 且 `status = "okay"` |
| 数据错误 | 检查寄存器地址是否需要 0-based 或 1-based 转换 |
| RS-485 只能发不能收 | 检查 DE/nRE 控制和收发器电路 |

生成文件检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "modbus0|zephyr,modbus-serial|uart7"
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_MODBUS|CONFIG_UART_INTERRUPT_DRIVEN"
```
