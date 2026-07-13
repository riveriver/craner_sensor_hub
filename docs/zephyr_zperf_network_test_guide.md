# Zephyr zperf 最小网络性能测试

本文用于验证当前板子的以太网 UDP/TCP 基础通信能力，先排除业务线程、syslog、Modbus、OTA 等因素，只保留最小网络压测环境。

## 1. 当前测试固件实现了什么

当前 `prj.conf` 打开：

- 串口 console / shell
- 静态 IPv4：`192.168.18.32/24`
- 以太网 RMII
- IPv4、ARP、UDP、TCP、socket
- Zephyr `zperf` shell
- Zephyr `net` shell 和网络统计

当前 `prj.conf` 关闭：

- 业务健康线程
- 编码器 Modbus RTU 读取线程
- Modbus TCP server
- OTA control / mcumgr UDP
- Telnet shell
- syslog 网络日志后端
- 硬件看门狗
- Zephyr Modbus 子系统

这样可以把网络负载降到最低，只测以太网驱动、IP、UDP、TCP、buffer 和 socket 路径。

当前 zperf 测试配置把 `CONFIG_NET_BUF_DATA_SIZE` 设置为 `512`。这样发送 `1K` UDP payload 时大约需要 3 个 data buffer，比 `256` 字节 buffer 的碎片数量更少。曾经在 `256` 字节 buffer 下出现过：

```text
net_pkt: Data buffer (1028) allocation failed.
net_ctx: Failed to allocate net_pkt
```

这表示 Zephyr 网络 buffer 池先耗尽，不一定是物理链路丢包。遇到这个错误时，先降低速率或 payload，再观察 `net stats`，不要直接判断为 PHY 或网线问题。

## 2. 前置条件

PC 需要安装 iPerf 2。

注意：Zephyr `zperf` 兼容的是 `iperf` 2.0.10 或更新版本，不是 `iperf3`。Windows 上命令通常是：

```powershell
iperf --version
```

如果系统提示找不到 `iperf`，需要先安装 iPerf 2，再重新打开 PowerShell 或 VS Code 终端。

## 3. 编译和烧录

```powershell
.\build.ps1 -SkipOtaImages
.\flash.ps1
```

串口 shell 启动后，先确认 IP 和链路：

```text
net iface
net stats
zperf version
zperf --help
```

## 4. 测试前清空统计

每轮测试前建议在 MCU shell 执行：

```text
net stats reset
zperf jobs clear
```

测试后执行：

```text
net stats
zperf jobs all
```

重点观察：

- `UDP sent/drop`
- `TCP sent/drop`
- `Processing err`
- `Bytes sent`
- `Bytes received`

## 5. UDP：MCU 发送，PC 接收

PC 作为 UDP server：

```powershell
iperf -s -u -p 5001
```

MCU shell 发送 UDP：

```text
zperf udp upload 192.168.18.4 5001 10 1K 1M
```

含义：

- `192.168.18.4`：PC IP
- `5001`：PC 监听端口
- `10`：测试 10 秒
- `1K`：每包 payload 大小
- `1M`：目标发送速率

可以逐步提高速率：

```text
zperf udp upload 192.168.18.4 5001 10 1K 100K
zperf udp upload 192.168.18.4 5001 10 1K 500K
zperf udp upload 192.168.18.4 5001 10 1K 1M
zperf udp upload 192.168.18.4 5001 10 1K 5M
```

## 6. UDP：PC 发送，MCU 接收

MCU shell 启动 UDP download server：

```text
zperf udp download 5001 192.168.18.32
```

PC 发送 UDP：

```powershell
iperf -c 192.168.18.32 -u -p 5001 -b 1M -t 10 -l 1024
```

停止 MCU UDP server：

```text
zperf udp download stop
```

## 7. TCP：MCU 发送，PC 接收

PC 作为 TCP server：

```powershell
iperf -s -p 5001
```

MCU shell 发送 TCP：

```text
zperf tcp upload 192.168.18.4 5001 10 1K
```

## 8. TCP：PC 发送，MCU 接收

MCU shell 启动 TCP download server：

```text
zperf tcp download 5001 192.168.18.32
```

PC 发送 TCP：

```powershell
iperf -c 192.168.18.32 -p 5001 -t 10
```

停止 MCU TCP server：

```text
zperf tcp download stop
```

## 9. Wireshark 过滤器

UDP zperf：

```text
ip.addr == 192.168.18.32 && udp.port == 5001
```

TCP zperf：

```text
ip.addr == 192.168.18.32 && tcp.port == 5001
```

ARP：

```text
arp && eth.addr == 02:00:00:00:01:32
```

## 10. 判断方法

如果 UDP/TCP zperf 都稳定，说明基础以太网链路和 socket 通信基本正常，之前 syslog UDP 丢包更可能和日志后端、net buffer 使用方式、发送节奏或接收端解析有关。

如果 UDP zperf 也大量丢包，但 TCP 正常，需要重点检查：

- STM32 ETH DMA descriptor / buffer 数量
- `NET_PKT` / `NET_BUF` 数量
- RX 方向是否有大量 `Processing err`
- PC、交换机、网线和双工协商
- Wireshark 是否抓在正确网卡

如果 TCP 也异常，需要先回到链路层排查：

- `phy_mii` 是否稳定显示 100M full duplex
- 是否存在重复 IP
- ARP 是否正常
- PC 防火墙是否拦截
- `net stats` 是否持续增加 drop / processing err
