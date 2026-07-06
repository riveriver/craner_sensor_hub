# Zephyr Ethernet RMII 示例：静态 IPv4 网络

## 1. 示例实现了什么

本示例在 STM32H743VIT6 上启用片上 Ethernet MAC，通过 RMII 连接外部 PHY，并使用 Zephyr `CONFIG_NET_CONFIG_*` 自动配置静态 IPv4 地址。

当前网络参数：

| 项目 | 值 |
| --- | --- |
| 接口模式 | RMII |
| IPv4 地址 | `192.168.18.32` |
| Netmask | `255.255.255.0` |
| Gateway | `192.168.18.1` |
| DHCP | 关闭 |
| PHY 地址 | `0x00` |
| PHY 复位脚 | PC0，低有效 |

两块板都启用了同一套 RMII 信号：

| 信号 | 引脚 |
| --- | --- |
| RMII_REF_CLK | PA1 |
| ETH_MDIO | PA2 |
| RMII_CRS_DV | PA7 |
| RMII_TX_EN | PB11 |
| RMII_TXD0 | PB12 |
| RMII_TXD1 | PB13 |
| RMII_RXD0 | PC4 |
| RMII_RXD1 | PC5 |
| ETH_MDC | PC1 |
| ETH_RESET | PC0 |

## 2. 怎么使用

编译：

```powershell
.\build.ps1
```

烧录后接入同一局域网，PC 设置到 `192.168.18.x/24` 网段，然后测试：

```powershell
ping 192.168.18.32
```

Shell 中可以查看接口：

```text
net iface
net ipv4
net stats
```

如果 Modbus TCP server 也已运行，可以连接：

```text
192.168.18.32:502
```

## 3. 前置条件

需要：

| 项目 | 说明 |
| --- | --- |
| 外部 PHY | 支持 RMII，MDIO 地址当前假设为 `0x00` |
| RMII 时钟 | PA1 需要 50 MHz RMII reference clock |
| 网线/交换机 | PHY link LED 应点亮 |
| 网络规划 | PC 和 MCU 在 `192.168.18.0/24` |
| IP 冲突 | 确认局域网中没有其他设备使用 `192.168.18.32` |

## 4. 设备树：硬件描述

Ethernet MAC 节点描述 MCU 侧 RMII 信号：

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

关键点：

| DTS 项 | 作用 |
| --- | --- |
| `pinctrl-0` | 描述 RMII 数据和控制信号引脚 |
| `local-mac-address` | 设置本机 MAC 地址 |
| `phy-connection-type = "rmii"` | 告诉驱动使用 RMII |
| `phy-handle = <&eth_phy>` | 关联 MDIO 总线上的 PHY |
| `status = "okay"` | 启用 MAC |

MDIO 节点描述 PHY 管理接口：

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

`reg = <0x00>` 是 PHY 的 MDIO 地址。如果硬件 strap 设置成其他地址，需要同步修改。

`reset-gpios` 表示 PC0 控制 PHY reset，低电平有效。

## 5. Kconfig/prj.conf：软件配置

网络和驱动配置：

```conf
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_ETH_STM32_HAL=y
CONFIG_PHY_GENERIC_MII=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=n
CONFIG_NET_ARP=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_DHCPV4=n
```

静态 IPv4 使用 Zephyr net_config：

```conf
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_NEED_IPV4=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.18.32"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
CONFIG_NET_CONFIG_MY_IPV4_GW="192.168.18.1"
```

含义：

| 配置 | 作用 |
| --- | --- |
| `CONFIG_NET_CONFIG_SETTINGS=y` | 让 Zephyr 启动时自动配置网络 |
| `CONFIG_NET_CONFIG_NEED_IPV4=y` | 声明应用需要 IPv4 |
| `CONFIG_NET_CONFIG_MY_IPV4_ADDR` | MCU 自己的 IP 地址 |
| `CONFIG_NET_CONFIG_MY_IPV4_NETMASK` | 子网掩码 |
| `CONFIG_NET_CONFIG_MY_IPV4_GW` | 默认网关 |

当前工程不再使用单独的 `ethernet_app.c` 手动添加 IP。

## 6. 业务/应用代码

以太网基础能力主要由 DTS 和 Kconfig 完成。业务代码不需要手动调用 `net_if_ipv4_addr_add()`。

当前与网络相关的业务模块是 `src/modbus_tcp_server_app.c`，它直接监听：

```c
bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
bind_addr.sin_port = htons(502);
```

`INADDR_ANY` 表示监听所有本机 IPv4 地址，其中就包括 net_config 配置出的 `192.168.18.32`。

## 7. 如何扩展

常见修改：

| 需求 | 修改位置 |
| --- | --- |
| 改 IP | `CONFIG_NET_CONFIG_MY_IPV4_ADDR` |
| 改网关 | `CONFIG_NET_CONFIG_MY_IPV4_GW` |
| 改 PHY 地址 | DTS 中 `ethernet-phy@0` 和 `reg` |
| 改 MAC 地址 | DTS 中 `local-mac-address` |
| 改复位脚 | DTS 中 `reset-gpios` |
| 使用 DHCP | 打开 `CONFIG_NET_DHCPV4=y`，移除或清空静态地址配置 |

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| PHY ID 能读到但 ping 不通 | PC 是否在 `192.168.18.0/24`，是否 IP 冲突 |
| 没有 link up | 网线、交换机、PHY 供电、PA1 50 MHz REF_CLK |
| 找不到 PHY | MDIO/MDC 引脚、PHY strap 地址、`reg = <0x00>` |
| IP 不是预期值 | 检查 `.config` 中 `CONFIG_NET_CONFIG_MY_IPV4_ADDR` |
| 502 端口连不上 | 确认 Modbus TCP server 线程已启动 |

生成文件检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\zephyr.dts -Pattern "phy-connection-type|local-mac-address|ethernet-phy|reset-gpios"
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_NET_CONFIG_MY_IPV4|CONFIG_NET_TCP|CONFIG_NET_SOCKETS"
```
