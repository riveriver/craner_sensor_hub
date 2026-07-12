# Zephyr 电源控制 GPIO 示例：启动后拉高供电使能

## 1. 示例实现了什么

本示例演示如何在 Zephyr 中用 GPIO 控制板级电源开关。固件启动后会自动把 3 个电源控制引脚配置为输出高电平，保证对应电源通道导通。

| 电源控制信号 | MCU 引脚 | 有效电平 | 作用 |
| --- | --- | --- | --- |
| `POWER_3V3_AND_CCTV` | PE6 | 高电平有效 | 使能 3V3 与 CCTV 相关供电 |
| `POWER_5V` | PC13 | 高电平有效 | 使能 5V 供电 |
| `POWER_NET_BRIGDE` | PB3 | 高电平有效 | 使能网络桥相关供电 |

这 3 个 GPIO 已在 `craner_general_stm32h743vit6` 和 `mp_rs485x4_stm32h743vit6` 两块板的设备树中声明。应用代码在 `src/power_control_app.c`，通过 `SYS_INIT()` 自动运行，不需要在 `main.c` 手动调用。

## 2. 怎么使用

构建 MP 板：

```powershell
python -m west build -p always -b mp_rs485x4_stm32h743vit6 . -d build\mp_rs485x4_stm32h743vit6
```

构建 Craner 板：

```powershell
python -m west build -p always -b craner_general_stm32h743vit6 . -d build\craner_general_stm32h743vit6
```

烧录后打开调试串口，应看到类似日志：

```text
POWER_3V3_AND_CCTV enabled
POWER_5V enabled
POWER_NET_BRIGDE enabled
```

也可以用万用表或示波器测量 PE6、PC13、PB3，确认启动后为高电平。

## 3. 前置条件

需要确认硬件上这 3 个信号确实为高电平导通。如果外部电源开关芯片是低电平使能，需要把设备树里的 `GPIO_ACTIVE_HIGH` 改成 `GPIO_ACTIVE_LOW`，业务代码仍然使用 `GPIO_OUTPUT_ACTIVE`。

PB3 在 STM32 上常见复用功能包括调试相关功能。如果硬件调试方式占用了 PB3，需要确认调试接口配置不会和 `POWER_NET_BRIGDE` 冲突。

## 4. 设备树：硬件描述

设备树使用一个 `gpio-leds` 容器描述 3 个可输出的 GPIO，并通过 `aliases` 给业务代码提供稳定名字：

```dts
aliases {
	power-3v3-and-cctv = &power_3v3_and_cctv;
	power-5v = &power_5v;
	power-net-brigde = &power_net_brigde;
};

power_control {
	compatible = "gpio-leds";

	power_3v3_and_cctv: power_3v3_and_cctv {
		gpios = <&gpioe 6 GPIO_ACTIVE_HIGH>;
		label = "POWER_3V3_AND_CCTV";
	};

	power_5v: power_5v {
		gpios = <&gpioc 13 GPIO_ACTIVE_HIGH>;
		label = "POWER_5V";
	};

	power_net_brigde: power_net_brigde {
		gpios = <&gpiob 3 GPIO_ACTIVE_HIGH>;
		label = "POWER_NET_BRIGDE";
	};
};
```

关键点：

| DTS 项 | 作用 |
| --- | --- |
| `aliases` | 让 C 代码用 `DT_ALIAS()` 找到节点 |
| `gpios` | 指定 GPIO 控制器、引脚号和有效电平 |
| `GPIO_ACTIVE_HIGH` | 表示业务上的 active 状态是高电平 |
| `compatible = "gpio-leds"` | 复用 Zephyr 已有的 GPIO 输出类 binding |

## 5. Kconfig/prj.conf：软件配置

本功能只需要 GPIO 子系统：

```conf
CONFIG_GPIO=y
```

工程的 `prj.conf` 已经启用该配置，所以不需要额外增加 Kconfig。

## 6. 业务/应用代码

业务代码在 `src/power_control_app.c`。它先通过 alias 获取 3 个 GPIO：

```c
#define POWER_3V3_AND_CCTV_NODE DT_ALIAS(power_3v3_and_cctv)
#define POWER_5V_NODE DT_ALIAS(power_5v)
#define POWER_NET_BRIGDE_NODE DT_ALIAS(power_net_brigde)
```

然后把每个 GPIO 配置成 active 状态：

```c
gpio_pin_configure_dt(&power->gpio, GPIO_OUTPUT_ACTIVE);
```

因为设备树里写的是 `GPIO_ACTIVE_HIGH`，所以 `GPIO_OUTPUT_ACTIVE` 最终就是输出高电平。如果以后硬件改为低电平导通，只需要改设备树 active flag，应用代码不用改。

模块通过 `SYS_INIT()` 注册：

```c
SYS_INIT(power_control_app_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
```

这表示内核启动后会自动执行电源 GPIO 初始化，不需要业务线程轮询。

新增源码需要加入 `CMakeLists.txt`：

```cmake
target_sources(app PRIVATE
	src/power_control_app.c
)
```

## 7. 怎么扩展

| 需求 | 修改位置 |
| --- | --- |
| 改引脚 | 板级 `.dts` 中对应 `gpios = <...>` |
| 改有效电平 | `GPIO_ACTIVE_HIGH` / `GPIO_ACTIVE_LOW` |
| 增加一路电源 | DTS 增加一个子节点，C 数组增加一项 |
| 更早拉高 | 可把 `SYS_INIT` level/priority 调整到更早阶段 |

## 8. 故障排查

| 现象 | 检查 |
| --- | --- |
| 上电后外设没供电 | 测 PE6、PC13、PB3 是否为高电平 |
| 日志显示 GPIO controller not ready | 检查对应 GPIO 控制器是否被启用 |
| 编译报 missing alias | 检查板级 DTS 的 `aliases` 是否包含 3 个 power alias |
| PB3 没有输出 | 检查调试接口或复用功能是否占用 PB3 |

可检查生成设备树：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "power_control|POWER_3V3_AND_CCTV|POWER_5V|POWER_NET_BRIGDE|power-net-brigde"
```
