#include "encoder_sample_service_internal.h"

#include <zephyr/sys/util.h>

void encoder_sample_service_update_success(
	struct encoder_sample_service *service,
	const struct encoder_sample_service_sample *sample,
	uint32_t read_duration_us,
	struct encoder_sample_service_sample *notify_sample)
{
	k_spinlock_key_t key;
	int64_t now_ms;
#if defined(CONFIG_ENCODER_SAMPLE_SERVICE_STATS)
	uint32_t success_count;
#endif

	key = k_spin_lock(&service->lock);

	now_ms = k_uptime_get();
#if defined(CONFIG_ENCODER_SAMPLE_SERVICE_STATS)
	success_count = service->latest_stats.success_count + 1U;
	service->latest_stats.success_count = success_count;
	service->latest_stats.consecutive_error_count = 0;
	service->latest_stats.last_error = 0;
	service->latest_stats.last_success_uptime_ms = now_ms;
	service->latest_stats.success_total_time_us += read_duration_us;
	service->latest_stats.success_avg_time_us =
		(uint32_t)(service->latest_stats.success_total_time_us /
			   success_count);
	if (read_duration_us > service->latest_stats.success_max_time_us) {
		service->latest_stats.success_max_time_us = read_duration_us;
	}
#endif

	service->latest_sample.online = true;
	service->latest_sample.seq++;
	service->latest_sample.status = ENCODER_SAMPLE_SERVICE_STATUS_ONLINE;
	service->latest_sample.raw_reg_count = sample->raw_reg_count;
	for (uint8_t i = 0U;
	     i < sample->raw_reg_count &&
	     i < ARRAY_SIZE(service->latest_sample.raw_regs);
	     i++) {
		service->latest_sample.raw_regs[i] = sample->raw_regs[i];
	}
	service->latest_sample.turn_count = sample->turn_count;
	service->latest_sample.single_value = sample->single_value;
	service->latest_sample.sample_uptime_ms = now_ms;
	service->latest_sample.read_duration_us = read_duration_us;
	service->latest_sample.last_error = 0;
	*notify_sample = service->latest_sample;

	k_spin_unlock(&service->lock, key);
}

void encoder_sample_service_update_error(
	struct encoder_sample_service *service, int err)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&service->lock);
	service->latest_sample.online = false;
	service->latest_sample.status &=
		~ENCODER_SAMPLE_SERVICE_STATUS_ONLINE;
	service->latest_sample.last_error = err;
#if defined(CONFIG_ENCODER_SAMPLE_SERVICE_STATS)
	service->latest_stats.error_count++;
	service->latest_stats.consecutive_error_count++;
	service->latest_stats.last_error = err;
	service->latest_stats.last_fault_error = err;
	service->latest_stats.last_error_uptime_ms = k_uptime_get();
	if (service->latest_stats.consecutive_error_count >
	    service->latest_stats.max_consecutive_error_count) {
		service->latest_stats.max_consecutive_error_count =
			service->latest_stats.consecutive_error_count;
	}
#endif
	k_spin_unlock(&service->lock, key);
}
