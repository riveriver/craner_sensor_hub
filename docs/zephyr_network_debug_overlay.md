# Zephyr 网络调试配置 debug_net.conf

## 这个配置做什么

`debug_net.conf` 是网络排障专用 overlay 配置，不改变默认 `prj.conf` 的使用方式。

它主要用于排查：

1. Telnet shell 端口是否监听。
2. Modbus TCP 端口是否监听。
3. UDP mcumgr 是否正常。
4. 网络 context、connection、packet、buffer 是否偏紧。
5. TCP worker 或 socket service 栈是否不够。

## 如何启用

使用 `west build` 时追加 `EXTRA_CONF_FILE`：

```powershell
west build -b craner_general_stm32h743vit6 . -d build\debug_net -- -DEXTRA_CONF_FILE=debug_net.conf
```

如果继续使用项目脚本，默认构建仍使用 `prj.conf`。需要调试网络时，再单独使用上面的命令构建调试版本。

## 配置内容

```conf
CONFIG_NET_SHELL=y
CONFIG_NET_STATISTICS=y
CONFIG_NET_STATISTICS_ETHERNET=y

CONFIG_NET_MAX_CONN=16
CONFIG_NET_MAX_CONTEXTS=16
CONFIG_NET_PKT_RX_COUNT=32
CONFIG_NET_PKT_TX_COUNT=32
CONFIG_NET_BUF_RX_COUNT=64
CONFIG_NET_BUF_TX_COUNT=64

CONFIG_NET_TCP_WORKQ_STACK_SIZE=2048
CONFIG_NET_SOCKETS_SERVICE_STACK_SIZE=2048
```

这些配置给 TCP listener、UDP socket、syslog、mcumgr、Telnet、Modbus TCP 留出更大的资源余量。

## 常用 shell 命令

查看接口：

```text
net iface
```

查看连接和监听：

```text
net conn
```

正常情况下应该能看到类似：

```text
0.0.0.0:23   LISTEN
0.0.0.0:502  LISTEN
0.0.0.0:1337 UDP
```

查看网络统计：

```text
net stats
```

如果需要按接口查看，先用 `net iface` 找到接口编号，再执行：

```text
net stats 1
```

## 什么时候打开更详细日志

`debug_net.conf` 里预留了这两行：

```conf
# CONFIG_NET_TCP_LOG_LEVEL_DBG=y
# CONFIG_NET_CONTEXT_LOG_LEVEL_DBG=y
```

只有在下面这些情况才建议打开：

1. `net conn` 看不到应该存在的 listener。
2. PC 侧 TCP 连接被拒绝。
3. TCP 能连接但很快断开。
4. 怀疑 socket/context 分配失败。

打开后日志会明显变多，不建议长期放在默认固件里。

## PC 侧验证命令

```powershell
ping 192.168.18.32
Test-NetConnection 192.168.18.32 -Port 23
Test-NetConnection 192.168.18.32 -Port 502
mcumgr --conntype udp --connstring 192.168.18.32:1337 echo hello
```

判断：

| 现象 | 含义 |
| --- | --- |
| ping 通，TCP 23/502 不通 | MCU IP 正常，TCP listener 或 TCP 资源异常 |
| ping 不通 | 先查 PHY、IP、网关、网线 |
| UDP mcumgr 通，TCP 不通 | 网络链路正常，重点查 TCP 资源和 listener |
| `net conn` 里有 LISTEN，PC 仍连不上 | 查防火墙、路由、TCP debug 日志 |

## 注意

默认 `prj.conf` 仍然是正式配置入口。`debug_net.conf` 是调试辅助配置，用来快速放大网络资源和打开网络 shell/statistics，方便复现和定位问题。
