target_include_directories(app PRIVATE .)

target_sources_ifdef(CONFIG_SYS_HEALTH_SERVICE app PRIVATE
	system_health_service.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_STACK_USAGE_CHECK app PRIVATE
	stack_usage_check.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_LED_INDICATOR_BACKEND app PRIVATE
	led_indicator_backend.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_WATCHDOG_FEED_BACKEND app PRIVATE
	watchdog_feed_backend.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_REBOOT_ACTION_BACKEND app PRIVATE
	reboot_action_backend.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_ICMP_PROBE app PRIVATE
	icmp_probe.c
)

target_sources_ifdef(CONFIG_SYS_HEALTH_SCHEDULED_REBOOT app PRIVATE
	scheduled_reboot_policy.c
)
