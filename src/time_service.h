#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum time_service_source {
	TIME_SERVICE_SOURCE_NONE,
	TIME_SERVICE_SOURCE_BOOT_TICK,
	TIME_SERVICE_SOURCE_RTC,
	TIME_SERVICE_SOURCE_MANUAL,
	TIME_SERVICE_SOURCE_NTP,
	TIME_SERVICE_SOURCE_GPS,
};

enum time_service_quality {
	TIME_SERVICE_QUALITY_INVALID,
	TIME_SERVICE_QUALITY_MONOTONIC_ONLY,
	TIME_SERVICE_QUALITY_ESTIMATED,
	TIME_SERVICE_QUALITY_SYNCED,
	TIME_SERVICE_QUALITY_HIGH_PRECISION,
};

enum time_service_correction_mode {
	TIME_SERVICE_CORRECTION_MODE_AUTO,
	TIME_SERVICE_CORRECTION_MODE_MANUAL,
};

struct time_service_status {
	bool wall_time_valid;
	enum time_service_source active_source;
	enum time_service_quality quality;
	enum time_service_correction_mode correction_mode;
	int64_t unix_time_s;
	int64_t uptime_ms;
	int64_t last_sync_uptime_ms;
	int64_t last_rtc_writeback_unix_time_s;
	int64_t last_rtc_writeback_uptime_ms;
	uint32_t sync_count;
	uint32_t fail_count;
	uint32_t retry_delay_ms;
	bool rtc_available;
	bool rtc_valid;
	int rtc_last_error;
	char ntp_server[64];
};

int time_service_init(void);
int time_service_sync_now(void);
int time_service_set_unix_time(int64_t unix_time_s);
int time_service_correction_mode_set(enum time_service_correction_mode mode);
enum time_service_correction_mode time_service_correction_mode_get(void);
bool time_service_is_time_valid(void);
int64_t time_service_unix_time_get(void);
void time_service_get_status(struct time_service_status *out);
int time_service_format_local_iso8601_from_unix(int64_t unix_time_s,
						char *buf, size_t len);
int time_service_format_iso8601(char *buf, size_t len);
int time_service_update_from_source(enum time_service_source source,
				    enum time_service_quality quality,
				    int64_t unix_time_s);
const char *time_service_source_name(enum time_service_source source);
const char *time_service_quality_name(enum time_service_quality quality);
const char *time_service_correction_mode_name(
	enum time_service_correction_mode mode);

#endif /* TIME_SERVICE_H */
