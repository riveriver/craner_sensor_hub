# Zephyr 心跳灯示例：PD10 周期闪烁

## 1. 示例实现了什么

本示例实现一个心跳灯线程，用来观察固件是否仍在运行。心跳灯连接在 PD10，高电平亮、低电平灭。固件启动后会自动创建线程，让 LED 亮 1 秒、灭 1 秒循环闪烁。

| 信号 | MCU 引脚 | 有效电平 | 行为 |
| --- | --- | --- | --- |
| `HEARTBEAT_LED` | PD10 | 高电平亮 | 1s 亮 / 1s 灭 |

## 2. 怎么使用

构建并烧录后观察 PD10 对应 LED。启动日志应看到：

```text
Heartbeat LED started on PD10, 1s on / 1s off
```

如果没有焊接 LED，也可以用万用表或示波器测量 PD10，确认电平每秒切换一次。

## 3. 前置条件

需要确认硬件 LED 是高电平点亮。如果硬件改成低电平点亮，把设备树中的 `GPIO_ACTIVE_HIGH` 改为 `GPIO_ACTIVE_LOW`，业务代码可以继续使用 `gpio_pin_set_dt(&heartbeat_led, 1)` 表示点亮。

## 4. 设备树：硬件描述

板级 DTS 中用 `gpio-leds` 描述 PD10，并通过 alias 给 C 代码提供稳定入口：

```dts
aliases {
	heartbeat-led = &heartbeat_led;
};

leds {
	compatible = "gpio-leds";

	heartbeat_led: heartbeat_led {
		gpios = <&gpiod 10 GPIO_ACTIVE_HIGH>;
		label = "HEARTBEAT_LED";
	};
};
```

关键点：

| DTS 项 | 作用 |
| --- | --- |
| `heartbeat-led` | C 代码通过 `DT_ALIAS(heartbeat_led)` 找到 LED |
| `&gpiod 10` | 使用 GPIOD 的第 10 号引脚，即 PD10 |
| `GPIO_ACTIVE_HIGH` | active 状态为高电平，也就是点亮 |

## 5. Kconfig/prj.conf：软件配置

心跳灯只需要 GPIO 子系统：

```conf
CONFIG_GPIO=y
```

当前工程已经启用该配置。

## 6. 业务/应用代码

代码在 `src/heartbeat_led_app.c`。先从设备树获取 GPIO：

```c
static const struct gpio_dt_spec heartbeat_led =
	GPIO_DT_SPEC_GET(HEARTBEAT_LED_NODE, gpios);
```

线程启动后配置为输出，并循环设置亮灭：

```c
gpio_pin_configure_dt(&heartbeat_led, GPIO_OUTPUT_INACTIVE);

while (1) {
	gpio_pin_set_dt(&heartbeat_led, 1);
	k_sleep(K_MSEC(1000));

	gpio_pin_set_dt(&heartbeat_led, 0);
	k_sleep(K_MSEC(1000));
}
```

线程通过 `K_THREAD_DEFINE()` 自动启动，不需要在 `main.c` 调用：

```c
K_THREAD_DEFINE(heartbeat_led_tid, HEARTBEAT_LED_STACK_SIZE,
		heartbeat_led_thread, NULL, NULL, NULL,
		HEARTBEAT_LED_PRIORITY, 0, 0);
```

## 7. 怎么扩展

| 需求 | 修改位置 |
| --- | --- |
| 改 LED 引脚 | DTS 中的 `gpios = <&gpiod 10 ...>` |
| 改亮灭时间 | `HEARTBEAT_LED_ON_TIME_MS` / `HEARTBEAT_LED_OFF_TIME_MS` |
| 改有效电平 | DTS 中的 `GPIO_ACTIVE_HIGH` / `GPIO_ACTIVE_LOW` |

## 8. 故障排查

| 现象 | 检查 |
| --- | --- |
| 编译报 missing heartbeat-led alias | 检查板级 DTS 是否有 `heartbeat-led` alias |
| 日志有但灯不亮 | 检查 LED 极性、限流电阻、PD10 焊接 |
| 一直亮或一直灭 | 用示波器测 PD10，确认代码输出是否在切换 |

生成文件检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "heartbeat-led|heartbeat_led|gpiod"
```
