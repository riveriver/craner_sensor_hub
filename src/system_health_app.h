#ifndef SYSTEM_HEALTH_APP_H
#define SYSTEM_HEALTH_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "system_health_service.h"

enum system_health_event {
	SYSTEM_HEALTH_NONE = 0,
	SYSTEM_HEALTH_SYSTEM_OFFLINE =
		SYS_HEALTH_EVENT_SYSTEM_PROTECT,
	SYSTEM_HEALTH_ETHERNET = 2,
	SYSTEM_HEALTH_READ_SLEWING_ENCODER = 100,
	SYSTEM_HEALTH_READ_LUFFING_ENCODER,
	SYSTEM_HEALTH_READ_HOISTING_ENCODER,
	SYSTEM_HEALTH_READ_ANEMOMETER,
};

static inline void system_health_update_event(enum system_health_event event)
{
	sys_health_event_report((uint16_t)event);
}

static inline void system_health_enable_event(enum system_health_event event)
{
	sys_health_event_enable((uint16_t)event);
}

static inline void system_health_disable_event(enum system_health_event event)
{
	sys_health_event_disable((uint16_t)event);
}

static inline bool system_health_is_event_offline(enum system_health_event event)
{
	return sys_health_event_is_offline((uint16_t)event);
}

#endif /* SYSTEM_HEALTH_APP_H */