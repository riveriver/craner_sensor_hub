#include "time_service.h"
#include "network_service.h"
#include "rtc_time_provider.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#ifdef CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP
#include <zephyr/logging/log_output_custom.h>
#endif
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(time_service, CONFIG_LOG_DEFAULT_LEVEL);

#define TIME_SERVICE_STACK_SIZE CONFIG_TIME_SERVICE_STACK_SIZE
#define TIME_SERVICE_PRIORITY 8
#define NTP_TIMEOUT_MS CONFIG_TIME_SERVICE_NTP_TIMEOUT_MS
#define NTP_RESYNC_INTERVAL_MS (CONFIG_TIME_SERVICE_NTP_RESYNC_INTERVAL_S * MSEC_PER_SEC)
#define NTP_RETRY_INITIAL_MS CONFIG_TIME_SERVICE_NTP_RETRY_INITIAL_MS
#define NTP_RETRY_MAX_MS CONFIG_TIME_SERVICE_NTP_RETRY_MAX_MS
#define RTC_WRITEBACK_THRESHOLD_S CONFIG_TIME_SERVICE_RTC_WRITEBACK_THRESHOLD_S
#define LOCAL_TIME_OFFSET_SECONDS \
	((int64_t)CONFIG_LOCAL_TIMEZONE_OFFSET_MINUTES * 60LL)

static struct time_service_status status = {
	.active_source = TIME_SERVICE_SOURCE_BOOT_TICK,
	.quality = TIME_SERVICE_QUALITY_MONOTONIC_ONLY,
	.correction_mode = TIME_SERVICE_CORRECTION_MODE_AUTO,
	.retry_delay_ms = NTP_RETRY_INITIAL_MS,
};
static K_MUTEX_DEFINE(time_lock);
K_MSGQ_DEFINE(sync_request_msgq, sizeof(bool), 4, 4);
static struct k_work_delayable sync_timer_work;
static bool initialized;

static int source_priority(enum time_service_source source)
{
	switch (source) {
	case TIME_SERVICE_SOURCE_GPS:
		return 40;
	case TIME_SERVICE_SOURCE_NTP:
		return 30;
	case TIME_SERVICE_SOURCE_MANUAL:
		return 25;
	case TIME_SERVICE_SOURCE_RTC:
		return 20;
	case TIME_SERVICE_SOURCE_BOOT_TICK:
		return 10;
	default:
		return 0;
	}
}

const char *time_service_source_name(enum time_service_source source)
{
	switch (source) {
	case TIME_SERVICE_SOURCE_NONE:
		return "none";
	case TIME_SERVICE_SOURCE_BOOT_TICK:
		return "boot_tick";
	case TIME_SERVICE_SOURCE_RTC:
		return "rtc";
	case TIME_SERVICE_SOURCE_MANUAL:
		return "manual";
	case TIME_SERVICE_SOURCE_NTP:
		return "ntp";
	case TIME_SERVICE_SOURCE_GPS:
		return "gps";
	default:
		return "unknown";
	}
}

const char *time_service_quality_name(enum time_service_quality quality)
{
	switch (quality) {
	case TIME_SERVICE_QUALITY_INVALID:
		return "invalid";
	case TIME_SERVICE_QUALITY_MONOTONIC_ONLY:
		return "monotonic_only";
	case TIME_SERVICE_QUALITY_ESTIMATED:
		return "estimated";
	case TIME_SERVICE_QUALITY_SYNCED:
		return "synced";
	case TIME_SERVICE_QUALITY_HIGH_PRECISION:
		return "high_precision";
	default:
		return "unknown";
	}
}

const char *time_service_correction_mode_name(
	enum time_service_correction_mode mode)
{
	switch (mode) {
	case TIME_SERVICE_CORRECTION_MODE_AUTO:
		return "auto";
	case TIME_SERVICE_CORRECTION_MODE_MANUAL:
		return "manual";
	default:
		return "unknown";
	}
}

static bool source_should_writeback_rtc(enum time_service_source source,
					enum time_service_quality quality)
{
	if (quality < TIME_SERVICE_QUALITY_SYNCED) {
		return false;
	}

	return source == TIME_SERVICE_SOURCE_MANUAL ||
	       source == TIME_SERVICE_SOURCE_NTP ||
	       source == TIME_SERVICE_SOURCE_GPS;
}

static enum rtc_trust_source trust_source_from_time_source(
	enum time_service_source source)
{
	switch (source) {
	case TIME_SERVICE_SOURCE_MANUAL:
		return RTC_TRUST_SOURCE_MANUAL;
	case TIME_SERVICE_SOURCE_NTP:
		return RTC_TRUST_SOURCE_NTP;
	case TIME_SERVICE_SOURCE_GPS:
		return RTC_TRUST_SOURCE_GPS;
	default:
		return RTC_TRUST_SOURCE_NONE;
	}
}

static void writeback_rtc_if_needed(enum time_service_source source,
				    int64_t unix_time_s)
{
	int64_t rtc_time_s = 0;
	int64_t diff_s;
	enum rtc_trust_source trust_source;
	int rc;

	if (!rtc_time_provider_is_available()) {
		return;
	}

	rc = rtc_time_provider_get_unix_time(&rtc_time_s);
	if (rc == 0) {
		diff_s = unix_time_s - rtc_time_s;
		if (diff_s < 0) {
			diff_s = -diff_s;
		}

		if (diff_s < RTC_WRITEBACK_THRESHOLD_S) {
			return;
		}
	}

	trust_source = trust_source_from_time_source(source);
	rc = rtc_time_provider_set_trusted_unix_time(unix_time_s, trust_source);
	k_mutex_lock(&time_lock, K_FOREVER);
	status.rtc_available = rtc_time_provider_is_available();
	status.rtc_valid = rtc_time_provider_is_valid();
	status.rtc_last_error = rc;
	if (rc == 0) {
		status.last_rtc_writeback_unix_time_s = unix_time_s;
		status.last_rtc_writeback_uptime_ms = k_uptime_get();
	}
	k_mutex_unlock(&time_lock);

	if (rc != 0) {
		LOG_WRN("RTC writeback failed: %d", rc);
	}
}

int time_service_update_from_source(enum time_service_source source,
				    enum time_service_quality quality,
				    int64_t unix_time_s)
{
	struct timespec ts;
	bool accept;
	int rc = 0;

	if (unix_time_s <= 0) {
		return -EINVAL;
	}

	k_mutex_lock(&time_lock, K_FOREVER);
	accept = source == TIME_SERVICE_SOURCE_MANUAL ||
		 !status.wall_time_valid ||
		 source_priority(source) >= source_priority(status.active_source);
	k_mutex_unlock(&time_lock);

	if (!accept) {
		return 0;
	}

	ts.tv_sec = (time_t)unix_time_s;
	ts.tv_nsec = 0;

	rc = sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&time_lock, K_FOREVER);
	status.wall_time_valid = quality >= TIME_SERVICE_QUALITY_ESTIMATED;
	status.active_source = source;
	status.quality = quality;
	status.unix_time_s = unix_time_s;
	status.uptime_ms = k_uptime_get();
	status.last_sync_uptime_ms = status.uptime_ms;
	status.sync_count++;
	status.retry_delay_ms = NTP_RETRY_INITIAL_MS;
	k_mutex_unlock(&time_lock);

	if (source_should_writeback_rtc(source, quality)) {
		writeback_rtc_if_needed(source, unix_time_s);
	}

	return 0;
}

static uint32_t next_retry_delay_ms(void)
{
	uint32_t delay;

	k_mutex_lock(&time_lock, K_FOREVER);
	delay = status.retry_delay_ms;
	if (delay == 0U) {
		delay = NTP_RETRY_INITIAL_MS;
	}
	status.retry_delay_ms = MIN(delay * 2U, NTP_RETRY_MAX_MS);
	k_mutex_unlock(&time_lock);

	return delay;
}

static void schedule_sync(uint32_t delay_ms)
{
	k_work_reschedule(&sync_timer_work, K_MSEC(delay_ms));
}

static void sync_timer_work_handler(struct k_work *work)
{
	bool automatic = true;

	ARG_UNUSED(work);
	(void)k_msgq_put(&sync_request_msgq, &automatic, K_NO_WAIT);
}

static int sync_ntp_once(void)
{
	struct sntp_time sntp_ts;
	int rc;

	rc = sntp_simple(CONFIG_TIME_SERVICE_NTP_SERVER, NTP_TIMEOUT_MS,
			 &sntp_ts);
	if (rc != 0) {
		k_mutex_lock(&time_lock, K_FOREVER);
		status.fail_count++;
		k_mutex_unlock(&time_lock);
		return rc;
	}

	rc = time_service_update_from_source(TIME_SERVICE_SOURCE_NTP,
					     TIME_SERVICE_QUALITY_SYNCED,
					     (int64_t)sntp_ts.seconds);
	if (rc != 0) {
		k_mutex_lock(&time_lock, K_FOREVER);
		status.fail_count++;
		k_mutex_unlock(&time_lock);
		return rc;
	}

	LOG_INF("NTP synced from %s: %lld",
		CONFIG_TIME_SERVICE_NTP_SERVER,
		(long long)sntp_ts.seconds);
	return 0;
}

static void network_event_handler(enum network_service_event event,
				  const struct network_service_status *net_status,
				  void *user_data)
{
	ARG_UNUSED(net_status);
	ARG_UNUSED(user_data);

	if (event == NETWORK_SERVICE_EVENT_READY) {
		bool automatic = true;

		(void)k_msgq_put(&sync_request_msgq, &automatic, K_NO_WAIT);
	}
}

static void time_service_thread(void *arg1, void *arg2, void *arg3)
{
	int rc;
	uint32_t delay;
	bool automatic;
	enum time_service_correction_mode mode;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msgq_get(&sync_request_msgq, &automatic, K_FOREVER);

		mode = time_service_correction_mode_get();
		if (automatic && mode != TIME_SERVICE_CORRECTION_MODE_AUTO) {
			continue;
		}

		if (!network_service_is_ready()) {
			continue;
		}

		rc = sync_ntp_once();
		if (rc == 0) {
			schedule_sync(NTP_RESYNC_INTERVAL_MS);
			continue;
		}

		delay = next_retry_delay_ms();
		LOG_WRN("NTP sync failed: %d, retry in %u ms", rc, delay);
		schedule_sync(delay);
	}
}

int time_service_init(void)
{
	int64_t rtc_time_s;
	int rc;

	if (initialized) {
		return 0;
	}

	k_work_init_delayable(&sync_timer_work, sync_timer_work_handler);
	(void)rtc_time_provider_init();

	k_mutex_lock(&time_lock, K_FOREVER);
	snprintk(status.ntp_server, sizeof(status.ntp_server), "%s",
		 CONFIG_TIME_SERVICE_NTP_SERVER);
	status.uptime_ms = k_uptime_get();
	status.rtc_available = rtc_time_provider_is_available();
	status.rtc_valid = rtc_time_provider_is_valid();
	k_mutex_unlock(&time_lock);

	rc = rtc_time_provider_get_unix_time(&rtc_time_s);
	if (rc == 0) {
		rc = time_service_update_from_source(TIME_SERVICE_SOURCE_RTC,
						     TIME_SERVICE_QUALITY_ESTIMATED,
						     rtc_time_s);
		if (rc == 0) {
			LOG_INF("System time restored from RTC: %lld",
				(long long)rtc_time_s);
		}
	} else {
		k_mutex_lock(&time_lock, K_FOREVER);
		status.rtc_available = rtc_time_provider_is_available();
		status.rtc_valid = false;
		status.rtc_last_error = rc;
		k_mutex_unlock(&time_lock);
		LOG_ERR("RTC rejected as system time source: %d", rc);
	}

	(void)network_service_register_handler(network_event_handler, NULL);

	initialized = true;
	return 0;
}

int time_service_sync_now(void)
{
	bool automatic = false;

	return k_msgq_put(&sync_request_msgq, &automatic, K_NO_WAIT);
}

int time_service_set_unix_time(int64_t unix_time_s)
{
	return time_service_update_from_source(TIME_SERVICE_SOURCE_MANUAL,
					       TIME_SERVICE_QUALITY_SYNCED,
					       unix_time_s);
}

int time_service_correction_mode_set(enum time_service_correction_mode mode)
{
	if (mode != TIME_SERVICE_CORRECTION_MODE_AUTO &&
	    mode != TIME_SERVICE_CORRECTION_MODE_MANUAL) {
		return -EINVAL;
	}

	k_mutex_lock(&time_lock, K_FOREVER);
	status.correction_mode = mode;
	k_mutex_unlock(&time_lock);

	if (mode == TIME_SERVICE_CORRECTION_MODE_AUTO &&
	    network_service_is_ready()) {
		bool automatic = true;

		(void)k_msgq_put(&sync_request_msgq, &automatic, K_NO_WAIT);
	}

	return 0;
}

enum time_service_correction_mode time_service_correction_mode_get(void)
{
	enum time_service_correction_mode mode;

	k_mutex_lock(&time_lock, K_FOREVER);
	mode = status.correction_mode;
	k_mutex_unlock(&time_lock);

	return mode;
}

bool time_service_is_time_valid(void)
{
	bool valid;

	k_mutex_lock(&time_lock, K_FOREVER);
	valid = status.wall_time_valid;
	k_mutex_unlock(&time_lock);

	return valid;
}

int64_t time_service_unix_time_get(void)
{
	struct timespec ts;

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &ts) != 0) {
		return 0;
	}

	return ts.tv_sec;
}

int time_service_format_local_iso8601_from_unix(int64_t unix_time_s,
						char *buf, size_t len)
{
	time_t local_time_s;
	struct tm tm_local;
	int32_t offset_minutes = CONFIG_LOCAL_TIMEZONE_OFFSET_MINUTES;
	char offset_sign = '+';
	uint32_t offset_abs;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	local_time_s = (time_t)(unix_time_s + LOCAL_TIME_OFFSET_SECONDS);
	if (gmtime_r(&local_time_s, &tm_local) == NULL) {
		return -EINVAL;
	}

	if (offset_minutes < 0) {
		offset_sign = '-';
		offset_abs = (uint32_t)-offset_minutes;
	} else {
		offset_abs = (uint32_t)offset_minutes;
	}

	written = snprintk(buf, len,
			   "%04u-%02u-%02uT%02u:%02u:%02u%c%02u:%02u",
			   tm_local.tm_year + 1900, tm_local.tm_mon + 1,
			   tm_local.tm_mday, tm_local.tm_hour, tm_local.tm_min,
			   tm_local.tm_sec, offset_sign, offset_abs / 60U,
			   offset_abs % 60U);
	if (written < 0) {
		return written;
	}

	return (size_t)written >= len ? -ENOMEM : 0;
}

void time_service_get_status(struct time_service_status *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&time_lock, K_FOREVER);
	*out = status;
	out->uptime_ms = k_uptime_get();
	out->unix_time_s = time_service_unix_time_get();
	out->rtc_available = rtc_time_provider_is_available();
	out->rtc_valid = rtc_time_provider_is_valid();
	k_mutex_unlock(&time_lock);
}

int time_service_format_iso8601(char *buf, size_t len)
{
	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	if (!time_service_is_time_valid()) {
		return -ENODATA;
	}

	return time_service_format_local_iso8601_from_unix(
		time_service_unix_time_get(), buf, len);
}

#ifdef CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP
static int time_service_log_timestamp_format(
	const struct log_output *output, const log_timestamp_t timestamp,
	const log_timestamp_printer_t printer)
{
	char iso_time[32];
	int64_t unix_time_s = (int64_t)(timestamp / 1000ULL);
	uint32_t ms = timestamp % 1000ULL;
	int rc;

	BUILD_ASSERT(IS_ENABLED(CONFIG_LOG_TIMESTAMP_USE_REALTIME),
		     "Local log timestamp formatter expects realtime timestamps");

	rc = time_service_format_local_iso8601_from_unix(unix_time_s,
							 iso_time,
							 sizeof(iso_time));
	if (rc != 0) {
		return printer(output, "[%llu] ", timestamp / 1000ULL);
	}

	ARG_UNUSED(ms);
	iso_time[19] = '\0';
	return printer(output, "[%s] ", iso_time);
}

static int time_service_log_timestamp_init(void)
{
	log_custom_timestamp_set(time_service_log_timestamp_format);
	return 0;
}

SYS_INIT(time_service_log_timestamp_init, APPLICATION, 1);
#endif

K_THREAD_DEFINE(time_service_tid, TIME_SERVICE_STACK_SIZE,
		time_service_thread, NULL, NULL, NULL,
		TIME_SERVICE_PRIORITY, 0, 0);
