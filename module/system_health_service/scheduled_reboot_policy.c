#include "scheduled_reboot_policy.h"
#include "system_health_service.h"

#include <limits.h>
#include <stdint.h>
#include <time.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(system_health_service, CONFIG_LOG_DEFAULT_LEVEL);

#define MS_PER_HOUR 3600000ULL
#define SECONDS_PER_MINUTE 60LL
#define SCHEDULED_REBOOT_SOURCE_EVENT SYS_HEALTH_EVENT_SYSTEM_PROTECT

static uint32_t last_check_ms;
static uint32_t unreliable_time_start_ms;
static bool unreliable_time_tracking_started;
static int last_reliable_reboot_day_key = INT_MIN;

static uint32_t unreliable_limit_ms(void)
{
	uint64_t limit =
		(uint64_t)
			CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_UNRELIABLE_UPTIME_HOURS *
		MS_PER_HOUR;

	return (uint32_t)MIN(limit, (uint64_t)UINT32_MAX);
}

static bool local_time_in_window(int hour, int minute)
{
	int now_minute = (hour * 60) + minute;
	int start_minute =
		(CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_LOCAL_HOUR * 60) +
		CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_LOCAL_MINUTE;
	int end_minute = start_minute +
		CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_WINDOW_MINUTES;

	if (end_minute <= 24 * 60) {
		return now_minute >= start_minute && now_minute < end_minute;
	}

	return now_minute >= start_minute ||
	       now_minute < (end_minute % (24 * 60));
}

static int local_day_key(const struct tm *local_time)
{
	return (local_time->tm_year * 366) + local_time->tm_yday;
}

static bool get_local_time(struct tm *local_time)
{
	time_t local_unix;
	int64_t unix_time_s;
	int64_t adjusted;

	if (sys_health_time_get_unix_s(&unix_time_s) != 0) {
		return false;
	}

	adjusted = unix_time_s +
		   ((int64_t)
			    CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_TIMEZONE_OFFSET_MINUTES *
		    SECONDS_PER_MINUTE);
	local_unix = (time_t)adjusted;

	return gmtime_r(&local_unix, local_time) != NULL;
}

void scheduled_reboot_policy_check(uint32_t now_ms)
{
	struct tm local_time;
	bool time_reliable;

	if ((uint32_t)(now_ms - last_check_ms) <
	    CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_CHECK_INTERVAL_MS) {
		return;
	}
	last_check_ms = now_ms;

	if (!unreliable_time_tracking_started) {
		unreliable_time_start_ms = now_ms;
		unreliable_time_tracking_started = true;
	}

	time_reliable = get_local_time(&local_time);
	if (time_reliable) {
		int day_key = local_day_key(&local_time);

		unreliable_time_start_ms = now_ms;
		if (local_time_in_window(local_time.tm_hour, local_time.tm_min) &&
		    now_ms >=
			    CONFIG_SYS_HEALTH_SCHEDULED_REBOOT_MIN_UPTIME_MS &&
		    last_reliable_reboot_day_key != day_key) {
			last_reliable_reboot_day_key = day_key;
			LOG_WRN("Scheduled health reboot: reliable local window");
			sys_health_action_request(
				SYS_HEALTH_ACTION_REQUEST_REBOOT,
				SCHEDULED_REBOOT_SOURCE_EVENT,
				"scheduled_reboot");
		}
		return;
	}

	if ((uint32_t)(now_ms - unreliable_time_start_ms) >=
	    unreliable_limit_ms()) {
		LOG_WRN("Scheduled health reboot: unreliable time uptime limit");
		sys_health_action_request(SYS_HEALTH_ACTION_REQUEST_REBOOT,
					  SCHEDULED_REBOOT_SOURCE_EVENT,
					  "scheduled_reboot");
	}
}
