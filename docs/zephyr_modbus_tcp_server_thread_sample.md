# Zephyr Modbus TCP Server 示例：官方 sample 的线程版

## 1. 示例实现了什么

本示例实现ModbusTCP server

当前参数：

| 项目 | 值 |
| --- | --- |
| IP | `192.168.18.32` |
| TCP 端口 | `502` |
| Modbus Unit ID | `1` |
| Raw backend | `RAW_0` |
| 线程栈 | `3072` bytes |
| 线程优先级 | `8` |
| Holding Register | 地址 `0` 到 `7`，由 `modbus_register_service` 统一管理 |
| Input Register | 地址 `0` 到 `5`，由 RTU 线程更新，TCP client 只读 |
| Coil | 地址 `0` 到 `7`，由 `modbus_register_service` 统一管理 |
| Custom FC | `101` |


## 2. 怎么使用

编译：

```powershell
.\build.ps1
```

烧录后，串口日志应看到：

```text
Started MODBUS TCP server example on port 502
```

PC 侧使用 Modbus TCP client：

| 参数 | 值 |
| --- | --- |
| IP | `192.168.18.32` |
| Port | `502` |
| Unit ID | `1` |

测试步骤：

1. 用 FC06 写 Holding Register，例如地址 `0` 写入 `0x1234`。
2. 用 FC03 读取 Holding Register 地址 `0`，数量 `1`。
3. 应读回 `0x1234`。

也可以测试：

| 功能码 | 作用 |
| --- | --- |
| FC01 | 读 Coil |
| FC03 | 读 Holding Register |
| FC04 | 读 Input Register |
| FC05 | 写单个 Coil |
| FC06 | 写单个 Holding Register |
| FC15 | 写多个 Coil |
| FC16 | 写多个 Holding Register |
| FC101 | 官方示例中的 custom function |

## 3. 前置条件

需要：

| 项目 | 说明 |
| --- | --- |
| Ethernet | RMII PHY link 正常 |
| IP 网段 | PC 和 MCU 在 `192.168.18.0/24` |
| 防火墙 | PC 工具允许访问 TCP 502 |
| Kconfig | 启用 Modbus raw、TCP socket、Zephyr zsock API |
| 端口权限 | 设备端监听 502，不需要 PC 本地监听 |

先确认网络可达：

```powershell
ping 192.168.18.32
```

## 4. 设备树：硬件描述

Modbus TCP 本身不需要 Modbus 串口节点，它依赖以太网 DTS。

MAC 节点：

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

PHY 节点：

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

这些节点让 Zephyr 创建以太网接口。Modbus TCP server 后续通过 socket 绑定到该接口的 IPv4 地址。

## 5. Kconfig/prj.conf：软件配置

网络 socket：

```conf
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
```

Modbus raw server：

```conf
CONFIG_MODBUS=y
CONFIG_MODBUS_ROLE_CLIENT_SERVER=y
CONFIG_MODBUS_RAW_ADU=y
CONFIG_MODBUS_NUMOF_RAW_ADU=1
```

静态 IP：

```conf
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_NEED_IPV4=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.18.32"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
CONFIG_NET_CONFIG_MY_IPV4_GW="192.168.18.1"
```

当前实现使用 Zephyr 原生 `zsock_*` socket API，不需要 `CONFIG_POSIX_API`。

## 6. 业务/应用代码

线程入口：

```c
K_THREAD_DEFINE(modbus_tcp_server_tid, MODBUS_TCP_SERVER_STACK_SIZE,
		modbus_tcp_server_thread, NULL, NULL, NULL,
		MODBUS_TCP_SERVER_PRIORITY, 0, 0);
```

初始化 Modbus raw server：

```c
server_iface = modbus_iface_get_by_name("RAW_0");
modbus_init_server(server_iface, server_param);
modbus_register_user_fc(server_iface, &modbus_cfg_custom);
```

TCP server 流程：

```c
serv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
zsock_bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
zsock_listen(serv, 5);
client = zsock_accept(serv, (struct sockaddr *)&client_addr, &client_addr_len);
```

收到 TCP 数据后：

1. `zsock_recv()` 读取 MBAP header。
2. `modbus_raw_get_header()` 转成 Zephyr Modbus ADU。
3. 再 `zsock_recv()` 读取 PDU 数据。
4. `modbus_raw_submit_rx()` 交给 Modbus core。
5. raw callback 收到响应并释放信号量。
6. `zsock_send()` 把 MBAP header 和响应数据发回 client。

## 7. 如何扩展

| 需求 | 修改位置 |
| --- | --- |
| 改端口 | `MODBUS_TCP_PORT` |
| 改 Unit ID | `server_param.server.unit_id` |
| 增加寄存器数量 | `src/modbus_register_map.c` 的对应寄存器表 |
| 增加 coil 数量 | `src/modbus_register_map.c` 的 `coil_table[]` |
| 修改寄存器读写规则 | `src/modbus_register_service.c` |
| 去掉 custom FC101 | 移除 `MODBUS_CUSTOM_FC_DEFINE` 和注册调用 |
| 使用 Zephyr 原生 zsock API | 当前已使用 `zsock_*`，不需要 `CONFIG_POSIX_API` |

## 8. 常见问题排查

| 现象 | 检查项 |
| --- | --- |
| 没有启动日志 | `src/modbus_tcp_server_app.c` 是否加入 `CMakeLists.txt` |
| `RAW_0` 找不到 | `CONFIG_MODBUS_RAW_ADU=y` 和 `CONFIG_MODBUS_NUMOF_RAW_ADU=1` |
| ping 通但连不上 502 | 是否看到 `Started MODBUS TCP server example on port 502` |
| 读寄存器异常 | 地址是否在 `src/modbus_register_map.c` 定义的范围内，Unit ID 是否为 `1` |
| 编译 socket 报错 | 检查 `CONFIG_NET_SOCKETS=y` |

生成配置检查：

```powershell
Select-String build\mp_rs485x4_stm32h743vit6\zephyr\.config -Pattern "CONFIG_MODBUS_RAW_ADU|CONFIG_NET_TCP|CONFIG_NET_SOCKETS"
```
