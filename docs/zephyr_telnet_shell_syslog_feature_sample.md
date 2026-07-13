# Zephyr Telnet Shell 与 Syslog 日志示例

## 这个示例实现了什么

本工程把 Zephyr Shell 后端从串口切换到 Telnet。日志后端同时输出到 UART 和网络 syslog，方便本地串口排障和 Windows 终端集中查看日志。

同时打开 `SHELL_LOG_BACKEND`，并把 Telnet shell 的日志初始等级限制为 `WRN`。这样：

| 日志级别 | Syslog UDP | Telnet Shell |
| --- | --- | --- |
| ERR | 输出 | 输出 |
| WRN | 输出 | 输出 |
| INF | 输出 | 默认不输出 |
| DBG | 取决于模块和运行时过滤 | 默认不输出 |

当前配置：

```text
MCU IPv4:        192.168.18.32
Telnet shell:    TCP 23
Syslog server:   192.168.18.4:5514
Syslog protocol: UDP
```

注意：`LOG_*()` 会同时走 UART 和 syslog。`printk()` 和早期启动信息仍保留在板子的 UART console 上。

## 怎么使用

先确保电脑和 MCU 在同一个网段，例如：

```text
PC:  192.168.18.4
MCU: 192.168.18.32
```

## 连接 Telnet Shell

Windows 可以用 PuTTY，连接方式：

```text
Connection type: Telnet
Host Name:       192.168.18.32
Port:            23
```

也可以使用命令行：

```powershell
telnet 192.168.18.32 23
```

连接成功后可以执行已有 shell 命令，例如：

```text
fw_time
ota show
log status
```

当系统产生 `LOG_WRN()` 或 `LOG_ERR()` 时，这些日志也会显示在 Telnet shell 里。

## 接收 Syslog 日志

在 `192.168.18.4` 这台电脑或服务器上启动 syslog 接收程序，监听 UDP 5514。

Windows 终端可以用 ncat：

```powershell
ncat -ul 5514
```

如果 syslog server IP 不是 `192.168.18.4`，修改 `prj.conf`：

```conf
CONFIG_LOG_BACKEND_NET_SERVER="192.168.18.4:5514"
```

改成实际服务器地址，例如：

```conf
CONFIG_LOG_BACKEND_NET_SERVER="192.168.18.10:5514"
```

## prj.conf 配置

Shell 后端：

```conf
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=n
CONFIG_SHELL_BACKEND_TELNET=y
CONFIG_SHELL_TELNET_PORT=23
CONFIG_SHELL_PROMPT_TELNET="craner:~$ "
CONFIG_SHELL_LOG_BACKEND=y
CONFIG_SHELL_TELNET_INIT_LOG_LEVEL_WRN=y
```

日志后端：

```conf
CONFIG_LOG=y
CONFIG_LOG_BACKEND_UART=y
CONFIG_LOG_BACKEND_NET=y
CONFIG_LOG_BACKEND_NET_SERVER="192.168.18.4:5514"
CONFIG_LOG_BACKEND_NET_AUTOSTART=y
CONFIG_LOG_BACKEND_NET_MAX_BUF_SIZE=480
```

网络依赖：

```conf
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_UDP=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
```

`CONFIG_SHELL_BACKEND_TELNET=y` 会选择 Telnet shell 所需的 socket service。

## 为什么 Shell 不能走 Syslog

Syslog 是日志协议，主要方向是：

```text
MCU -> Syslog server
```

它没有标准的交互输入通道，所以不能作为 shell 后端。

Shell 是交互式协议，需要：

```text
PC -> MCU 输入命令
MCU -> PC 输出结果
```

因此网络 shell 使用 Telnet、MQTT、WebSocket 或自定义 TCP 更合适。本工程使用 Zephyr 原生 Telnet shell。

## 为什么 WRN/ERR 可以同时输出到 Telnet 和 Syslog

Zephyr logging 支持多个后端同时存在：

```text
LOG_WRN / LOG_ERR
       |
       +-- log_backend_net   -> syslog UDP 5514
       |
       +-- shell_log_backend -> Telnet shell
```

`CONFIG_LOG_BACKEND_NET=y` 负责把日志发给 syslog server。

`CONFIG_SHELL_LOG_BACKEND=y` 表示 shell 也作为一个日志输出后端。

`CONFIG_SHELL_TELNET_INIT_LOG_LEVEL_WRN=y` 表示 Telnet shell 后端默认只接收 WRN 和 ERR。

syslog 后端仍按全局日志等级输出。当前全局默认等级是：

```conf
CONFIG_LOG_DEFAULT_LEVEL=3
```

常用等级含义：

| 等级 | 含义 |
| ---: | --- |
| 1 | ERR |
| 2 | WRN |
| 3 | INF |
| 4 | DBG |

## 启动顺序

上电后大致流程：

1. MCUboot 启动，早期日志仍走串口。
2. 应用启动，`printk()` 仍走 UART console。
3. 以太网 PHY link up。
4. Zephyr 配置静态 IP `192.168.18.32`。
5. Telnet shell 在 TCP 23 等待连接。
6. LOG 后端通过 UDP 5514 把日志发送到 syslog server。
7. WRN/ERR 日志也同步显示到 Telnet shell。

如果第 3、4 步失败，Telnet 和 syslog 都不可用，此时仍可通过串口排查。

## 故障排查

| 现象 | 检查点 |
| --- | --- |
| Telnet 连不上 | 检查网线、PHY link、PC 和 MCU 是否同网段 |
| Telnet 连接后没有提示符 | 按一次 Enter，或确认 `CONFIG_SHELL_BACKEND_TELNET=y` |
| Telnet 被日志刷屏 | 检查是否误设成 `CONFIG_SHELL_TELNET_INIT_LOG_LEVEL_INF/DBG` |
| Telnet 看不到 WRN/ERR | 检查 `CONFIG_SHELL_LOG_BACKEND=y` 和 `CONFIG_SHELL_TELNET_INIT_LOG_LEVEL_WRN=y` |
| Syslog 收不到日志 | 检查 `CONFIG_LOG_BACKEND_NET_SERVER` 是否是 syslog server IP |
| Syslog 收不到日志 | 检查服务器是否监听 UDP 5514，以及防火墙是否放行 |
| 串口 shell 没了 | 这是预期行为，shell 已切换到 Telnet |
| 串口仍有少量输出 | 这是 `printk()` 和早期启动信息，UART console 仍保留 |

## 修改服务器地址

只需要改一行：

```conf
CONFIG_LOG_BACKEND_NET_SERVER="192.168.18.4:5514"
```

如果要用 TCP syslog，可以写成：

```conf
CONFIG_LOG_BACKEND_NET_SERVER="tcp://192.168.18.4:5514"
```

当前工程使用 UDP，因为它简单、开销小，也符合常见 syslog 514 端口用法。
