#ifndef ENCODER_SAMPLE_SERVICE_H_
#define ENCODER_SAMPLE_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "idecoder_encoder_modbus.h"

enum encoder_sample_service_status {
	ENCODER_SAMPLE_SERVICE_STATUS_ONLINE = BIT(0),
};

struct encoder_sample_service_config {
	const char *name;
	const char *iface_name;
	uint8_t unit_id;
	uint32_t baud;
	uint32_t rx_timeout_us;
	uint32_t period_ms;
	uint32_t start_delay_ms;
	uint16_t start_addr;
};

struct encoder_sample_service_sample {
	bool online;
	uint32_t seq;
	uint32_t status;
	uint16_t raw_regs[2];
	uint8_t raw_reg_count;
	uint16_t turn_count;
	uint16_t single_value;
	int64_t sample_uptime_ms;
	uint32_t read_duration_us;
	int last_error;
};

struct encoder_sample_service_stats {
	uint32_t success_count;
	uint32_t error_count;
	uint32_t consecutive_error_count;
	uint32_t max_consecutive_error_count;
	int last_error;
	int last_fault_error;
	int64_t last_error_uptime_ms;
	int64_t last_success_uptime_ms;
	uint64_t success_total_time_us;
	uint32_t success_avg_time_us;
	uint32_t success_max_time_us;
};

struct encoder_sample_service;

typedef void (*encoder_sample_service_callback_t)(
	struct encoder_sample_service *service, int err,
	const struct encoder_sample_service_sample *sample, void *user_data);

struct encoder_sample_service {
	const struct encoder_sample_service_config *config;
	struct idecoder_encoder_modbus_config modbus_config;
	struct idecoder_encoder_modbus_client modbus_client;
	struct encoder_sample_service_sample latest_sample;
	struct encoder_sample_service_stats latest_stats;
	struct k_spinlock lock;
	struct k_thread thread;
	K_KERNEL_STACK_MEMBER(stack,
		CONFIG_ENCODER_SAMPLE_SERVICE_THREAD_STACK_SIZE);
	encoder_sample_service_callback_t callback;
	void *user_data;
	bool started;
	bool client_ready;
};

int encoder_sample_service_start(
	struct encoder_sample_service *service,
	const struct encoder_sample_service_config *config,
	encoder_sample_service_callback_t callback, void *user_data);
void encoder_sample_service_get_latest(
	struct encoder_sample_service *service,
	struct encoder_sample_service_sample *sample);
void encoder_sample_service_get_stats(
	struct encoder_sample_service *service,
	struct encoder_sample_service_stats *stats);

#endif /* ENCODER_SAMPLE_SERVICE_H_ */
