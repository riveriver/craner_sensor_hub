#ifndef SYSTEM_HEALTH_EVENT_TABLE_H
#define SYSTEM_HEALTH_EVENT_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "system_health_app.h"

struct system_health_event_obj;

typedef void (*system_health_event_callback_t)(
	const struct system_health_event_obj *event_obj);

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

extern const struct system_health_event_obj system_health_event_table[];
extern const int system_health_event_table_size;

#define SYSTEM_HEALTH_WATCHDOG_STOP_PRIORITY 1U

#endif /* SYSTEM_HEALTH_EVENT_TABLE_H */
