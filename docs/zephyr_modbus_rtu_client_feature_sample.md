# Zephyr Modbus RTU Client 示例：三路编码器采集

## 1. 示例实现了什么

本示例演示如何在 Zephyr 中使用多个 Modbus RTU client，通过 3 路独立 UART 周期读取 3 个编码器的 Holding Register，并把每次通信结果写入共享的 `modbus_register_service`。

当前业务参数在 `src/modbus_rtu_client_app.c`：

| 项目 | 值 |
| --- | --- |
| Unit ID | `1` |
| 功能码 | FC03 Read Holding Registers |
| 远端起始地址 | `0x0002` |
| 远端寄存器数量 | `2` |
| 轮询周期 | `25 ms` |
| 串口格式 | `9600 8E1` |
| RX timeout | `200000 us` |

3 路编码器串口：

| 机构 | UART | TX | RX | Modbus interface alias |
| --- | --- | --- | --- | --- |
| 回转 SWING/SLEWING | UART7 | PE8 | PE7 | `modbus-slewing-encoder` |
| 变幅 LUFFING | UART8 | PE1 | PE0 | `modbus-luffing-encoder` |
| 吊钩 HOISTING | UART4 | PD1 | PD0 | `modbus-hook-encoder` |

每个编码器读取远端 Holding Register：

| 远端地址 | 本地含义 |
| --- | --- |
| `0x0002` | 圈数，写入对应机构的 `*_TRUN_CNT` |
| `0x0003` | 单圈值，写入对应机构的 `*_SINAGLE_VAL` |

每次 `modbus_read_holding_regs()` 返回后，都会记录一次通信结果：

| 本地寄存器 | 成功时 | 失败时 |
| --- | --- | --- |
| `*_TIMESTAMP_H/L` | 写入当前 `k_uptime_get_32()` | 保持上一次成功读取时间 |
| `*_ERROR_CODE` | `0` | 正数错误码，例如 timeout `116` |
| `*_OFFLINE_STATUS` | `0` | `1` |
| `*_TRUN_CNT` | 远端 `0x0002` 的值 | 保持上一次成功读取值 |
| `*_SINAGLE_VAL` | 远端 `0x0003` 的值 | 保持上一次成功读取值 |

应用层写寄存器时使用名字访问，例如 `REG_SLEWING_TRUN_CNT`，不再在 RTU 业务代码里硬编码本地 input register 地址。

## 2. 怎么使用

确认 3 个线程已启用：

```c
K_THREAD_DEFINE(slewing_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &slewing_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);

K_THREAD_DEFINE(luffing_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &luffing_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);

K_THREAD_DEFINE(hook_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &hook_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);
```

编译：

```powershell
.\build.ps1
```

连接 Modbus RTU 从站，参数设置为：

| 参数 | 值 |
| --- | --- |
| Slave ID | 每路默认 `1` |
| Baudrate | `9600` |
| Data bits | `8` |
| Parity | Even |
| Stop bits | `1` |

读取成功时不会再用 `LOG_INF` 周期打印 `single/turn_cnt` 数据，避免 3 路编码器刷屏。成功数据会写入 input register，失败时只更新错误码和离线状态，供 Modbus TCP client 用 FC04 判断当前通信状态。

读取失败时日志类似：

```text
SLEWING encoder FC03 addr=0x0002 qty=2 failed: -116
```

查看 3 路 RTU 通信统计：

```text
show_encoder_stats
```

清除已有统计并重新统计：

```text
clear_encoder_stats
```

输出类似：

```text
Modbus RTU stats; avg/max use successful intervals only:
SLEWING encoder: total=42 success=40 failure=2 success_rate=95.23% success_intervals=39 avg=3000 ms max=3001 ms last_error=0
LUFFING encoder: total=42 success=42 failure=0 success_rate=100.00% success_intervals=41 avg=3000 ms max=3001 ms last_error=0
HOISTING encoder: total=42 success=38 failure=4 success_rate=90.47% success_intervals=37 avg=3158 ms max=6000 ms last_error=-116
```

如果需要看每次成功通信间隔的 DBG 日志，可以在 shell 中调高日志级别：

```text
log enable dbg modbus_rtu_client_app
```

## 3. 前置条件

| 项目 | 说明 |
| --- | --- |
| RS-485 收发器 | MCU UART 不能直接接 A/B 总线 |
| 从站设备 | 3 个编码器默认 Unit ID 都为 `1`，支持 FC03 |
| 串口参数 | `9600 8E1` |
| 接线 | TX/RX 或 RS-485 A/B 正确连接，GND 共地 |
| 方向控制 | 当前 DTS 未配置 DE/nRE GPIO，如硬件需要需补充 |
| 寄存器表 | `modbus_register_map.c` 中必须存在 RTU 应用层使用的寄存器名字 |

如果硬件是 RS-485 半双工且需要 MCU 控制方向脚，需要在对应 `modbus-encoder-*` 节点增加方向控制 GPIO。

## 4. 设备树：硬件描述

Zephyr Modbus 串口接口由 UART 子节点描述。每一路 UART 下面放一个 `zephyr,modbus-serial` 子节点，业务代码通过 alias 找到它。

3 路 alias：

```dts
aliases {
	modbus-slewing-encoder = &modbus_slewing_encoder;
	modbus-luffing-encoder = &modbus_luffing_encoder;
	modbus-hook-encoder = &modbus_hook_encoder;
};
```

UART7 编码器：

```dts
&uart7 {
	pinctrl-0 = <&uart7_tx_pe8 &uart7_rx_pe7>;
	pinctrl-names = "default";
	current-speed = <9600>;
	status = "okay";

	modbus_slewing_encoder: modbus-slewing-encoder {
		compatible = "zephyr,modbus-serial";
		status = "okay";
	};
};
```

UART8 编码器：

```dts
&uart8 {
	pinctrl-0 = <&uart8_tx_pe1 &uart8_rx_pe0>;
	pinctrl-names = "default";
	current-speed = <9600>;
	status = "okay";

	modbus_luffing_encoder: modbus-luffing-encoder {
		compatible = "zephyr,modbus-serial";
		status = "okay";
	};
};
```

UART4 编码器：

```dts
&uart4 {
	pinctrl-0 = <&uart4_tx_pd1 &uart4_rx_pd0>;
	pinctrl-names = "default";
	current-speed = <9600>;
	status = "okay";

	modbus_hook_encoder: modbus-hook-encoder {
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
| `aliases` | 给业务代码提供稳定的设备树查找入口 |

业务代码通过 alias 找到 3 个 Modbus serial 节点：

```c
#define MODBUS_SLEWING_ENCODER_NODE DT_ALIAS(modbus_slewing_encoder)
#define MODBUS_LUFFING_ENCODER_NODE DT_ALIAS(modbus_luffing_encoder)
#define MODBUS_HOISTING_ENCODER_NODE DT_ALIAS(modbus_hook_encoder)
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
| `CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE=y` | 放宽 Zephyr Modbus 串口格式检查，便于兼容现场设备 |

本项目实际使用 8E1：8 个数据位、Even 偶校验、1 个停止位。业务代码中对应 `UART_CFG_PARITY_EVEN` 和 `UART_CFG_STOP_BITS_1`。

## 6. 业务/应用代码

初始化参数：

```c
static const struct modbus_iface_param modbus_encoder_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = MODBUS_ENCODER_RX_TIMEOUT_US,
	.serial = {
		.baud = MODBUS_ENCODER_BAUDRATE,
		.parity = UART_CFG_PARITY_EVEN,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};
```

每一路编码器保存自己的 interface name、Modbus 参数和本地寄存器名字：

```c
static struct modbus_encoder_client slewing_encoder = {
	.name = "SLEWING encoder",
	.iface_name = DEVICE_DT_NAME(MODBUS_SLEWING_ENCODER_NODE),
	.unit_id = MODBUS_ENCODER_UNIT_ID,
	.start_addr = MODBUS_ENCODER_START_ADDR,
	.register_count = MODBUS_ENCODER_REGISTER_COUNT,
	.timestamp_high_name = "REG_SLEWING_TIMESTAMP_H",
	.timestamp_low_name = "REG_SLEWING_TIMESTAMP_L",
	.error_code_name = "REG_SLEWING_ERROR_CODE",
	.offline_status_name = "REG_SLEWING_OFFLINE_STATUS",
	.turn_count_name = "REG_SLEWING_TRUN_CNT",
	.single_value_name = "REG_SLEWING_SINAGLE_VAL",
	.iface = -1,
};
```

线程启动后先初始化当前 encoder 对应的 Modbus interface：

```c
encoder->iface = modbus_iface_get_by_name(encoder->iface_name);
modbus_init_client(encoder->iface, modbus_encoder_param);
```

读取远端寄存器：

```c
err = modbus_read_holding_regs(encoder->iface,
			       encoder->unit_id,
			       encoder->start_addr,
			       regs,
			       encoder->register_count);
```

每次调用返回后，先更新统计：

```c
modbus_encoder_record_attempt(encoder, err);
```

再把通信结果写入寄存器表。这里是应用层，所以使用按起始名字访问的连续区间 API。一次通信结果会先整理成数组，再从起始寄存器名字解析到起始地址，只进入一次寄存器互斥锁写入连续寄存器：

```c
const uint16_t values[] = {
	error_code,
	offline_status,
	regs[0],
	regs[1],
};

modbus_register_service_write_inputs_by_name(
	encoder->error_code_name, values, ARRAY_SIZE(values));
```

统计内容包括：

| 字段 | 含义 |
| --- | --- |
| `total` | 总通信次数，等于成功次数加失败次数 |
| `success` | 成功通信次数 |
| `failure` | 失败通信次数 |
| `success_rate` | 通信成功率，按 `success / total` 计算 |
| `success_intervals` | 成功间隔样本数，第一次成功没有前一个成功点，所以不计入间隔 |
| `avg` | 成功到成功之间的平均间隔 |
| `max` | 成功到成功之间的最大间隔 |
| `last_error` | 最近一次 Modbus 调用返回值，`0` 表示成功 |

`show_encoder_stats` 直接读取这些统计值并打印。`clear_encoder_stats` 会清空 3 路编码器统计值，下一次通信开始重新计数。成功通信间隔也会用 `LOG_DBG` 记录，但默认运行日志级别是 INF，所以不会刷屏。

线程用 `next_poll_time` 控制 25 ms 周期，避免每次读操作耗时造成长期漂移。

## 7. 如何扩展

| 需求 | 修改位置 |
| --- | --- |
| 改从站地址 | `MODBUS_ENCODER_UNIT_ID`，或给每个 `*_encoder` 单独设置 |
| 改读取地址 | `MODBUS_ENCODER_START_ADDR` |
| 改读取数量 | `MODBUS_ENCODER_REGISTER_COUNT`，并同步本地记录逻辑 |
| 改轮询频率 | `MODBUS_ENCODER_POLL_PERIOD_MS` |
| 改波特率 | DTS 的 `current-speed` 和 `MODBUS_ENCODER_BAUDRATE` |
| 增减编码器 | DTS 增减 UART 子节点，C 代码增减 `*_encoder` 和 `K_THREAD_DEFINE()` |
| 改本地寄存器映射 | 修改 `*_encoder` 中的 `*_name` 字段，并保证 map 中存在对应名字 |

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 没有任何 RTU 日志 | 3 个 `K_THREAD_DEFINE()` 是否存在 |
| 返回超时 | 从站 ID、波特率、A/B 接线、方向控制 |
| 编译找不到 Modbus 节点 | DTS 是否有 3 个 `modbus-encoder-*` alias 和 `zephyr,modbus-serial` 节点 |
| 按名字写寄存器失败 | `modbus_register_map.c` 中是否存在完全一致的 `REG_*` 名称 |
| 数据错误 | 检查远端寄存器地址是否需要 0-based 或 1-based 转换 |
| RS-485 只能发不能收 | 检查 DE/nRE 控制和收发器电路 |

生成文件检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "modbus-encoder|uart7|uart8|uart4|zephyr,modbus-serial"
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_MODBUS|CONFIG_UART_INTERRUPT_DRIVEN"
```
