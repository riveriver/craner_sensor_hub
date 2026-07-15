# 网络与时间管理服务

## This example implements

本项目把网络和时间拆成两个服务：`network_service` 管以太网链路、DHCP、IP 状态和指数退避重试；`time_service` 管 boot tick、NTP 校时、系统实时时钟和时间可信度。业务代码不直接关心 DHCP 细节，也不直接选择 NTP/RTC/GPS，而是读取这两个服务的统一状态。

当前版本实现：

```text
network_service:
  link up/down
  DHCP waiting/ready/failed
  DHCP 失败后指数退避重试，最大 60 秒
  IP/netmask/gateway/DHCP server/lease 状态缓存

time_service:
  boot_tick 单调时间兜底
  DHCP ready 后触发 NTP
  NTP 失败后指数退避重试，最大 300 秒
  NTP 成功后设置 Zephyr SYS_CLOCK_REALTIME
  预留 RTC/GPS provider 接口
```

## How to use it

构建和烧录：

```powershell
.\build.ps1 -SkipOtaImages
.\flash.ps1
```

串口或 Telnet shell 查看网络状态：

```sh
net_status
```

成功时应看到：

```text
state: ready
link_up: yes
ready: yes
ip: 172.16.0.177
gateway: 172.16.1.254
lease_s: 28800
```

查看时间状态：

```sh
time_status
```

NTP 成功后应看到：

```text
valid: yes
source: ntp
quality: synced
iso8601_utc: 2026-07-15T02:30:00Z
```

立即请求 NTP 同步：

```sh
time_sync
```

MQTT 远程诊断白名单也支持：

```text
net_status
time_status
time_sync
```

## Prerequisites

设备需要以太网链路、可用 DHCP 服务器、可访问的 DNS/NTP。默认 NTP 服务器是：

```text
pool.ntp.org
```

如果现场网络禁止公网 NTP，应改为内网 NTP 服务器。

## Kconfig/prj.conf: software configuration

关键配置：

```conf
CONFIG_NET_DHCPV4=y
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_AUTO_INIT=n
CONFIG_NET_CONFIG_INIT_TIMEOUT=30
CONFIG_SNTP=y
CONFIG_DNS_RESOLVER=y

CONFIG_CRANER_NETWORK_DHCP_RETRY_INITIAL_MS=5000
CONFIG_CRANER_NETWORK_DHCP_RETRY_MAX_MS=60000
CONFIG_CRANER_TIME_SERVICE_NTP_SERVER="pool.ntp.org"
CONFIG_CRANER_TIME_SERVICE_NTP_TIMEOUT_MS=5000
CONFIG_CRANER_TIME_SERVICE_NTP_RETRY_INITIAL_MS=10000
CONFIG_CRANER_TIME_SERVICE_NTP_RETRY_MAX_MS=300000
CONFIG_CRANER_TIME_SERVICE_NTP_RESYNC_INTERVAL_S=3600
```

`CONFIG_NET_CONFIG_AUTO_INIT=n` 表示应用自己控制网络启动顺序：先生成设备身份和 MAC，再启动 DHCP。

## Business/application code

启动顺序在 `src/main.c`：

```text
device_identity_service_init()
network_service_init()
time_service_init()
network_service_start()
```

`network_service_start()` 首次调用 Zephyr `net_config_init()`，等待 DHCP 最多 `CONFIG_NET_CONFIG_INIT_TIMEOUT` 秒。如果失败，服务进入 `failed`，然后按指数退避重启 DHCP：

```text
5s -> 10s -> 20s -> 40s -> 60s -> 60s ...
```

DHCP 成功后，`network_service` 发出 `NETWORK_SERVICE_EVENT_READY`，`time_service` 收到后立即请求 NTP。

`time_service` 成功同步后调用：

```c
sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
```

业务代码获取时间时应使用：

```c
time_service_is_time_valid()
time_service_unix_time_get()
time_service_format_iso8601()
```

## How to extend it

未来接入 RTC 或 GPS 时，不要让业务直接读 RTC/GPS。新增 provider 后调用：

```c
time_service_update_from_source(TIME_SERVICE_SOURCE_RTC,
                                TIME_SERVICE_QUALITY_ESTIMATED,
                                unix_time_s);

time_service_update_from_source(TIME_SERVICE_SOURCE_GPS,
                                TIME_SERVICE_QUALITY_HIGH_PRECISION,
                                unix_time_s);
```

当前优先级为：

```text
GPS > NTP > RTC > boot_tick
```

也就是说 GPS 可以覆盖 NTP，NTP 可以覆盖 RTC，boot tick 只用于运行时长和事件排序，不能当真实年月日使用。

## Troubleshooting

如果 `net_status` 一直是 `dhcp_waiting` 或 `failed`，检查网线、交换机、DHCP 服务器和 VLAN。失败不是致命错误，设备会继续后台重试，Modbus RTU、本地采集和串口 shell 仍可运行。

如果 `time_status` 一直是 `valid: no`，但 `net_status` 已经 ready，优先检查 DNS 和 NTP 服务器是否可达。现场网络不通公网时，把 `CONFIG_CRANER_TIME_SERVICE_NTP_SERVER` 改成内网 NTP。

如果 MQTT 已连接但上线状态里的 `time_valid=false`，说明 MQTT 可达但 NTP 尚未成功。这是允许的，业务数据应同时带 `uptime_ms`，并在 `time_valid=true` 后再信任真实时间戳。
