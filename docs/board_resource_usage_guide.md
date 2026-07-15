# 板卡资源与固件资源使用说明

本文面向嵌入式新人，说明 `craner_general_stm32h743vit6` 板卡有哪些硬件资源，当前固件用了哪些资源，用在哪里，以及 Flash、RAM、线程栈、网络缓冲区这些软件资源该怎么理解和调整。

说明：你提到的 `RAW`，这里按 `RAM` 理解；工程里也有 Modbus `RAW ADU`，它属于 Modbus TCP Server 内部处理方式，不是硬件内存资源。

## 1. 板卡基本资源

| 资源 | 当前工程配置 | 用在哪里 |
| --- | --- | --- |
| MCU | STM32H743VIT6 | 主控芯片 |
| 主频 | 240 MHz | DTS 中通过 HSE 25 MHz + PLL 配置 |
| 外部晶振 | 25 MHz HSE | 系统主时钟来源 |
| Flash | 2 MiB 内部 Flash | MCUboot、主程序、OTA 备份镜像、scratch、app storage |
| 主 RAM | SRAM0 512 KiB | Zephyr 内核、线程栈、网络缓冲区、应用全局变量 |
| DTCM | 128 KiB | 当前未使用 |
| ITCM | 64 KiB | 当前未使用 |
| SRAM1 | 128 KiB | 当前未使用 |
| SRAM2 | 128 KiB | 当前未使用 |
| SRAM3 | 32 KiB | 以太网驱动使用 16 KiB |
| SRAM4 | 64 KiB | 当前未使用 |
| UART5 | PB6 TX / PB5 RX，115200 | 串口控制台、串口 Shell、`printk` |
| UART7 | PE8 TX / PE7 RX，9600 | 回转编码器 Modbus RTU |
| UART8 | PE1 TX / PE0 RX，9600 | 变幅编码器 Modbus RTU |
| UART4 | PD1 TX / PD0 RX，9600 | 起升编码器 Modbus RTU |
| Ethernet RMII | PA1/PA2/PA7/PB11/PB12/PB13/PC1/PC4/PC5/PC0 | Modbus TCP、Telnet Shell、MQTT、mcumgr UDP OTA |
| Watchdog | IWDG | 系统健康线程喂狗 |
| RNG | STM32 RNG | 网络/MQTT 随机数、协议栈熵源 |
| GPIO 电源控制 | PE6、PC13、PB3 | 3.3V/CCTV、5V、网络桥接电源 |
| 状态 LED | PD10 | 系统健康状态显示 |

## 2. Flash 资源

### 2.1 Flash 分区

当前 DTS 把 2 MiB 内部 Flash 分成 5 个区：

| 分区 | 起始地址 | 大小 | 用途 |
| --- | --- | --- | --- |
| `mcuboot` | `0x08000000` | 128 KiB | MCUboot bootloader |
| `image-0` | `0x08020000` | 768 KiB | 当前运行的应用镜像 |
| `image-1` | `0x080E0000` | 768 KiB | OTA 下载的新应用镜像 |
| `image-scratch` | `0x081A0000` | 128 KiB | MCUboot swap scratch 交换区 |
| `app-storage` | `0x081C0000` | 256 KiB | 应用存储预留 |

### 2.2 当前使用量

当前构建产物：

| 镜像 | 当前大小 | 所在分区 | 分区上限 | 当前结论 |
| --- | ---: | --- | ---: | --- |
| MCUboot `zephyr.bin` | 32,356 B | `mcuboot` | 131,072 B | 很充足 |
| 应用 `zephyr.signed.bin` | 237,616 B | `image-0` / `image-1` | 786,432 B | 很充足 |
| 应用 confirmed 镜像 | 786,432 B | `image-0` | 786,432 B | 这是补齐到 slot 大小后的烧录镜像 |

新人容易混淆的一点：`zephyr.signed.confirmed.bin` 等于 768 KiB，不代表程序真的用了 768 KiB，它是为了首烧 confirmed image 而补齐到整个 slot。看真实程序大小，要看 `zephyr.signed.bin` 或 build 输出里的 FLASH 使用量。

### 2.3 Flash 最大和最优边界

单个应用镜像最大不能超过 `image-0` 或 `image-1`，也就是 768 KiB。考虑 MCUboot header、trailer、签名信息和未来增长，建议不要把应用做满。

推荐边界：

| 场景 | 建议应用镜像大小 |
| --- | ---: |
| 当前最优目标 | 小于 400 KiB |
| 首发稳定目标 | 小于 512 KiB |
| 警戒线 | 大于 650 KiB 需要评估 |
| 硬上限 | 不能超过 768 KiB slot |

当前应用约 232 KiB，Flash 余量非常充足。

## 3. RAM 资源

### 3.1 当前链接内存区域

从当前 `zephyr.map` 看，应用主要使用：

| RAM 区域 | 总量 | 当前使用 | 当前用途 |
| --- | ---: | ---: | --- |
| SRAM0 / RAM | 512 KiB | 约 202 KiB | 内核、线程栈、网络缓冲区、应用全局变量 |
| SRAM3 | 32 KiB | 16 KiB | STM32 以太网驱动 DMA 区 |
| ITCM | 64 KiB | 0 | 当前未使用 |
| DTCM | 128 KiB | 0 | 当前未使用 |
| SRAM1 | 128 KiB | 0 | 当前未使用 |
| SRAM2 | 128 KiB | 0 | 当前未使用 |
| SRAM4 | 64 KiB | 0 | 当前未使用 |

当前应用 ELF 段统计：

```text
text = 227,560 B
data =   9,896 B
bss  = 212,507 B
```

其中 `text` 主要进入 Flash，`data` 和 `bss` 会占 RAM。`bss` 大，通常不是坏事，因为网络缓冲区、线程栈、协议栈池都属于这类静态 RAM。

### 3.2 RAM 最大和最优边界

理论上 STM32H743VIT6 有多块 RAM，但当前 Zephyr 链接配置主要把应用 RAM 放在 SRAM0 512 KiB，另有以太网占用 SRAM3 16 KiB。不要简单把所有 RAM 加起来当作可直接使用的连续内存。

推荐边界：

| 场景 | SRAM0 使用目标 |
| --- | ---: |
| 当前最优目标 | 小于 300 KiB |
| 首发稳定目标 | 小于 350 KiB |
| 压力测试上限 | 小于 400 KiB |
| 警戒线 | 大于 430 KiB 需要重看栈和缓冲区 |
| 硬边界 | 不能超过 512 KiB SRAM0 |

当前 SRAM0 约 202 KiB，离稳定边界还有较大空间。

## 4. 线程栈资源

线程栈就是每个线程运行函数调用、局部变量、中断上下文时需要的内存。栈太小会溢出，栈太大会浪费 RAM。

当前关键栈配置：

| 配置 | 当前值 | 用在哪里 | 当前评价 |
| --- | ---: | --- | --- |
| `CONFIG_MAIN_STACK_SIZE` | 1024 B | `main()` 线程 | `main()` 很薄，够用 |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | 4096 B | 系统工作队列 | 偏稳 |
| `CONFIG_SHELL_STACK_SIZE` | 4096 B | 串口/Telnet Shell | 合理 |
| `CONFIG_NET_TX_STACK_SIZE` | 4096 B | 网络 TX 线程 | 偏稳 |
| `CONFIG_NET_RX_STACK_SIZE` | 4096 B | 网络 RX 线程 | 偏稳 |
| `CONFIG_NET_TCP_WORKQ_STACK_SIZE` | 4096 B | TCP 工作队列 | 偏稳 |
| `CONFIG_NET_MGMT_EVENT_STACK_SIZE` | 4096 B | 网络管理事件线程 | 偏稳 |
| `CONFIG_CRANER_MQTT_SERVICE_MANAGER_STACK_SIZE` | 4096 B | MQTT 连接管理线程 | 合理 |
| Modbus TCP Server 栈 | 3072 B | `modbus_tcp_server_app.c` | 合理 |
| 单路 RTU 编码器栈 | 2048 B | 3 路编码器线程 | 合理 |
| 系统健康线程栈 | 1536 B | LED、看门狗、健康检测 | 合理 |

### 4.1 栈的最大和最优设置

Kconfig 对很多 stack size 没有硬性最大值，你可以写 8192、16384，甚至更高；真正限制是 RAM。嵌入式里不建议盲目拉大栈，应该先打开栈统计或线程分析，看实际高水位。

建议值：

| 场景 | 网络相关栈 | Shell 栈 | MQTT 栈 | 说明 |
| --- | ---: | ---: | ---: | --- |
| 最小可跑 | 2048 B | 2048 B | 3072 B | 功能少、日志少时可尝试 |
| 当前最优 | 4096 B | 4096 B | 4096 B | 当前工程推荐 |
| 压力测试 | 4096-6144 B | 4096-6144 B | 4096-6144 B | 大量日志、复杂命令时 |
| 不建议默认超过 | 8192 B | 8192 B | 8192 B | 超过通常应先找原因 |

对当前工程来说，`4096 B` 是比较舒服的工程值：浪费不大，又能覆盖 Shell、网络、MQTT、日志这些调用链。

## 5. 网络资源

网络资源不是一个东西，而是一组池：

| 资源 | 作用 |
| --- | --- |
| `NET_MAX_CONTEXTS` | 网络 context 数，基本可理解成 socket/连接对象池 |
| `NET_MAX_CONN` | 连接匹配表，TCP/UDP 都会用 |
| `ZVFS_POLL_MAX` | 一次 `poll()` 最多能监控多少个 fd |
| `NET_PKT_RX_COUNT` | 同时挂起的接收 packet 数 |
| `NET_PKT_TX_COUNT` | 同时挂起的发送 packet 数 |
| `NET_BUF_RX_COUNT` | 接收数据 buffer 数 |
| `NET_BUF_TX_COUNT` | 发送数据 buffer 数 |
| `NET_BUF_DATA_SIZE` | 每个网络数据 buffer 的大小 |

当前配置：

| 配置 | 当前值 |
| --- | ---: |
| `CONFIG_NET_MAX_CONTEXTS` | 16 |
| `CONFIG_NET_MAX_CONN` | 16 |
| `CONFIG_ZVFS_POLL_MAX` | 8 |
| `CONFIG_NET_PKT_RX_COUNT` | 16 |
| `CONFIG_NET_PKT_TX_COUNT` | 16 |
| `CONFIG_NET_BUF_RX_COUNT` | 32 |
| `CONFIG_NET_BUF_TX_COUNT` | 32 |
| `CONFIG_NET_BUF_DATA_SIZE` | 1536 B |

### 5.1 谁会用网络资源

当前固件里会使用网络资源的功能：

| 功能 | 资源估算 | 说明 |
| --- | ---: | --- |
| Modbus TCP Server 监听 socket | 1 | 监听 TCP 502 |
| Modbus TCP Client socket | 最多 4 | `CONFIG_CRANER_MODBUS_TCP_MAX_CLIENTS=4` |
| Telnet Shell | 约 2 | 一个监听 socket + 一个客户端 socket |
| MQTT | 1 | 连接 MQTT broker |
| mcumgr UDP OTA | 1 | UDP 1337 |
| 临时关闭/TIME_WAIT/重连余量 | 2-4 | TCP 关闭和重连过程可能暂占 |

按最大同时使用估算：

```text
Modbus TCP: 1 listen + 4 clients = 5
Telnet Shell: 1 listen + 1 client = 2
MQTT: 1
mcumgr UDP: 1
临时余量: 2 到 4
合计压力估算: 11 到 13
当前配置: 16
```

所以当前 `NET_MAX_CONTEXTS=16`、`NET_MAX_CONN=16` 对“4 个 Modbus TCP 客户端 + Telnet + MQTT + mcumgr UDP”是够的。

### 5.2 `ZVFS_POLL_MAX` 是否够

当前 Modbus TCP Server 使用固定客户端池：

```text
1 个 server listen fd + 4 个 client fd = 5 个 poll fd
```

`CONFIG_ZVFS_POLL_MAX=8`，所以 Modbus TCP 自己够用，并且还有 3 个余量。注意：`ZVFS_POLL_MAX` 是单次 `poll()` 能监控的 fd 数，不是全系统 socket 总数。

### 5.3 网络 buffer 的 RAM 成本

当前固定数据 buffer：

```text
NET_BUF_DATA_SIZE = 1536 B
RX buffer = 32 个
TX buffer = 32 个
```

只算数据区，大约占：

```text
(32 + 32) * 1536 = 98,304 B
```

还没算 `net_buf` 头、`net_pkt` 结构、TCP 控制块等额外开销。所以网络 buffer 是当前 RAM 的大头之一。

### 5.4 网络资源最大和最优设置

这些配置很多没有 Kconfig 硬性最大值，理论上可以继续增大，但实际最大值由 SRAM0 剩余空间决定。当前 SRAM0 还有较大余量，但也要给线程栈、日志、应用业务留空间。

建议分三档：

| 场景 | contexts/conn | poll | pkt RX/TX | buf RX/TX | 适用情况 |
| --- | ---: | ---: | ---: | ---: | --- |
| 当前普通运行 | 16 / 16 | 8 | 16 / 16 | 32 / 32 | 4 个 Modbus TCP + Telnet + MQTT，够用 |
| 推荐最优稳定值 | 20 / 20 | 10 | 24 / 24 | 48 / 48 | 更适合首发稳定版 |
| 压力测试值 | 24 / 24 | 12 | 32 / 32 | 64 / 64 | 4 客户端高频读 + MQTT 日志 + Telnet + OTA |

`NET_BUF_DATA_SIZE=1536` 建议保持不变。因为以太网 MTU 通常 1500，1536 可以容纳完整以太网负载附近的数据，减少分片 buffer 链。把它调小会省 RAM，但可能让大包变成多个 buffer；把它调大收益不明显，反而浪费。

## 6. 当前业务的最优建议

如果目标是“稳定优先，不抠 RAM”，我建议把网络资源改成：

```conf
CONFIG_NET_MAX_CONTEXTS=20
CONFIG_NET_MAX_CONN=20
CONFIG_ZVFS_POLL_MAX=10

CONFIG_NET_PKT_RX_COUNT=24
CONFIG_NET_PKT_TX_COUNT=24
CONFIG_NET_BUF_RX_COUNT=48
CONFIG_NET_BUF_TX_COUNT=48
CONFIG_NET_BUF_DATA_SIZE=1536
```

如果要做极限压力测试，例如同时执行：

```text
4 个 Modbus TCP 客户端高频读寄存器
1 个 Telnet Shell 在线诊断
MQTT 日志持续上报
mcumgr UDP OTA 上传镜像
```

建议压力测试配置：

```conf
CONFIG_NET_MAX_CONTEXTS=24
CONFIG_NET_MAX_CONN=24
CONFIG_ZVFS_POLL_MAX=12

CONFIG_NET_PKT_RX_COUNT=32
CONFIG_NET_PKT_TX_COUNT=32
CONFIG_NET_BUF_RX_COUNT=64
CONFIG_NET_BUF_TX_COUNT=64
CONFIG_NET_BUF_DATA_SIZE=1536
```

这档配置只算网络数据 buffer，约占：

```text
(64 + 64) * 1536 = 196,608 B
```

再加结构体和协议栈开销，会明显增加 RAM 占用。STM32H743 当前还有空间，但不建议默认一上来就拉到更高，除非压力测试证明需要。

## 7. 最大能力边界怎么看

对新人来说，不要只问“最大能配多少”，要看这四个边界：

1. Flash 边界：应用镜像不能超过 768 KiB slot。当前约 232 KiB，空间充足。

2. SRAM0 边界：当前主要 RAM 区是 512 KiB SRAM0。当前约 202 KiB，建议稳定版控制在 350 KiB 以内。

3. 网络连接边界：当前配置能支撑约 16 个网络 context，业务最大压力估算 11 到 13 个，够用。若增加更多 TCP 服务或更多 Modbus TCP 客户端，需要同步增加 `NET_MAX_CONTEXTS` 和 `NET_MAX_CONN`。

4. 网络吞吐边界：由 `NET_PKT_*`、`NET_BUF_*`、以太网驱动 DMA、TCP 工作队列共同决定。Modbus TCP 帧很小，真正吃资源的是 OTA 上传、MQTT 大量日志、Telnet 输出大量诊断信息。

## 8. 怎么验证资源够不够

建议测试时用这些 Shell 命令：

```text
kernel threads
kernel stacks
net stats
net stats reset
net iface
log status
show_encoder_stats
```

重点观察：

| 现象 | 可能原因 |
| --- | --- |
| TCP 连接被拒绝 | `NET_MAX_CONTEXTS` / `NET_MAX_CONN` 不够，或客户端池满 |
| Modbus TCP 偶发断开 | buffer 紧张、客户端超时、RAW ADU 处理超时 |
| OTA 过程中日志丢失 | TX buffer 或 MQTT publish 资源紧张 |
| Shell 输出卡顿 | Shell 栈、网络 TX、Telnet socket 输出阻塞 |
| 系统复位 | 看门狗未喂、线程卡死、栈溢出 |
| build 后 RAM 暴涨 | buffer 数量或栈数量调得过大 |

如果测试时出现资源问题，调参顺序建议是：

1. 先确认业务最大连接数是否合理，例如 Modbus TCP 是否真的需要超过 4 个客户端。
2. 再加 `NET_MAX_CONTEXTS` 和 `NET_MAX_CONN`。
3. 再加 `NET_PKT_RX_COUNT`、`NET_PKT_TX_COUNT`。
4. 最后加 `NET_BUF_RX_COUNT`、`NET_BUF_TX_COUNT`，因为这两个最吃 RAM。
5. 栈不要盲目加，先用 `kernel stacks` 看高水位。

## 9. 当前结论

当前资源配置对普通运行是够的：

```text
4 个 Modbus TCP 客户端
+ Telnet Shell
+ MQTT log / MQTT shell service
+ mcumgr UDP OTA 空闲或低频使用
```

如果要做“最大同时压力测试”，建议使用第 6 章的压力测试配置。对于首发稳定版，我更推荐使用“推荐最优稳定值”，它比当前配置多一些余量，但不会像压力测试配置那样明显吃 RAM。
