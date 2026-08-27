# 系统健康服务移植与使用指南

本文档说明如何把 `module/system_health_service` 移植到其他 Zephyr 项目，并接入产品业务。

## 模块定位

`system_health_service` 是健康监督核心，不是某个产品的业务模块。

它负责：

- 保存健康事件状态。
- 根据超时时间判断事件离线。
- 记录离线、恢复、持续时间等诊断信息。
- 触发降级、重启、停止喂狗等动作请求。
- 提供可选 shell 诊断接口。

产品负责：

- 定义自己的健康事件 ID。
- 注册事件表。
- 在业务成功时调用 `sys_health_event_report()` 报活。
- 选择是否启用 LED、watchdog、ICMP probe、计划重启等可选能力。

## 当前模块文件结构

```text
module/system_health_service/
  system_health_service.h
  system_health_service_internal.h
  system_health_service.c
  system_health_event.c
  system_health_action.c
  system_health_state.c
  system_health_shell.c
```

核心拆分原则：

- `event`：事件注册、报活、启停、查询和遍历。
- `state`：全局状态、离线状态机、系统保护内置事件和周期线程。
- `action`：动作锁存、action callback、action handler、重启/降级/停止喂狗请求。
- `shell`：诊断命令，可通过 `CONFIG_SYS_HEALTH_SERVICE_SHELL` 裁剪。

## 移植步骤

1. 复制或以 submodule 方式引入 `module/system_health_service/`。
2. 在主工程 `CMakeLists.txt` 中加入：

```cmake
add_subdirectory(module/system_health_service)

target_link_libraries(app PRIVATE
    system_health_service
)
```

3. 在主工程 Kconfig 中 `rsource` 模块 Kconfig，或使用项目的自动 Kconfig 收集机制。
4. 在 `prj.conf` 或产品配置片段中启用：

```conf
CONFIG_SYS_HEALTH_SERVICE=y
```

5. 产品侧新增公共头，例如 `include/system_health_app.h`，定义产品事件 ID。
6. 产品侧新增事件表，例如 `src/system_health_event_table.c`。
7. 系统初始化时调用：

```c
sys_health_init(system_health_event_table,
                system_health_event_table_size,
                &time_provider);
```

8. 各业务模块在成功完成关键动作时调用：

```c
sys_health_event_report(event_id);
```

## 事件 ID 建议

保留 `SYS_HEALTH_EVENT_SYSTEM_PROTECT` 作为系统保护事件：

```c
enum system_health_event {
    SYSTEM_HEALTH_NONE = 0,
    SYSTEM_HEALTH_SYSTEM_OFFLINE = SYS_HEALTH_EVENT_SYSTEM_PROTECT,
    SYSTEM_HEALTH_ETHERNET = 2,
    SYSTEM_HEALTH_READ_SLEWING_ENCODER = 100,
};
```

建议按业务域预留 ID 段：

- `1`：系统保护内置事件。
- `2-99`：网络、电源、存储等平台事件。
- `100-199`：传感器采样事件。
- `200-299`：通信协议事件。

## 事件表字段

```c
{
    .event = SYSTEM_HEALTH_READ_SLEWING_ENCODER,
    .name = "slewing_encoder",
    .enable = IS_ENABLED(CONFIG_ENCODER_USE_SLEWING),
    .priority = 3,
    .offline_timeout_ms = 3000,
    .action_mask = SYS_HEALTH_ACTION_LOG,
    .action_delay_ms = 0,
    .offline_first_func = log_offline_event,
    .online_first_func = log_online_event,
}
```

字段含义：

- `event`：事件 ID。
- `name`：shell 和日志中显示的名字。
- `enable`：默认是否启用。
- `priority`：优先级，LED 后端显示最高优先级离线事件。
- `offline_timeout_ms`：多久没有报活就判定离线。
- `action_mask`：离线后要请求的动作。
- `action_delay_ms`：离线持续多久后触发动作。
- `offline_first_func`：首次离线回调。
- `online_first_func`：恢复在线回调。

## 报活模型

推荐模型是“成功报活，失败不报活”。业务模块只在关键动作成功时调用：

```c
sys_health_event_report(event_id);
```

如果业务连续失败，不需要每次失败都调用健康服务。健康服务会根据最后一次成功报活时间和 `offline_timeout_ms` 统一判断离线。

## 动作模型

支持的动作：

```c
SYS_HEALTH_ACTION_LOG
SYS_HEALTH_ACTION_CALLBACK
SYS_HEALTH_ACTION_SET_DEGRADED
SYS_HEALTH_ACTION_REQUEST_REBOOT
SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED
```

动作请求由核心锁存，具体执行由可选后端决定。

## DTS 需求

如果启用 LED 或 watchdog 后端，板级 DTS 需要提供：

```dts
aliases {
    heartbeat-led = &heartbeat_led;
    watchdog0 = &iwdg;
};
```

没有启用对应后端时不需要这些别名。

## 推荐配置

基础健康服务：

```conf
CONFIG_SYS_HEALTH_SERVICE=y
CONFIG_SYS_HEALTH_MAX_EVENTS=16
CONFIG_SYS_HEALTH_CHECK_INTERVAL_MS=100
```

开发期 shell：

```conf
CONFIG_SHELL=y
CONFIG_SYS_HEALTH_SERVICE_SHELL=y
```

需要 LED 状态显示：

```conf
CONFIG_GPIO=y
CONFIG_SYS_HEALTH_LED_INDICATOR_BACKEND=y
```

需要 watchdog 保护：

```conf
CONFIG_WATCHDOG=y
CONFIG_SYS_HEALTH_WATCHDOG_FEED_BACKEND=y
CONFIG_SYS_HEALTH_WDT_TIMEOUT_MS=30000
```

需要网络连通性探针：

```conf
CONFIG_NET_IPV4=y
CONFIG_SYS_HEALTH_ICMP_PROBE=y
CONFIG_SYS_HEALTH_ICMP_PROBE_EVENT_ID=2
CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_GATEWAY=y
```

## 移植检查清单

- `module/system_health_service` 已加入 CMake。
- 主工程链接了 `system_health_service` target。
- Kconfig 能看到 `SYS_HEALTH_SERVICE`。
- `CONFIG_SYS_HEALTH_MAX_EVENTS` 大于产品事件数量。
- 产品事件 ID 没有重复。
- 每个关键业务成功路径都调用了 `sys_health_event_report()`。
- `offline_timeout_ms` 大于正常业务周期和允许抖动。
- 启用 LED/watchdog 后端时 DTS aliases 已配置。
- shell 中 `health events` 能看到注册事件。
