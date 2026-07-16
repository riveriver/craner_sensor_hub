#ifndef SCHEDULED_REBOOT_SERVICE_H
#define SCHEDULED_REBOOT_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum scheduled_reboot_reason {
	SCHEDULED_REBOOT_REASON_NONE,
	SCHEDULED_REBOOT_REASON_UNRELIABLE_TIME_UPTIME_LIMIT,
	SCHEDULED_REBOOT_REASON_RELIABLE_TIME_DAILY_WINDOW,
};

struct scheduled_reboot_status {
	bool initialized;
	bool time_reliable;
	bool in_reliable_window;
	bool reboot_pending;
	enum scheduled_reboot_reason pending_reason;
	uint32_t last_check_uptime_ms;
	uint32_t unreliable_start_uptime_ms;
	uint32_t unreliable_elapsed_ms;
	uint32_t unreliable_limit_ms;
	uint32_t min_uptime_ms;
	int32_t timezone_offset_minutes;
	uint8_t local_hour;
	uint8_t local_minute;
	uint8_t local_window_minutes;
	int64_t unix_time_s;
	int local_year;
	int local_yday;
	int local_hour_now;
	int local_minute_now;
	int last_reliable_reboot_day_key;
	int last_error;
};

void scheduled_reboot_service_check(uint32_t now_ms);
void scheduled_reboot_service_get_status(struct scheduled_reboot_status *status);
const char *scheduled_reboot_reason_name(enum scheduled_reboot_reason reason);
int scheduled_reboot_service_format_status(char *buf, size_t len);

#endif /* SCHEDULED_REBOOT_SERVICE_H */
