# MP RS485x4 STM32H743VIT6 板级支持说明

本文说明本项目新增的 Zephyr 板卡 `mp_rs485x4_stm32h743vit6`。该板使用 STM32H743VIT6，控制台走 USART1，以太网使用 RMII，Modbus RTU 使用 UART7。

## 一、板卡基本信息

| 项目 | 配置 |
| --- | --- |
| Zephyr 板卡 ID | `mp_rs485x4_stm32h743vit6` |
| PCB 硬件版本 | `1.2.0` |
| MCU | STM32H743VIT6 |
| SoC qualifier | `stm32h743xx` |
| 控制台 / Shell | USART1，PA9 TX / PA10 RX，115200 |
| 以太网 | STM32 MAC，RMII，外部 PHY |
| PHY 复位 | PC0，低有效 |
| PHY 地址 | `0x00` |
| IPv4 地址 | 静态地址 `192.168.18.32/24` |
| Modbus RTU | UART7，PE8 TX / PE7 RX，115200 |

## 二、文件位置

板级文件位于：

```text
boards/mp/mp_rs485x4_stm32h743vit6/
```

关键文件：

| 文件 | 作用 |
| --- | --- |
| `mp_rs485x4_stm32h743vit6.dts` | 配置时钟、USART1、UART7、RMII 以太网、PHY reset |
| `mp_rs485x4_stm32h743vit6_defconfig` | 板级默认 Kconfig |
| `board.yml` | Zephyr 新版 board 元数据 |
| `mp_rs485x4_stm32h743vit6.yaml` | 板卡描述元数据 |
| `Kconfig.*` | 板卡 Kconfig 入口 |
| `board.cmake` | flash runner 配置 |

## 三、控制台串口

DTS 中将 Zephyr console 和 Shell 都指向 USART1：

```dts
chosen {
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

串口连接：

| 信号 | MCU 引脚 |
| --- | --- |
| USART1_TX | PA9 |
| USART1_RX | PA10 |

## 四、RMII 以太网

RMII 引脚配置如下：

| 信号 | MCU 引脚 | DTS pinctrl |
| --- | --- | --- |
| RMII_REF_CLK | PA1 | `eth_ref_clk_pa1` |
| RMII_CRS_DV | PA7 | `eth_crs_dv_pa7` |
| RMII_RXD0 | PC4 | `eth_rxd0_pc4` |
| RMII_RXD1 | PC5 | `eth_rxd1_pc5` |
| RMII_TX_EN | PB11 | `eth_tx_en_pb11` |
| RMII_TXD0 | PB12 | `eth_txd0_pb12` |
| RMII_TXD1 | PB13 | `eth_txd1_pb13` |
| ETH_MDIO | PA2 | `eth_mdio_pa2` |
| ETH_MDC | PC1 | `eth_mdc_pc1` |
| ETH_RESET | PC0 | `reset-gpios = <&gpioc 0 GPIO_ACTIVE_LOW>` |

当前 MAC/PHY 配置：

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

&mdio {
	pinctrl-0 = <&eth_mdio_pa2 &eth_mdc_pc1>;
	pinctrl-names = "default";
	status = "okay";

	eth_phy: ethernet-phy@0 {
		compatible = "ethernet-phy";
		reg = <0x00>;
		reset-gpios = <&gpioc 0 GPIO_ACTIVE_LOW>;
		reset-assert-duration-us = <10000>;
		reset-deassertion-timeout-ms = <100>;
	};
};
```

注意：PA1 的 `RMII_REF_CLK` 通常需要外部 PHY 输出 50 MHz 参考时钟给 MCU。

## 五、Modbus RTU

UART7 被配置为 Modbus RTU 串口：

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

串口连接：

| 信号 | MCU 引脚 |
| --- | --- |
| UART7_TX | PE8 |
| UART7_RX | PE7 |

如果硬件上的 RS-485 收发器需要 MCU 控制 DE/nRE 方向脚，需要在 `modbus0` 节点中继续补充方向控制 GPIO。

## 六、编译和烧录

编译新板：

```powershell
.\build.ps1 -Board mp_rs485x4_stm32h743vit6
```

烧录新板：

```powershell
.\flash.ps1 -Board mp_rs485x4_stm32h743vit6
```

手动 west 命令：

```powershell
python -m west build -b mp_rs485x4_stm32h743vit6 . -d build\mp_rs485x4_stm32h743vit6
python -m west flash -d build\mp_rs485x4_stm32h743vit6 --runner stm32cubeprogrammer
```

## 七、验证

启动后 USART1 日志应显示当前板卡 ID：

```text
craner_encoder_hub started on mp_rs485x4_stm32h743vit6
```

Shell 中可以使用：

```text
net iface
net ipv4
net stats
```

以太网正常时应看到 PHY ID、link speed 和静态 IPv4 地址：

```text
Configured static Ethernet IPv4 on iface index 1
Ethernet IPv4 address: 192.168.18.32
Ethernet IPv4 netmask: 255.255.255.0
```

Modbus RTU 应使用 UART7 对应的 `modbus0` 节点。

## 八、常见问题

| 现象 | 检查项 |
| --- | --- |
| USART1 无日志 | PA9/PA10 是否接反，串口参数是否 115200 8N1 |
| PHY 找不到 | MDIO/MDC、PHY 地址 `0x00`、PC0 reset 极性 |
| Link 不起来 | PHY 供电、网线、交换机、PA1 是否有 50 MHz RMII_REF_CLK |
| IP 不通 | PC 是否在 `192.168.18.0/24` 网段，是否存在 `192.168.18.32` 地址冲突 |
| Modbus 无响应 | UART7 PE8/PE7 是否接到 RS-485 收发器，A/B 线和从站地址是否正确 |
