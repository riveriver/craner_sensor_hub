#ifndef SYSTEM_HEALTH_EVENT_TABLE_H
#define SYSTEM_HEALTH_EVENT_TABLE_H

#include "system_health_app.h"

extern const struct sys_health_event system_health_event_table[];
extern const int system_health_event_table_size;

int system_health_event_table_get_unix_time_s(int64_t *unix_time_s,
	void *user_data);

#endif /* SYSTEM_HEALTH_EVENT_TABLE_H */
