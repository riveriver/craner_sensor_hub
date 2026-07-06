# Zephyr Modbus RTU Client 功能实现说明

本文说明本项目如何在 `craner_general_board_v110` 上启用 Modbus RTU client，并通过 USART6 周期读取从站寄存器。

当前实现：

| 项目 | 配置 |
| --- | --- |
| 串口 | USART6 |
| TX | PC6 |
| RX | PC7 |
| 波特率 | 115200 |
| 数据格式 | 8N1 |
| Modbus 模式 | RTU client |
| 从站地址 | `1` |
| 功能码 | FC03，读保持寄存器 |
| 起始地址 | `0x0002` |
| 寄存器数量 | 2 |
| 读取频率 | 25 Hz |

## 一、板级串口配置

板级 DTS 文件：

```text
boards/craner/craner_general_board_v110/craner_general_board_v110.dts
```

启用 USART6，并把 PC6/PC7 配置为 Modbus RTU 使用的串口：

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

`modbus0` 节点使用 Zephyr 的 `zephyr,modbus-serial` binding。应用启动时会通过这个节点找到 Modbus 串口接口。

## 二、项目配置

项目配置文件：

```text
prj.conf
```

新增配置：

```conf
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_UART_LINE_CTRL=n
CONFIG_MODBUS=y
CONFIG_MODBUS_ROLE_CLIENT=y
CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE=y
```

其中：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_UART_INTERRUPT_DRIVEN=y` | Modbus 串口后端使用 UART 中断收发 |
| `CONFIG_MODBUS=y` | 启用 Zephyr Modbus 子系统 |
| `CONFIG_MODBUS_ROLE_CLIENT=y` | 只启用 Modbus client 角色 |
| `CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE=y` | 允许显式配置 8N1 的 1 个停止位 |

Zephyr Modbus RTU 在无校验时默认按规范使用 2 个停止位。本项目为了匹配常见设备的 8N1 参数，启用了 non-compliant serial mode，并在代码中设置 `UART_CFG_STOP_BITS_1`。

## 三、应用模块

源码文件：

```text
src/modbus_rtu_client_app.c
```

关键参数集中在文件顶部：

```c
#define MODBUS_CLIENT_UNIT_ID 1
#define MODBUS_CLIENT_BAUDRATE 115200
#define MODBUS_CLIENT_RX_TIMEOUT_US 20000
#define MODBUS_CLIENT_POLL_PERIOD_MS 40
#define MODBUS_CLIENT_START_ADDR 0x0002
#define MODBUS_CLIENT_REGISTER_COUNT 2
```

`MODBUS_CLIENT_POLL_PERIOD_MS` 为 40 ms，对应 25 Hz。

读取逻辑使用 Zephyr 原生 API：

```c
err = modbus_read_holding_regs(modbus_client_iface,
			       MODBUS_CLIENT_UNIT_ID,
			       MODBUS_CLIENT_START_ADDR,
			       regs,
			       ARRAY_SIZE(regs));
```

读取成功后会通过日志打印：

```text
FC03 addr=0x0002 qty=2 value[0]=0x1234 value[1]=0x5678
```

## 四、编译和烧录

编译：

```powershell
.\build.ps1
```

烧录：

```powershell
.\flash.ps1
```

打开 UART5 控制台串口查看日志：

| 参数 | 值 |
| --- | --- |
| 波特率 | 115200 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

注意：控制台仍然使用 UART5，Modbus RTU 使用 USART6。

## 五、硬件接线

如果接的是 TTL 串口 Modbus 设备：

```text
板子 PC6 / USART6_TX -> 从站 RX
板子 PC7 / USART6_RX -> 从站 TX
板子 GND             -> 从站 GND
```

如果接的是 RS-485 Modbus 设备，需要外接 RS-485 收发器。本次实现没有配置 DE/nRE 方向控制引脚，因为当前需求只提供了 USART6 的 TX/RX 引脚。若硬件需要 MCU 控制方向，需要在 `modbus0` 节点中补充 `de-gpios` 或 `re-gpios`。

## 六、常见问题

### 1. 一直打印 FC03 failed

检查：

| 检查项 | 说明 |
| --- | --- |
| 从站地址 | 当前代码默认 `MODBUS_CLIENT_UNIT_ID` 为 `1` |
| 寄存器类型 | 当前读的是保持寄存器 FC03，不是输入寄存器 FC04 |
| 寄存器地址 | 当前起始地址为 `0x0002`，数量为 2 |
| 串口参数 | 当前为 115200 8N1 |
| 接线 | TX/RX 需要交叉连接，且 GND 必须共地 |

### 2. 设备要求 9600 或偶校验

修改 `src/modbus_rtu_client_app.c`：

```c
#define MODBUS_CLIENT_BAUDRATE 9600
```

并调整：

```c
.parity = UART_CFG_PARITY_EVEN,
```

### 3. 设备需要读输入寄存器

当前代码使用：

```c
modbus_read_holding_regs(...)
```

如果设备文档要求 FC04，需要改成：

```c
modbus_read_input_regs(...)
```

### 4. 25 Hz 不稳定

当前线程按 40 ms 周期发起读取。若从站响应慢、超时、串口速率低，实际读取频率会下降。可以降低读取频率，或缩短 `MODBUS_CLIENT_RX_TIMEOUT_US`，但超时过短会导致正常响应也被判失败。
