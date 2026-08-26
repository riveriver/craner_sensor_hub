#include "system_health_event_table.h"
#include "time_service.h"

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(system_health_event_table, CONFIG_LOG_DEFAULT_LEVEL);

static void log_offline_event(
	const struct sys_health_event_status *status)
{
	LOG_ERR("System health event offline: event=%u name=%s priority=%u timeout=%u ms offline_count=%u",
		status->event,
		status->name != NULL ? status->name : "",
		status->priority,
		status->offline_timeout_ms,
		status->offline_count);
}

static void log_online_event(
	const struct sys_health_event_status *status)
{
	LOG_ERR("System health event recovered: event=%u name=%s offline_duration=%u ms recover_count=%u max_offline=%u ms",
		status->event,
		status->name != NULL ? status->name : "",
		status->last_offline_duration_ms,
		status->recover_count,
		status->max_offline_duration_ms);
}

const struct sys_health_event system_health_event_table[] = {
#ifdef CONFIG_SYS_HEALTH_ICMP_PROBE
	{
		.event = SYSTEM_HEALTH_ETHERNET,
		.name = "ethernet_gateway",
		.enable = IS_ENABLED(CONFIG_SYS_HEALTH_ICMP_PROBE),
		.priority = 2,
		.offline_timeout_ms =
			(CONFIG_SYS_HEALTH_ICMP_PROBE_PERIOD_MS *
			 CONFIG_SYS_HEALTH_ICMP_PROBE_MAX_CONSECUTIVE_FAILURES) +
			CONFIG_SYS_HEALTH_ICMP_PROBE_TIMEOUT_MS +
			1000U,
		.action_mask = SYS_HEALTH_ACTION_LOG |
			       SYS_HEALTH_ACTION_SET_DEGRADED |
			       SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED,
		.action_delay_ms = 0,
		.offline_first_func = log_offline_event,
		.online_first_func = log_online_event,
	},
#endif
	{
		.event = SYSTEM_HEALTH_READ_SLEWING_ENCODER,
		.name = "slewing_encoder",
		.enable = IS_ENABLED(CONFIG_CRANE_ENCODER_USE_SLEWING),
		.priority = 3,
		.offline_timeout_ms = 3000,
		.offline_first_func = log_offline_event,
		.online_first_func = log_online_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_LUFFING_ENCODER,
		.name = "luffing_encoder",
		.enable = IS_ENABLED(CONFIG_CRANE_ENCODER_USE_LUFFING) ||
			  IS_ENABLED(CONFIG_ENABLE_LUFFING_IMU_APP),
		.priority = 4,
		.offline_timeout_ms = 3000,
		.offline_first_func = log_offline_event,
		.online_first_func = log_online_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_HOISTING_ENCODER,
		.name = "hoisting_encoder",
		.enable = IS_ENABLED(CONFIG_CRANE_ENCODER_USE_HOISTING),
		.priority = 5,
		.offline_timeout_ms = 3000,
		.offline_first_func = log_offline_event,
		.online_first_func = log_online_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_ANEMOMETER,
		.name = "anemometer",
		.enable = IS_ENABLED(CONFIG_ENABLE_CRANE_ANEMOMETER_APP),
		.priority = 6,
		.offline_timeout_ms = 3000,
		.offline_first_func = log_offline_event,
		.online_first_func = log_online_event,
	},
};

const int system_health_event_table_size =
	ARRAY_SIZE(system_health_event_table);

int system_health_event_table_get_unix_time_s(int64_t *unix_time_s,
	void *user_data)
{
	struct time_service_status status;

	ARG_UNUSED(user_data);

	if (unix_time_s == NULL) {
		return -EINVAL;
	}

	time_service_get_status(&status);
	if (!status.wall_time_valid ||
	    status.quality < TIME_SERVICE_QUALITY_ESTIMATED) {
		return -ENODATA;
	}

	*unix_time_s = status.unix_time_s;
	return 0;
}
