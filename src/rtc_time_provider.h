#ifndef RTC_TIME_PROVIDER_H
#define RTC_TIME_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "rtc_trust_store.h"

struct rtc_time_provider_status {
	bool available;
	bool ready;
	bool valid;
	bool time_range_valid;
	bool trust_valid;
	int last_error;
	int trust_error;
	enum rtc_trust_source trust_source;
	int64_t unix_time_s;
	int64_t last_set_unix_time_s;
	int64_t last_set_uptime_ms;
};

int rtc_time_provider_init(void);
bool rtc_time_provider_is_available(void);
bool rtc_time_provider_is_valid(void);
int rtc_time_provider_get_unix_time(int64_t *unix_time_s);
int rtc_time_provider_set_unix_time(int64_t unix_time_s);
int rtc_time_provider_set_trusted_unix_time(int64_t unix_time_s,
					    enum rtc_trust_source source);
int rtc_time_provider_clear_trust(void);
void rtc_time_provider_get_status(struct rtc_time_provider_status *out);

#endif /* RTC_TIME_PROVIDER_H */
