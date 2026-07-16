#include "scheduled_reboot_service.h"
#include "time_service.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(scheduled_reboot_service, CONFIG_LOG_DEFAULT_LEVEL);

#define MS_PER_HOUR 3600000ULL
#define SECONDS_PER_MINUTE 60LL

static K_MUTEX_DEFINE(scheduled_reboot_lock);
static struct scheduled_reboot_status reboot_status = {
	.initialized = false,
	.last_reliable_reboot_day_key = INT_MIN,
};

static uint32_t last_check_ms;
static bool unreliable_tracking_started;

const char *scheduled_reboot_reason_name(enum scheduled_reboot_reason reason)
{
	switch (reason) {
	case SCHEDULED_REBOOT_REASON_NONE:
		return "none";
	case SCHEDULED_REBOOT_REASON_UNRELIABLE_TIME_UPTIME_LIMIT:
		return "unreliable_time_uptime_limit";
	case SCHEDULED_REBOOT_REASON_RELIABLE_TIME_DAILY_WINDOW:
		return "reliable_time_daily_window";
	default:
		return "unknown";
	}
}

static uint32_t unreliable_limit_ms(void)
{
	uint64_t limit = (uint64_t)
		CONFIG_CRANER_SCHEDULED_REBOOT_UNRELIABLE_UPTIME_HOURS *
		MS_PER_HOUR;

	return (uint32_t)MIN(limit, (uint64_t)UINT32_MAX);
}

static bool local_time_in_window(int hour, int minute)
{
	int now_minute = (hour * 60) + minute;
	int start_minute = (CONFIG_CRANER_SCHEDULED_REBOOT_LOCAL_HOUR * 60) +
			   CONFIG_CRANER_SCHEDULED_REBOOT_LOCAL_MINUTE;
	int end_minute = start_minute +
			 CONFIG_CRANER_SCHEDULED_REBOOT_LOCAL_WINDOW_MINUTES;

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

static bool get_local_time(const struct time_service_status *time_status,
			   struct tm *local_time)
{
	time_t local_unix;
	int64_t adjusted;

	if (!time_status->wall_time_valid ||
	    time_status->quality < TIME_SERVICE_QUALITY_ESTIMATED) {
		return false;
	}

	adjusted = time_status->unix_time_s +
		   ((int64_t)CONFIG_CRANER_SCHEDULED_REBOOT_TIMEZONE_OFFSET_MINUTES *
		    SECONDS_PER_MINUTE);
	local_unix = (time_t)adjusted;

	return gmtime_r(&local_unix, local_time) != NULL;
}

static void update_status_locked(uint32_t now_ms,
				 const struct time_service_status *time_status,
				 const struct tm *local_time,
				 bool time_reliable, bool in_window)
{
	reboot_status.initialized = true;
	reboot_status.time_reliable = time_reliable;
	reboot_status.in_reliable_window = in_window;
	reboot_status.last_check_uptime_ms = now_ms;
	reboot_status.unreliable_limit_ms = unreliable_limit_ms();
	reboot_status.min_uptime_ms = CONFIG_CRANER_SCHEDULED_REBOOT_MIN_UPTIME_MS;
	reboot_status.timezone_offset_minutes =
		CONFIG_CRANER_SCHEDULED_REBOOT_TIMEZONE_OFFSET_MINUTES;
	reboot_status.local_hour =
		CONFIG_CRANER_SCHEDULED_REBOOT_LOCAL_HOUR;
	reboot_status.local_minute =
		CONFIG_CRANER_SCHEDULED_REBOOT_LOCAL_MINUTE;
	reboot_status.local_window_minutes =
		CONFIG_CRANER_SCHEDULED_REBOOT_LOCAL_WINDOW_MINUTES;
	reboot_status.unix_time_s = time_status->unix_time_s;
	reboot_status.unreliable_elapsed_ms =
		now_ms - reboot_status.unreliable_start_uptime_ms;

	if (local_time != NULL) {
		reboot_status.local_year = local_time->tm_year + 1900;
		reboot_status.local_yday = local_time->tm_yday;
		reboot_status.local_hour_now = local_time->tm_hour;
		reboot_status.local_minute_now = local_time->tm_min;
	} else {
		reboot_status.local_year = 0;
		reboot_status.local_yday = -1;
		reboot_status.local_hour_now = -1;
		reboot_status.local_minute_now = -1;
	}
}

static void request_reboot(uint32_t now_ms, enum scheduled_reboot_reason reason)
{
	k_mutex_lock(&scheduled_reboot_lock, K_FOREVER);
	reboot_status.reboot_pending = true;
	reboot_status.pending_reason = reason;
	k_mutex_unlock(&scheduled_reboot_lock);

	LOG_WRN("Scheduled reboot: reason=%s uptime_ms=%u",
		scheduled_reboot_reason_name(reason), now_ms);
	k_sleep(K_MSEC(250));
	sys_reboot(SYS_REBOOT_COLD);
}

void scheduled_reboot_service_check(uint32_t now_ms)
{
	struct time_service_status time_status;
	struct tm local_time;
	bool time_reliable;
	bool in_window = false;
	bool should_reboot = false;
	enum scheduled_reboot_reason reason = SCHEDULED_REBOOT_REASON_NONE;

	if ((uint32_t)(now_ms - last_check_ms) <
	    CONFIG_CRANER_SCHEDULED_REBOOT_CHECK_INTERVAL_MS) {
		return;
	}
	last_check_ms = now_ms;

	time_service_get_status(&time_status);
	time_reliable = get_local_time(&time_status, &local_time);

	k_mutex_lock(&scheduled_reboot_lock, K_FOREVER);
	if (!unreliable_tracking_started) {
		reboot_status.unreliable_start_uptime_ms = now_ms;
		unreliable_tracking_started = true;
	}

	if (time_reliable) {
		int day_key = local_day_key(&local_time);

		reboot_status.unreliable_start_uptime_ms = now_ms;
		in_window = local_time_in_window(local_time.tm_hour,
						 local_time.tm_min);
		if (in_window &&
		    now_ms >= CONFIG_CRANER_SCHEDULED_REBOOT_MIN_UPTIME_MS &&
		    reboot_status.last_reliable_reboot_day_key != day_key) {
			reboot_status.last_reliable_reboot_day_key = day_key;
			should_reboot = true;
			reason = SCHEDULED_REBOOT_REASON_RELIABLE_TIME_DAILY_WINDOW;
		}
	} else if ((uint32_t)(now_ms - reboot_status.unreliable_start_uptime_ms) >=
		   unreliable_limit_ms()) {
		should_reboot = true;
		reason = SCHEDULED_REBOOT_REASON_UNRELIABLE_TIME_UPTIME_LIMIT;
	}

	update_status_locked(now_ms, &time_status,
			     time_reliable ? &local_time : NULL,
			     time_reliable, in_window);
	k_mutex_unlock(&scheduled_reboot_lock);

	if (should_reboot) {
		request_reboot(now_ms, reason);
	}
}

void scheduled_reboot_service_get_status(struct scheduled_reboot_status *status)
{
	if (status == NULL) {
		return;
	}

	k_mutex_lock(&scheduled_reboot_lock, K_FOREVER);
	*status = reboot_status;
	k_mutex_unlock(&scheduled_reboot_lock);
}

int scheduled_reboot_service_format_status(char *buf, size_t len)
{
	struct scheduled_reboot_status status;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	scheduled_reboot_service_get_status(&status);
	written = snprintk(buf, len,
			   "{\"type\":\"scheduled_reboot_status\","
			   "\"initialized\":%s,\"time_reliable\":%s,"
			   "\"in_reliable_window\":%s,\"reboot_pending\":%s,"
			   "\"pending_reason\":\"%s\","
			   "\"last_check_uptime_ms\":%u,"
			   "\"unreliable_start_uptime_ms\":%u,"
			   "\"unreliable_elapsed_ms\":%u,"
			   "\"unreliable_limit_ms\":%u,"
			   "\"min_uptime_ms\":%u,"
			   "\"timezone_offset_minutes\":%d,"
			   "\"local_hour\":%u,\"local_minute\":%u,"
			   "\"local_window_minutes\":%u,"
			   "\"unix_time_s\":%lld,"
			   "\"local_year\":%d,\"local_yday\":%d,"
			   "\"local_hour_now\":%d,\"local_minute_now\":%d,"
			   "\"last_reliable_reboot_day_key\":%d,"
			   "\"last_error\":%d}",
			   status.initialized ? "true" : "false",
			   status.time_reliable ? "true" : "false",
			   status.in_reliable_window ? "true" : "false",
			   status.reboot_pending ? "true" : "false",
			   scheduled_reboot_reason_name(status.pending_reason),
			   status.last_check_uptime_ms,
			   status.unreliable_start_uptime_ms,
			   status.unreliable_elapsed_ms,
			   status.unreliable_limit_ms,
			   status.min_uptime_ms,
			   status.timezone_offset_minutes,
			   status.local_hour, status.local_minute,
			   status.local_window_minutes,
			   (long long)status.unix_time_s,
			   status.local_year, status.local_yday,
			   status.local_hour_now, status.local_minute_now,
			   status.last_reliable_reboot_day_key,
			   status.last_error);

	if (written < 0) {
		return written;
	}

	return (size_t)written >= len ? -EMSGSIZE : 0;
}
