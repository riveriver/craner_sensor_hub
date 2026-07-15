#include "rtc_time_provider.h"
#include "rtc_trust_store.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(rtc_time_provider, CONFIG_LOG_DEFAULT_LEVEL);

#define RTC_NODE DT_NODELABEL(rtc)
#define RTC_MIN_VALID_UNIX_TIME CONFIG_CRANER_TIME_SERVICE_RTC_MIN_VALID_UNIX_TIME
#define RTC_MAX_VALID_UNIX_TIME 4102444799LL

static const struct device *const rtc_dev = DEVICE_DT_GET_OR_NULL(RTC_NODE);
static K_MUTEX_DEFINE(rtc_lock);

static struct rtc_time_provider_status status;
static bool initialized;

static bool unix_time_is_reasonable(int64_t unix_time_s)
{
	return unix_time_s >= RTC_MIN_VALID_UNIX_TIME &&
	       unix_time_s <= RTC_MAX_VALID_UNIX_TIME;
}

static int rtc_time_to_unix(const struct rtc_time *rtc_time,
			    int64_t *unix_time_s)
{
	struct tm tm_time;
	int64_t converted;

	if (rtc_time == NULL || unix_time_s == NULL) {
		return -EINVAL;
	}

	memset(&tm_time, 0, sizeof(tm_time));
	tm_time.tm_sec = rtc_time->tm_sec;
	tm_time.tm_min = rtc_time->tm_min;
	tm_time.tm_hour = rtc_time->tm_hour;
	tm_time.tm_mday = rtc_time->tm_mday;
	tm_time.tm_mon = rtc_time->tm_mon;
	tm_time.tm_year = rtc_time->tm_year;
	tm_time.tm_wday = rtc_time->tm_wday;
	tm_time.tm_yday = rtc_time->tm_yday;
	tm_time.tm_isdst = -1;

	converted = timeutil_timegm64(&tm_time);
	if (!unix_time_is_reasonable(converted)) {
		return -ENODATA;
	}

	*unix_time_s = converted;
	return 0;
}

static int unix_time_to_rtc_time(int64_t unix_time_s,
				 struct rtc_time *rtc_time)
{
	time_t unix_time;
	struct tm tm_time;

	if (rtc_time == NULL) {
		return -EINVAL;
	}

	if (!unix_time_is_reasonable(unix_time_s)) {
		return -EINVAL;
	}

	unix_time = (time_t)unix_time_s;
	if (gmtime_r(&unix_time, &tm_time) == NULL) {
		return -EINVAL;
	}

	memset(rtc_time, 0, sizeof(*rtc_time));
	rtc_time->tm_sec = tm_time.tm_sec;
	rtc_time->tm_min = tm_time.tm_min;
	rtc_time->tm_hour = tm_time.tm_hour;
	rtc_time->tm_mday = tm_time.tm_mday;
	rtc_time->tm_mon = tm_time.tm_mon;
	rtc_time->tm_year = tm_time.tm_year;
	rtc_time->tm_wday = tm_time.tm_wday;
	rtc_time->tm_yday = tm_time.tm_yday;
	rtc_time->tm_isdst = -1;
	rtc_time->tm_nsec = 0;

	return 0;
}

int rtc_time_provider_init(void)
{
	int64_t unix_time_s = 0;
	int rc;

	if (initialized) {
		return 0;
	}

	k_mutex_lock(&rtc_lock, K_FOREVER);

	status.available = rtc_dev != NULL;
	status.ready = rtc_dev != NULL && device_is_ready(rtc_dev);
	status.valid = false;
	status.last_error = 0;

	if (!status.available) {
		status.last_error = -ENODEV;
		k_mutex_unlock(&rtc_lock);
		initialized = true;
		LOG_WRN("RTC device is not available");
		return -ENODEV;
	}

	if (!status.ready) {
		status.last_error = -ENODEV;
		k_mutex_unlock(&rtc_lock);
		initialized = true;
		LOG_WRN("RTC device is not ready");
		return -ENODEV;
	}

	k_mutex_unlock(&rtc_lock);

	(void)rtc_trust_store_init();

	rc = rtc_time_provider_get_unix_time(&unix_time_s);
	if (rc == 0) {
		LOG_INF("RTC time is trusted and valid: %lld",
			(long long)unix_time_s);
	} else if (rc == -ENODATA) {
		LOG_WRN("RTC time is invalid: not set, out of range, or not trusted");
	} else {
		LOG_ERR("RTC is invalid and will not be used as system time: %d",
			rc);
	}

	initialized = true;
	return 0;
}

bool rtc_time_provider_is_available(void)
{
	bool available;

	k_mutex_lock(&rtc_lock, K_FOREVER);
	available = status.available && status.ready;
	k_mutex_unlock(&rtc_lock);

	return available;
}

bool rtc_time_provider_is_valid(void)
{
	bool valid;

	k_mutex_lock(&rtc_lock, K_FOREVER);
	valid = status.valid;
	k_mutex_unlock(&rtc_lock);

	return valid;
}

int rtc_time_provider_get_unix_time(int64_t *unix_time_s)
{
	struct rtc_time rtc_time;
	struct rtc_trust_store_status trust_status = { 0 };
	int64_t converted = 0;
	int rc;
	int trust_rc = 0;
	bool range_ok = false;

	if (unix_time_s == NULL) {
		return -EINVAL;
	}

	if (rtc_dev == NULL || !device_is_ready(rtc_dev)) {
		rc = -ENODEV;
		goto out_status;
	}

	rc = rtc_get_time(rtc_dev, &rtc_time);
	if (rc != 0) {
		goto out_status;
	}

	rc = rtc_time_to_unix(&rtc_time, &converted);
	if (rc == 0) {
		range_ok = true;
		rtc_trust_store_get_status(&trust_status);
		trust_rc = trust_status.valid ? 0 : trust_status.last_error;
		if (trust_rc != 0) {
			rc = trust_rc != 0 ? trust_rc : -ENODATA;
		} else if (converted < trust_status.trusted_unix_time_s) {
			rc = -ERANGE;
		} else {
			*unix_time_s = converted;
		}
	}

out_status:
	k_mutex_lock(&rtc_lock, K_FOREVER);
	status.available = rtc_dev != NULL;
	status.ready = rtc_dev != NULL && device_is_ready(rtc_dev);
	status.valid = rc == 0;
	status.time_range_valid = range_ok;
	status.trust_valid = trust_rc == 0 && range_ok;
	status.last_error = rc;
	status.trust_error = trust_rc;
	if (rc == 0) {
		status.trust_source = trust_status.source;
	} else if (trust_rc != 0) {
		status.trust_source = RTC_TRUST_SOURCE_NONE;
	}
	if (range_ok) {
		status.unix_time_s = converted;
	}
	k_mutex_unlock(&rtc_lock);

	return rc;
}

int rtc_time_provider_set_unix_time(int64_t unix_time_s)
{
	return rtc_time_provider_set_trusted_unix_time(unix_time_s,
						      RTC_TRUST_SOURCE_MANUAL);
}

int rtc_time_provider_set_trusted_unix_time(int64_t unix_time_s,
					    enum rtc_trust_source source)
{
	struct rtc_time rtc_time;
	int rc;
	int trust_rc = 0;

	if (rtc_dev == NULL || !device_is_ready(rtc_dev)) {
		rc = -ENODEV;
		goto out_status;
	}

	rc = unix_time_to_rtc_time(unix_time_s, &rtc_time);
	if (rc != 0) {
		goto out_status;
	}

	rc = rtc_set_time(rtc_dev, &rtc_time);
	if (rc == 0) {
		trust_rc = rtc_trust_store_mark_valid(source, unix_time_s);
		if (trust_rc != 0) {
			rc = trust_rc;
		}
	}

out_status:
	k_mutex_lock(&rtc_lock, K_FOREVER);
	status.available = rtc_dev != NULL;
	status.ready = rtc_dev != NULL && device_is_ready(rtc_dev);
	status.valid = rc == 0 ? true : status.valid;
	status.time_range_valid = rc == 0 ? true : status.time_range_valid;
	status.trust_valid = rc == 0 ? true : status.trust_valid;
	status.last_error = rc;
	status.trust_error = trust_rc;
	if (rc == 0) {
		status.unix_time_s = unix_time_s;
		status.last_set_unix_time_s = unix_time_s;
		status.last_set_uptime_ms = k_uptime_get();
		status.trust_source = source;
	}
	k_mutex_unlock(&rtc_lock);

	if (rc == 0) {
		LOG_INF("RTC updated: %lld", (long long)unix_time_s);
	}

	return rc;
}

int rtc_time_provider_clear_trust(void)
{
	int rc;

	rc = rtc_trust_store_clear();

	k_mutex_lock(&rtc_lock, K_FOREVER);
	status.valid = false;
	status.trust_valid = false;
	status.trust_error = rc == 0 ? -ENODATA : rc;
	status.trust_source = RTC_TRUST_SOURCE_NONE;
	status.last_error = status.trust_error;
	k_mutex_unlock(&rtc_lock);

	return rc;
}

void rtc_time_provider_get_status(struct rtc_time_provider_status *out)
{
	int64_t unix_time_s;
	struct rtc_trust_store_status trust_status;

	if (out == NULL) {
		return;
	}

	(void)rtc_time_provider_get_unix_time(&unix_time_s);
	rtc_trust_store_get_status(&trust_status);

	k_mutex_lock(&rtc_lock, K_FOREVER);
	status.trust_valid = trust_status.valid;
	status.trust_error = trust_status.last_error;
	status.trust_source = trust_status.source;
	*out = status;
	k_mutex_unlock(&rtc_lock);
}
