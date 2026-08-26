# system_health_service

`system_health_service` is a reusable Zephyr health supervisor. The core owns
health events, offline detection, action state, and one service thread that
runs registered periodic handlers.

Optional checks/probes detect health conditions:

- `icmp_probe.c`: checks IPv4 reachability and reports a health event.
- `stack_usage_check.c`: checks thread stack usage.

Optional backends handle action state or presentation:

- `led_indicator_backend.c`: displays the highest-priority offline event.
- `watchdog_feed_backend.c`: owns watchdog feeding and can stop on request.
- `reboot_action_backend.c`: performs requested reboot actions.

`scheduled_reboot_policy.c` is a policy because it decides when a time-based
reboot should be requested.

Offline detection is core behavior: if an enabled event is not reported within
`offline_timeout_ms`, it becomes offline. Optional checks/probes decide which
events to report. Optional backends decide how offline/action state is handled.

Product code registers health events and periodically calls
`sys_health_event_report()`. The service keeps the existing first/continuous
offline callbacks and adds two action fields per event:

- `action_mask`: what to request when this event remains offline.
- `action_delay_ms`: how long the event must stay offline before actions run.

Event callbacks receive a `const struct sys_health_event_status *` snapshot.
Use `offline_first_func` and `online_first_func` for transition logs, and
`offline_func`/`online_func` for continuous handling while the state remains
unchanged.

When an event stays offline longer than its configured delay, the core latches
the requested action state. Backends decide how to handle that state:

- `SYS_HEALTH_ACTION_LOG`
- `SYS_HEALTH_ACTION_CALLBACK`
- `SYS_HEALTH_ACTION_SET_DEGRADED`
- `SYS_HEALTH_ACTION_REQUEST_REBOOT`
- `SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED`

Stopping watchdog feed, rebooting, and LED display are optional backends, not
core behavior. The watchdog feed backend is the only module file
that owns the hardware watchdog.

The stack usage check runs in its own thread when
`CONFIG_SYS_HEALTH_STACK_USAGE_CHECK=y`. The ICMP probe also runs in its own
thread, sends ICMP echo requests to
the default interface gateway when `CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_GATEWAY=y`,
and reports the application event configured by
`CONFIG_SYS_HEALTH_ICMP_PROBE_EVENT_ID` only after a successful probe. Probe
failures do not directly set the event offline; the core event timeout is the
single offline decision point. Registered events decide whether an offline
state only alarms or eventually raises degraded/protect state.

## Required DTS

```dts
aliases {
	heartbeat-led = &heartbeat_led;
	watchdog0 = &iwdg;
};
```

## Required Kconfig

```conf
CONFIG_WATCHDOG=y
CONFIG_GPIO=y
CONFIG_SYS_HEALTH_SERVICE=y
CONFIG_SYS_HEALTH_LED_INDICATOR_BACKEND=y
CONFIG_SYS_HEALTH_WATCHDOG_FEED_BACKEND=y
CONFIG_SYS_HEALTH_STACK_USAGE_CHECK=y
CONFIG_SYS_HEALTH_ICMP_PROBE=y
CONFIG_SYS_HEALTH_ICMP_PROBE_EVENT_ID=2
CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_GATEWAY=y
CONFIG_NET_IPV4=y
```

## Shell

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

## Main Project Integration

```cmake
add_subdirectory(module/system_health_service)
```

The module currently adds enabled service sources directly to the application
target from its `CMakeLists.txt`. This keeps `SYS_INIT()` hooks and shell
commands from optional backends in the final image even when no application
code calls the backend functions directly.

Product-specific event IDs and registrations should remain in the product
application, not in this module.

The public API uses neutral `sys_health_*` names:

```c
sys_health_init(event_table, event_count, time_provider);
sys_health_event_register(&event);
sys_health_event_report(event_id);
sys_health_event_set(event_id, true);
sys_health_policy_register(&policy);
```
