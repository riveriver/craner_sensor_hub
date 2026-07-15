#include "rtc_trust_store.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(rtc_trust_store, CONFIG_LOG_DEFAULT_LEVEL);

#define BBRAM_NODE DT_NODELABEL(bbram)
#define RTC_TRUST_MAGIC 0x43525452U /* CRTR */
#define RTC_TRUST_VERSION 1U
#define RTC_TRUST_FLAG_VALID BIT(0)

struct rtc_trust_record {
	uint32_t magic;
	uint16_t version;
	uint16_t flags;
	uint32_t source;
	uint32_t write_count;
	uint32_t boot_count;
	int64_t trusted_unix_time_s;
	int64_t write_uptime_ms;
	uint32_t crc32;
};

static const struct device *const bbram_dev = DEVICE_DT_GET_OR_NULL(BBRAM_NODE);
static K_MUTEX_DEFINE(trust_lock);

static struct rtc_trust_store_status status;
static bool initialized;

static uint32_t record_crc(const struct rtc_trust_record *record)
{
	return crc32_ieee((const uint8_t *)record,
			  offsetof(struct rtc_trust_record, crc32));
}

static int read_record(struct rtc_trust_record *record)
{
	if (record == NULL) {
		return -EINVAL;
	}

	if (bbram_dev == NULL || !device_is_ready(bbram_dev)) {
		return -ENODEV;
	}

	return bbram_read(bbram_dev, 0, sizeof(*record), (uint8_t *)record);
}

static int write_record(const struct rtc_trust_record *record)
{
	if (record == NULL) {
		return -EINVAL;
	}

	if (bbram_dev == NULL || !device_is_ready(bbram_dev)) {
		return -ENODEV;
	}

	return bbram_write(bbram_dev, 0, sizeof(*record),
			   (const uint8_t *)record);
}

static int validate_record(const struct rtc_trust_record *record)
{
	if (record == NULL) {
		return -EINVAL;
	}

	if (record->magic != RTC_TRUST_MAGIC ||
	    record->version != RTC_TRUST_VERSION ||
	    (record->flags & RTC_TRUST_FLAG_VALID) == 0U) {
		return -ENODATA;
	}

	if (record->crc32 != record_crc(record)) {
		return -EBADMSG;
	}

	if (record->source != RTC_TRUST_SOURCE_MANUAL &&
	    record->source != RTC_TRUST_SOURCE_NTP &&
	    record->source != RTC_TRUST_SOURCE_GPS) {
		return -ENODATA;
	}

	return 0;
}

static void update_status_from_record(int rc,
				      const struct rtc_trust_record *record)
{
	status.available = bbram_dev != NULL;
	status.ready = bbram_dev != NULL && device_is_ready(bbram_dev);
	status.valid = rc == 0;
	status.last_error = rc;

	if (rc == 0 && record != NULL) {
		status.source = (enum rtc_trust_source)record->source;
		status.trusted_unix_time_s = record->trusted_unix_time_s;
		status.write_uptime_ms = record->write_uptime_ms;
		status.write_count = record->write_count;
		status.boot_count = record->boot_count;
	}
}

const char *rtc_trust_source_name(enum rtc_trust_source source)
{
	switch (source) {
	case RTC_TRUST_SOURCE_NONE:
		return "none";
	case RTC_TRUST_SOURCE_MANUAL:
		return "manual";
	case RTC_TRUST_SOURCE_NTP:
		return "ntp";
	case RTC_TRUST_SOURCE_GPS:
		return "gps";
	default:
		return "unknown";
	}
}

int rtc_trust_store_init(void)
{
	struct rtc_trust_record record;
	int rc;

	if (initialized) {
		return 0;
	}

	rc = read_record(&record);
	if (rc == 0) {
		rc = validate_record(&record);
	}

	k_mutex_lock(&trust_lock, K_FOREVER);
	update_status_from_record(rc, rc == 0 ? &record : NULL);
	k_mutex_unlock(&trust_lock);

	if (rc == 0) {
		LOG_INF("RTC trust record valid: source=%s time=%lld",
			rtc_trust_source_name((enum rtc_trust_source)record.source),
			(long long)record.trusted_unix_time_s);
	} else {
		LOG_WRN("RTC trust record invalid: %d", rc);
	}

	initialized = true;
	return 0;
}

bool rtc_trust_store_is_valid(void)
{
	bool valid;

	k_mutex_lock(&trust_lock, K_FOREVER);
	valid = status.valid;
	k_mutex_unlock(&trust_lock);

	return valid;
}

int rtc_trust_store_mark_valid(enum rtc_trust_source source,
			       int64_t trusted_unix_time_s)
{
	struct rtc_trust_record old_record;
	struct rtc_trust_record record;
	uint32_t write_count = 0U;
	uint32_t boot_count = 0U;
	int rc;

	if (source != RTC_TRUST_SOURCE_MANUAL &&
	    source != RTC_TRUST_SOURCE_NTP &&
	    source != RTC_TRUST_SOURCE_GPS) {
		return -EINVAL;
	}

	rc = read_record(&old_record);
	if (rc == 0 && validate_record(&old_record) == 0) {
		write_count = old_record.write_count;
		boot_count = old_record.boot_count;
	}

	memset(&record, 0, sizeof(record));
	record.magic = RTC_TRUST_MAGIC;
	record.version = RTC_TRUST_VERSION;
	record.flags = RTC_TRUST_FLAG_VALID;
	record.source = source;
	record.write_count = write_count + 1U;
	record.boot_count = boot_count;
	record.trusted_unix_time_s = trusted_unix_time_s;
	record.write_uptime_ms = k_uptime_get();
	record.crc32 = record_crc(&record);

	rc = write_record(&record);

	k_mutex_lock(&trust_lock, K_FOREVER);
	update_status_from_record(rc, rc == 0 ? &record : NULL);
	k_mutex_unlock(&trust_lock);

	if (rc == 0) {
		LOG_INF("RTC trust record updated: source=%s time=%lld",
			rtc_trust_source_name(source),
			(long long)trusted_unix_time_s);
	} else {
		LOG_ERR("Failed to update RTC trust record: %d", rc);
	}

	return rc;
}

int rtc_trust_store_clear(void)
{
	struct rtc_trust_record record;
	int rc;

	memset(&record, 0, sizeof(record));
	rc = write_record(&record);

	k_mutex_lock(&trust_lock, K_FOREVER);
	update_status_from_record(rc == 0 ? -ENODATA : rc, NULL);
	k_mutex_unlock(&trust_lock);

	if (rc == 0) {
		LOG_WRN("RTC trust record cleared");
	}

	return rc;
}

void rtc_trust_store_get_status(struct rtc_trust_store_status *out)
{
	struct rtc_trust_record record;
	int rc;

	if (out == NULL) {
		return;
	}

	rc = read_record(&record);
	if (rc == 0) {
		rc = validate_record(&record);
	}

	k_mutex_lock(&trust_lock, K_FOREVER);
	update_status_from_record(rc, rc == 0 ? &record : NULL);
	*out = status;
	k_mutex_unlock(&trust_lock);
}
