# Zephyr 系统健康检测与状态灯示例

## 这个示例实现了什么

本工程把原来的心跳灯线程和看门狗线程合并为 `system_health_app`。它负责三件事：

1. 周期检查业务事件是否按时更新。
2. 根据当前系统状态控制 PD10 状态灯。
3. 根据业务健康等级决定是否喂 STM32H743 IWDG 硬件看门狗。
4. 每隔 6 秒打印一次设备运行时间，便于通过 syslog 确认健康线程和网络日志链路正常。

正常状态下，状态灯亮 500 ms、灭 500 ms。异常状态下，状态灯用“快闪若干次 + 长停”表达不同异常。快闪次数由事件 `priority` 决定，`priority` 越小表示越严重。当存在 `priority = 1` 的离线事件时，健康线程停止喂狗，让 IWDG 复位系统。

## 怎么使用

默认配置：

```conf
CONFIG_WATCHDOG=y
CONFIG_WDT_DISABLE_AT_BOOT=n
CONFIG_CRANER_ENABLE_SYSTEM_HEALTH_THREAD=y
CONFIG_CRANER_SYSTEM_HEALTH_WDT_TIMEOUT_MS=30000
```

构建：

```powershell
.\build.ps1 -SkipOtaImages
```

启动后应看到类似日志：

```text
<inf> system_health_app: System health watchdog started on iwdg1, timeout=8000 ms
<inf> system_health_app: System health monitor started, status LED normal=500/500 ms, error blink=150 ms, pause=3000 ms
<inf> system_health_app: System health alive: uptime_ms=6717
```

## 前置条件

硬件上使用 PD10 作为状态灯，高电平点亮。IWDG 是 STM32H743 片内独立看门狗，不需要额外连线。

## Devicetree：硬件描述

状态灯仍使用原来的 `heartbeat-led` alias：

```dts
aliases {
	heartbeat-led = &heartbeat_led;
	watchdog0 = &iwdg;
};
```

应用代码把 `heartbeat-led` 当作状态灯使用：

```c
#define STATUS_LED_NODE DT_ALIAS(heartbeat_led)
```

IWDG 需要在板级 DTS 中开启：

```dts
&iwdg {
	status = "okay";
};
```

## Kconfig/prj.conf：软件配置

核心开关：

```conf
CONFIG_CRANER_ENABLE_SYSTEM_HEALTH_THREAD=y
```

这个开关控制是否编译：

```text
src/system_health_app.c
src/system_health_event_table.c
```

状态灯时序是系统健康灯语协议的一部分，固定在 `src/system_health_app.c` 内部：正常 500 ms 亮 / 500 ms 灭；异常 150 ms 快闪，最后 3000 ms 长停。

看门狗配置：

```conf
CONFIG_CRANER_SYSTEM_HEALTH_WDT_TIMEOUT_MS=30000
```

## 业务事件表

业务监控参考 `offline_manage` 的思想实现。事件表只放规则，不放运行状态：

```c
struct system_health_event_obj {
	enum system_health_event event;
	bool enable;
	uint8_t priority;
	uint32_t offline_timeout_ms;
	system_health_event_callback_t offline_first_func;
	system_health_event_callback_t offline_func;
	system_health_event_callback_t online_first_func;
	system_health_event_callback_t online_func;
};
```

当前事件表在 `src/system_health_event_table.c`：

| 事件 | priority | offline_timeout_ms | 默认启用 |
| --- | ---: | ---: | --- |
| `SYSTEM_HEALTH_ETHERNET` | 1 | 3000 | 否 |
| `SYSTEM_HEALTH_MODBUS_TCP` | 2 | 3000 | 否 |
| `SYSTEM_HEALTH_READ_SLEWING_ENCODER` | 3 | 3000 | 跟随回转编码器线程 |
| `SYSTEM_HEALTH_READ_LUFFING_ENCODER` | 4 | 3000 | 跟随变幅编码器线程 |
| `SYSTEM_HEALTH_READ_HOISTING_ENCODER` | 5 | 3000 | 跟随吊钩编码器线程 |

`priority` 有两个作用：

1. 多个异常同时存在时，状态灯显示 `priority` 最小的异常。
2. 当 `priority = 1` 的事件离线时，`system_health_app` 不再喂 IWDG，等待硬件看门狗复位系统。

## 运行状态

运行状态放在 `src/system_health_app.c` 内部：

```c
struct system_health_event_state {
	bool configured;
	bool enable;
	uint8_t priority;
	uint32_t offline_timeout_ms;
	uint32_t update_timestamp_ms;
	bool offline_status;
	bool last_offline_status;
	...
};
```

`update_timestamp_ms` 表示最近一次业务正常上报时间。

`offline_status` 表示当前检查周期是否异常。

`last_offline_status` 表示上一轮检查周期是否异常，用于判断刚离线、持续离线、刚恢复、持续在线。

## 业务代码如何接入

业务正常时调用：

```c
system_health_update_event(SYSTEM_HEALTH_READ_SLEWING_ENCODER);
```

当前 Modbus RTU 编码器读取成功后已经接入：

```c
if (err == 0) {
	system_health_update_event(encoder->health_event);
}
```

健康线程周期判断：

```text
now_ms - update_timestamp_ms > offline_timeout_ms
```

超过阈值则认为该事件异常。

## 状态灯规则

无异常：

```text
亮 500 ms，灭 500 ms
```

有异常：

```text
亮 150 ms，灭 150 ms，重复 priority 次，然后灭 3000 ms
```

如果多个异常同时存在，显示 `priority` 最小的异常。

## 看门狗策略

IWDG 由 `system_health_app` 统一管理：

```text
无异常：继续喂狗
priority > 1 的普通异常：继续喂狗，只改变状态灯
priority = 1 的严重异常：停止喂狗，让 IWDG 复位
system_health_app 自身卡死：无法喂狗，IWDG 自动复位
```

因此，普通通信异常不会导致 MCU 反复重启；只有事件表中定义为 `priority = 1` 的严重离线事件才会触发硬件复位。

## 如何扩展

新增业务监控事件：

1. 在 `include/system_health_app.h` 增加枚举。
2. 在 `src/system_health_event_table.c` 增加事件表项。
3. 在业务正常执行或通信成功时调用 `system_health_update_event()`。

临时禁用某个事件：

```c
system_health_disable_event(SYSTEM_HEALTH_READ_SLEWING_ENCODER);
```

重新启用某个事件：

```c
system_health_enable_event(SYSTEM_HEALTH_READ_SLEWING_ENCODER);
```

## 故障排查

| 现象 | 检查点 |
| --- | --- |
| 编译报 `Missing heartbeat-led alias` | 检查板级 DTS 是否有 `heartbeat-led` alias |
| 编译报 `Missing enabled watchdog0 alias` | 检查板级 DTS 是否有 `watchdog0 = &iwdg;` 且 `&iwdg` 为 `okay` |
| 状态灯一直快闪 | 检查对应业务是否调用 `system_health_update_event()` |
| 状态灯正常但通信实际异常 | 检查事件表中该事件是否 `enable = true` |
| 设备周期复位 | 检查健康线程是否启动、IWDG 超时时间是否过短 |
