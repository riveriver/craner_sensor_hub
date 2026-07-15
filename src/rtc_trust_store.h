#ifndef RTC_TRUST_STORE_H
#define RTC_TRUST_STORE_H

#include <stdbool.h>
#include <stdint.h>

enum rtc_trust_source {
	RTC_TRUST_SOURCE_NONE,
	RTC_TRUST_SOURCE_MANUAL,
	RTC_TRUST_SOURCE_NTP,
	RTC_TRUST_SOURCE_GPS,
};

struct rtc_trust_store_status {
	bool available;
	bool ready;
	bool valid;
	int last_error;
	enum rtc_trust_source source;
	int64_t trusted_unix_time_s;
	int64_t write_uptime_ms;
	uint32_t write_count;
	uint32_t boot_count;
};

int rtc_trust_store_init(void);
bool rtc_trust_store_is_valid(void);
int rtc_trust_store_mark_valid(enum rtc_trust_source source,
			       int64_t trusted_unix_time_s);
int rtc_trust_store_clear(void);
void rtc_trust_store_get_status(struct rtc_trust_store_status *out);
const char *rtc_trust_source_name(enum rtc_trust_source source);

#endif /* RTC_TRUST_STORE_H */
