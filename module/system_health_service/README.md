# 系统健康服务

`system_health_service` 是可复用的 Zephyr 系统健康监督模块。它负责健康事件注册、离线判定、动作状态锁存，以及周期处理器调度。

## 负责范围

- 注册和维护健康事件表。
- 根据 `offline_timeout_ms` 判断事件是否离线。
- 维护离线次数、恢复次数、离线持续时间等诊断信息。
- 在事件离线超过 `action_delay_ms` 后触发动作请求。
- 支持 action callback 和 action handler。
- 提供可选探针和后端。

## 可选能力

- `icmp_probe.c`：周期性检查 IPv4 可达性，并向指定健康事件报活。
- `stack_usage_check.c`：检查线程栈使用情况。
- `led_indicator_backend.c`：用 LED 显示最高优先级离线事件。
- `watchdog_feed_backend.c`：负责硬件 watchdog 喂狗，可在健康动作请求后停止喂狗。
- `reboot_action_backend.c`：执行健康服务请求的重启动作。
- `scheduled_reboot_policy.c`：根据时间策略请求计划重启。

## 不负责范围

- 不定义产品自己的健康事件 ID。
- 不决定某个业务事件是否应启用。
- 不知道传感器、网络、寄存器表等业务细节。
- 不直接判断业务是否成功，只接收业务层的 `sys_health_event_report()`。

## 基本使用

产品侧定义事件 ID：

```c
enum system_health_event {
	SYSTEM_HEALTH_NONE = 0,
	SYSTEM_HEALTH_SYSTEM_OFFLINE = SYS_HEALTH_EVENT_SYSTEM_PROTECT,
	SYSTEM_HEALTH_ETHERNET = 2,
	SYSTEM_HEALTH_READ_SLEWING_ENCODER = 100,
};
```

产品侧注册事件表：

```c
const struct sys_health_event system_health_event_table[] = {
	{
		.event = SYSTEM_HEALTH_READ_SLEWING_ENCODER,
		.name = "slewing_encoder",
		.enable = true,
		.priority = 3,
		.offline_timeout_ms = 3000,
		.offline_first_func = log_offline_event,
		.online_first_func = log_online_event,
	},
};
```

业务成功时上报：

```c
sys_health_event_report(SYSTEM_HEALTH_READ_SLEWING_ENCODER);
```

业务失败时通常不用主动设置离线，健康服务会在超时后统一判定离线。需要人工测试或强制置位时可调用：

```c
sys_health_event_set(SYSTEM_HEALTH_READ_SLEWING_ENCODER, true);
```

## 常用配置

```conf
CONFIG_SYS_HEALTH_SERVICE=y
CONFIG_SYS_HEALTH_MAX_EVENTS=16
CONFIG_SYS_HEALTH_CHECK_INTERVAL_MS=100
```

启用 LED 显示：

```conf
CONFIG_GPIO=y
CONFIG_SYS_HEALTH_LED_INDICATOR_BACKEND=y
```

启用 watchdog 喂狗：

```conf
CONFIG_WATCHDOG=y
CONFIG_SYS_HEALTH_WATCHDOG_FEED_BACKEND=y
```

启用网络探针：

```conf
CONFIG_NET_IPV4=y
CONFIG_SYS_HEALTH_ICMP_PROBE=y
CONFIG_SYS_HEALTH_ICMP_PROBE_EVENT_ID=2
CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_GATEWAY=y
```

## DTS 需求

使用 LED 和 watchdog 后端时，板级 DTS 需要提供别名：

```dts
aliases {
	heartbeat-led = &heartbeat_led;
	watchdog0 = &iwdg;
};
```

## Shell 命令

```text
health status
health events
health event <id>
health stats
health enable <id>
health disable <id>
health protect
health probes
health_led status
health_led on
health_led off
health_led auto
```

## CMake 集成

主工程显式加入模块：

```cmake
add_subdirectory(module/system_health_service)

target_link_libraries(app PRIVATE
	system_health_service
)
```

产品事件表、事件 ID、业务报活逻辑应保留在产品应用中，不放进 `module/system_health_service/`。
