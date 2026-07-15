# 设备身份服务说明

## This example implements

本项目使用 `device_identity_service` 统一生成设备身份。固定字段为公司名 `craner`、项目名 `test001`、设备类型 `mt2r`；唯一字段来自 STM32 96-bit Unique Device ID。服务会对完整 STM32 UID 做 FNV-1a 64-bit hash，然后截取 5 字节作为短 UID。

最终身份格式如下：

```text
short_uid:      a1b2c3d4e5
mac:            02:a1:b2:c3:d4:e5
hostname:       craner-test001-mt2r
mDNS:           craner-test001-mt2r.local
mqtt_client_id: craner-test001-mt2r-a1b2c3d4e5
```

## How to use it

烧录后在串口 shell 输入：

```sh
net iface
```

应能看到 `Hostname` 为 `craner-test001-mt2r`，`Link addr` 变成 `02:xx:xx:xx:xx:xx`，其中后 5 字节与短 UID 一致。设备也会通过 DHCP 使用动态 hostname，并通过 mDNS 响应：

```powershell
ping craner-test001-mt2r.local
```

## Prerequisites

板卡需要 STM32 HWINFO 支持，当前 STM32H743 可通过 Zephyr `hwinfo_get_device_id()` 读取 12 字节唯一 ID。网络需要 DHCP，局域网内要允许 mDNS 组播 `224.0.0.251:5353`，电脑侧也要支持 `.local` 名称解析。

## Kconfig/prj.conf: software configuration

关键配置如下：

```conf
CONFIG_HWINFO=y
CONFIG_NET_HOSTNAME_ENABLE=y
CONFIG_NET_HOSTNAME_DYNAMIC=y
CONFIG_NET_HOSTNAME_MAX_LEN=63
CONFIG_MDNS_RESPONDER=y
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_AUTO_INIT=n
CONFIG_NET_CONFIG_INIT_TIMEOUT=30
```

`CONFIG_NET_CONFIG_AUTO_INIT=n` 表示网络不会在系统启动时自动开始 DHCP。应用会先生成身份、设置 MAC 和 hostname，然后调用 `net_config_init()` 启动 DHCP，这样 DHCP 服务器看到的就是正确身份。

## Business/application code

身份服务位于：

```text
src/device_identity_service.c
src/device_identity_service.h
```

`main.c` 启动顺序为：

```text
device_identity_service_init()
net_config_init()
```

MQTT 连接管理器不再使用写死的 client id，而是调用：

```c
device_identity_mqtt_client_id_get()
```

这样 DHCP hostname、mDNS 名称、MQTT client id、设备短 UID 都来自同一套生成规则。

## How to extend it

如果后续公司名、项目名、设备类型要改，优先改 `src/device_identity_service.c` 里的：

```c
#define DEVICE_IDENTITY_COMPANY "craner"
#define DEVICE_IDENTITY_PROJECT "test001"
#define DEVICE_IDENTITY_DEVICE_TYPE "mt2r"
```

如果要把这些字段做成可配置项，可以再把它们迁移到应用 Kconfig。

## Troubleshooting

如果同一局域网内同时有多台设备，固定 hostname 和固定 mDNS 名称可能冲突。当前方案适合单设备调试；批量部署时建议把短 UID 加回 hostname 或使用 DHCP 租约表 / MQTT 上线信息区分设备。

如果 `net iface` 仍然看到 `02:80:e1:xx:xx:xx`，说明应用层设置 MAC 没有生效，优先检查 `device_identity_service_init()` 是否在 `net_config_init()` 之前执行。

如果 `ping xxx.local` 失败，但 `net iface` 已经获取 IP，先确认电脑和设备在同一二层网络，并确认网络没有禁止 mDNS 组播。Windows 环境下 `.local` 支持受系统和网络策略影响，必要时优先从 DHCP 租约表或 MQTT 上线信息确认设备 IP。
