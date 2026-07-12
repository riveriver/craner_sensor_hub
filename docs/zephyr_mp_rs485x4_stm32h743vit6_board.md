# MP RS485x4 STM32H743VIT6 板卡入门

## 1. 示例实现了什么

本文说明如何在当前 Zephyr 工程中新增并使用 `mp_rs485x4_stm32h743vit6` 板卡。

板卡信息：

| 项目 | 值 |
| --- | --- |
| Zephyr board ID | `mp_rs485x4_stm32h743vit6` |
| MCU | STM32H743VIT6 |
| PCB 硬件版本 | `1.2.0` |
| 调试串口 | USART1，PA9 TX / PA10 RX |
| Modbus RTU 编码器串口 | UART7 PE8 TX / PE7 RX，UART8 PE1 TX / PE0 RX，UART4 PD1 TX / PD0 RX，9600 8E1 |
| Ethernet | RMII |
| 电源控制 | PE6 `POWER_3V3_AND_CCTV`，PC13 `POWER_5V`，PB3 `POWER_NET_BRIGDE`，高电平导通 |
| 状态灯 | PD10，高电平亮，正常 500 ms 亮 / 500 ms 灭，异常快闪若干次 + 长停 |
| 静态 IP | `192.168.18.32/24` |

板卡目录：

```text
boards/mp/mp_rs485x4_stm32h743vit6/
```

## 2. 怎么使用

编译默认板卡：

```powershell
.\build.ps1
```

显式编译：

```powershell
.\build.ps1 -Board mp_rs485x4_stm32h743vit6
```

烧录：

```powershell
.\flash.ps1
```

打开 USART1 调试串口，参数 `115200 8N1`。应看到：

```text
craner_encoder_hub started on mp_rs485x4_stm32h743vit6
craner:~$
```

网络测试：

```powershell
ping 192.168.18.32
```

## 3. 前置条件

需要：

| 项目 | 说明 |
| --- | --- |
| Zephyr workspace | `build.ps1` 中已设置 `ZEPHYR_BASE` 和 SDK 路径 |
| 调试串口 | USART1 PA9/PA10 接 USB-TTL |
| Ethernet PHY | RMII PHY 硬件正确连接 |
| RS-485 | UART7 需要外接 RS-485 收发器 |
| 电源控制 | PE6、PC13、PB3 外部电源开关为高电平使能 |
| 状态灯 | PD10 外接 LED，高电平点亮 |
| ST-LINK | 用于烧录和调试 |

## 4. 设备树：硬件描述

板卡 DTS 文件：

```text
boards/mp/mp_rs485x4_stm32h743vit6/mp_rs485x4_stm32h743vit6.dts
```

根节点描述板卡身份：

```dts
/ {
	model = "MP RS485x4 STM32H743VIT6 PCB V1.2.0";
	compatible = "mp,mp-rs485x4-stm32h743vit6";
};
```

`model` 是人类可读名称，`compatible` 是设备树匹配字符串。

电源控制 GPIO：

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

应用启动时会通过 `src/power_control_app.c` 把这 3 个 GPIO 配置为 active 状态。由于这里使用 `GPIO_ACTIVE_HIGH`，所以 active 就是输出高电平。

状态灯 GPIO：

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

应用启动时会通过 `src/system_health_app.c` 创建系统健康检测线程。PD10 被作为状态灯使用：正常状态 500 ms 亮 / 500 ms 灭，异常状态按优先级快闪若干次后长停。

Console 和 Shell 使用 USART1：

```dts
chosen {
	zephyr,sram = &sram0;
	zephyr,flash = &flash0;
	zephyr,console = &usart1;
	zephyr,shell-uart = &usart1;
};

&usart1 {
	pinctrl-0 = <&usart1_tx_pa9 &usart1_rx_pa10>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};
```

Modbus RTU 使用 3 路 UART 连接编码器：

```dts
aliases {
	modbus-slewing-encoder = &modbus_slewing_encoder;
	modbus-luffing-encoder = &modbus_luffing_encoder;
	modbus-hook-encoder = &modbus_hook_encoder;
};

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

Ethernet RMII：

```dts
&mac {
	pinctrl-0 = <&eth_ref_clk_pa1
		     &eth_crs_dv_pa7
		     &eth_rxd0_pc4
		     &eth_rxd1_pc5
		     &eth_tx_en_pb11
		     &eth_txd0_pb12
		     &eth_txd1_pb13>;
	pinctrl-names = "default";
	local-mac-address = [02 00 00 00 02 32];
	phy-connection-type = "rmii";
	phy-handle = <&eth_phy>;
	status = "okay";
};
```

PHY 通过 MDIO 管理：

```dts
&mdio {
	pinctrl-0 = <&eth_mdio_pa2 &eth_mdc_pc1>;
	pinctrl-names = "default";
	status = "okay";

	eth_phy: ethernet-phy@0 {
		compatible = "ethernet-phy";
		reg = <0x00>;
		reset-gpios = <&gpioc 0 GPIO_ACTIVE_LOW>;
	};
};
```

## 5. Kconfig/prj.conf：软件配置

板卡默认配置在：

```text
boards/mp/mp_rs485x4_stm32h743vit6/mp_rs485x4_stm32h743vit6_defconfig
```

应用配置在：

```text
prj.conf
```

关键应用配置：

```conf
CONFIG_SERIAL=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y

CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_ETH_STM32_HAL=y

CONFIG_MODBUS=y
CONFIG_MODBUS_ROLE_CLIENT_SERVER=y
CONFIG_MODBUS_RAW_ADU=y
```

静态 IP：

```conf
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.18.32"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
CONFIG_NET_CONFIG_MY_IPV4_GW="192.168.18.1"
```

## 6. 业务/应用代码

板卡本身只描述硬件，业务功能由 `src/` 下模块实现：

| 文件 | 作用 |
| --- | --- |
| `src/main.c` | 启动打印，保持主线程 |
| `src/shell_app.c` | 注册 `fw_time` Shell 命令 |
| `src/modbus_rtu_client_app.c` | Modbus RTU client 示例，当前线程注释 |
| `src/modbus_tcp_server_app.c` | Modbus TCP server 线程版 |
| `src/log_app_sensor.c` / `src/log_app_comm.c` | 日志示例，当前线程注释 |

这些文件通过 `CMakeLists.txt` 加入编译：

```cmake
target_sources(app PRIVATE
	src/main.c
	src/shell_app.c
	src/modbus_tcp_server_app.c
)
```

## 7. 如何扩展

| 需求 | 修改位置 |
| --- | --- |
| 改调试串口 | DTS 的 `chosen` 和对应 UART 节点 |
| 增加 RS-485 方向控制 | 对应 `modbus-encoder-*` 节点添加 `de-gpios` / `re-gpios` |
| 改 PHY 地址 | `ethernet-phy@0` 和 `reg` |
| 改 IP | `prj.conf` 的 `CONFIG_NET_CONFIG_MY_IPV4_*` |
| 新增外设 | 在 DTS 启用外设节点，并在 `prj.conf` 启用驱动 |

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| Zephyr 找不到板卡 | `BOARD_ROOT` 是否包含当前工程，板卡目录结构是否正确 |
| 串口无输出 | USART1 PA9/PA10 接线、`zephyr,console` |
| Ethernet 无 link | PA1 REF_CLK、PHY 地址、复位脚、网线 |
| Modbus RTU 不工作 | UART7 PE8/PE7、RS-485 收发器、方向控制 |
| IP 不对 | `.config` 中 `CONFIG_NET_CONFIG_MY_IPV4_ADDR` |

生成文件检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "zephyr,console|uart7|ethernet-phy|phy-connection-type"
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_BOARD|CONFIG_NET_CONFIG_MY_IPV4"
```
