#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "system_health_event_table.h"

LOG_MODULE_DECLARE(system_health_app);

static void system_health_log_offline_event(
	const struct system_health_event_obj *event_obj)
{
	LOG_ERR_RATELIMIT_RATE(60000,
			       "System health event offline: event=%d",
			       event_obj->event);
}

const struct system_health_event_obj system_health_event_table[] = {
	{
		.event = SYSTEM_HEALTH_SYSTEM_OFFLINE,
		.enable = false,
		.priority = 1,
		.offline_timeout_ms = 3000,
		.offline_first_func = system_health_log_offline_event,
	},
	{
		.event = SYSTEM_HEALTH_ETHERNET,
		.enable = true,
		.priority = 2,
		.offline_timeout_ms = 3000,
		.offline_first_func = system_health_log_offline_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_SLEWING_ENCODER,
		.enable = IS_ENABLED(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD),
		.priority = 3,
		.offline_timeout_ms = 3000,
		.offline_first_func = system_health_log_offline_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_LUFFING_ENCODER,
		.enable = IS_ENABLED(CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD),
		.priority = 4,
		.offline_timeout_ms = 3000,
		.offline_first_func = system_health_log_offline_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_HOISTING_ENCODER,
		.enable = IS_ENABLED(CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD),
		.priority = 5,
		.offline_timeout_ms = 3000,
		.offline_first_func = system_health_log_offline_event,
	},
	{
		.event = SYSTEM_HEALTH_READ_ANEMOMETER,
		.enable = IS_ENABLED(CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD),
		.priority = 6,
		.offline_timeout_ms = 3000,
		.offline_first_func = system_health_log_offline_event,
	},
};

const int system_health_event_table_size =
	ARRAY_SIZE(system_health_event_table);
