# Zephyr 以太网 RMII 功能实现说明

本文说明本项目如何在 `craner_general_stm32h743vit6` 上启用 STM32H743 以太网 MAC，使用 RMII 模式连接外部 PHY，并配置静态 IPv4 地址。

当前实现：

| 项目 | 配置 |
| --- | --- |
| Zephyr 板卡 ID | `craner_general_stm32h743vit6` |
| PCB 硬件版本 | `1.1.0` |
| MCU | STM32H743VIT6 |
| MAC | STM32 内置 Ethernet MAC |
| PHY 接口 | RMII |
| PHY 地址 | `0x00` |
| PHY 复位 | PC0，低有效 |
| MAC 地址 | `02:00:00:00:01:32` |
| IP 配置 | 静态 IPv4：`192.168.18.32/24` |
| 调试入口 | UART5 Shell，`net` 命令 |

## 一、RMII 引脚

板级 DTS 文件：

```text
boards/craner/craner_general_stm32h743vit6/craner_general_stm32h743vit6.dts
```

当前 RMII 引脚配置如下：

| 信号 | 引脚 | DTS pinctrl |
| --- | --- | --- |
| RMII_REF_CLK | PA1 | `eth_ref_clk_pa1` |
| ETH_MDIO | PA2 | `eth_mdio_pa2` |
| RMII_CRS_DV | PA7 | `eth_crs_dv_pa7` |
| RMII_TX_EN | PB11 | `eth_tx_en_pb11` |
| RMII_TXD0 | PB12 | `eth_txd0_pb12` |
| RMII_TXD1 | PB13 | `eth_txd1_pb13` |
| RMII_RXD0 | PC4 | `eth_rxd0_pc4` |
| RMII_RXD1 | PC5 | `eth_rxd1_pc5` |
| ETH_MDC | PC1 | `eth_mdc_pc1` |
| ETH_nRST | PC0 | `reset-gpios = <&gpioc 0 GPIO_ACTIVE_LOW>` |

## 二、MAC 和 MDIO 配置

以太网 MAC 配置：

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
	local-mac-address = [02 00 00 00 01 32];
	phy-connection-type = "rmii";
	phy-handle = <&eth_phy>;
	status = "okay";
};
```

MDIO 和 PHY 配置：

```dts
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

`reg = <0x00>` 是 PHY 的 MDIO 地址。如果硬件通过 strap 电阻把 PHY 地址设成其他值，需要同步修改这里。

## 三、项目配置

项目配置文件：

```text
prj.conf
```

新增关键配置：

```conf
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_ETH_STM32_HAL=y
CONFIG_PHY_GENERIC_MII=y
CONFIG_NET_IPV4=y
CONFIG_NET_ARP=y
CONFIG_NET_UDP=y
CONFIG_NET_DHCPV4=n
CONFIG_NET_MGMT=y
CONFIG_NET_MGMT_EVENT=y
CONFIG_NET_SHELL=y
CONFIG_ENTROPY_GENERATOR=y
```

这些配置启用 Zephyr 网络栈、STM32 HAL 以太网驱动、通用 MII PHY 驱动、IPv4、网络 Shell 命令和硬件熵源，并明确关闭 DHCPv4。网络栈需要随机数能力，当前通过 STM32 RNG 提供。

板级 DTS 同时启用了 HSI48 和 RNG：

```dts
&clk_hsi48 {
	status = "okay";
};

&rng {
	status = "okay";
};
```

## 四、应用模块

源码文件：

```text
src/ethernet_app.c
```

该模块做两件事：

1. 注册网络事件回调，打印 interface up/down 状态。
2. 启动后对默认网络接口配置静态地址 `192.168.18.32/24`。

串口日志中会看到类似输出：

```text
Configured static Ethernet IPv4 on iface index 1
Ethernet MAC address: 02:00:00:00:01:32
Ethernet interface is up
Ethernet IPv4 address: 192.168.18.32
Ethernet IPv4 netmask: 255.255.255.0
Ethernet IPv4 gateway: 0.0.0.0
```

## 五、编译、烧录和验证

编译：

```powershell
.\build.ps1
```

烧录：

```powershell
.\flash.ps1
```

打开 UART5 Shell 后，可以使用 Zephyr 内置网络命令：

```text
net iface
net ipv4
net stats
```

如果静态 IP 配置正常，`net iface` 中应能看到以太网接口和 `192.168.18.32`。

## 六、硬件注意事项

### 1. RMII_REF_CLK

PA1 是 RMII_REF_CLK，通常需要外部 PHY 输出 50 MHz RMII 参考时钟给 MCU。请确认 PHY 已上电且时钟输出模式正确。

### 2. PHY 地址

当前假设 PHY 地址为 `0x00`。如果 Zephyr 日志中出现 PHY 找不到、MDIO 读失败、链路不起等现象，优先确认 PHY strap 地址。

### 3. PHY 复位

PC0 被配置为 PHY 低有效复位脚。启动时 Zephyr PHY 驱动会拉低再释放该引脚：

```dts
reset-gpios = <&gpioc 0 GPIO_ACTIVE_LOW>;
```

如果硬件复位极性不同，需要调整 `GPIO_ACTIVE_LOW`。

### 4. 网络不通

按顺序检查：

| 检查项 | 说明 |
| --- | --- |
| PHY 供电 | PHY 是否有稳定电源 |
| REF_CLK | PA1 是否有 50 MHz RMII clock |
| MDIO/MDC | PA2/PC1 是否连接到 PHY |
| PHY 地址 | DTS 中 `reg` 是否匹配硬件 strap |
| 网线和交换机 | link LED 是否亮 |
| IP 冲突 | 局域网中是否已有设备使用 `192.168.18.32` |
