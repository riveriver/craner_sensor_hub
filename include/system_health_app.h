#ifndef SYSTEM_HEALTH_APP_H
#define SYSTEM_HEALTH_APP_H

#include <stdbool.h>
#include <stdint.h>

enum system_health_event {
	SYSTEM_HEALTH_NONE = 0,
	SYSTEM_HEALTH_ETHERNET,
	SYSTEM_HEALTH_MODBUS_TCP,
	SYSTEM_HEALTH_READ_SLEWING_ENCODER,
	SYSTEM_HEALTH_READ_LUFFING_ENCODER,
	SYSTEM_HEALTH_READ_HOISTING_ENCODER,
	SYSTEM_HEALTH_READ_ANEMOMETER,
	SYSTEM_HEALTH_EVENT_MAX,
};

void system_health_update_event(enum system_health_event event);
void system_health_enable_event(enum system_health_event event);
void system_health_disable_event(enum system_health_event event);
bool system_health_is_event_offline(enum system_health_event event);

#endif /* SYSTEM_HEALTH_APP_H */
