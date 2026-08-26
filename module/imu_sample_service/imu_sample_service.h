#ifndef IMU_SAMPLE_SERVICE_H_
#define IMU_SAMPLE_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "wit_imu_modbus.h"

enum imu_sample_service_status {
	IMU_SAMPLE_SERVICE_STATUS_ONLINE = BIT(0),
};

struct imu_sample_service_config {
	const char *name;
	const char *iface_name;
	enum wit_imu_modbus_model model;
	uint8_t unit_id;
	uint32_t baud;
	uint32_t rx_timeout_us;
	uint32_t period_ms;
	uint32_t start_delay_ms;
};

struct imu_sample_service_sample {
	bool online;
	uint32_t seq;
	uint32_t status;
	uint16_t raw_regs[6];
	uint8_t raw_reg_count;
	int32_t roll_raw;
	int32_t pitch_raw;
	int32_t yaw_raw;
	int32_t roll_mdeg;
	int32_t pitch_mdeg;
	int32_t yaw_mdeg;
	int64_t sample_uptime_ms;
	uint32_t read_duration_us;
	int last_error;
};

struct imu_sample_service_stats {
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

struct imu_sample_service;

typedef void (*imu_sample_service_callback_t)(
	struct imu_sample_service *service, int err,
	const struct imu_sample_service_sample *sample, void *user_data);

struct imu_sample_service {
	const struct imu_sample_service_config *config;
	struct wit_imu_modbus_config modbus_config;
	struct wit_imu_modbus_client modbus_client;
	struct imu_sample_service_sample latest_sample;
	struct imu_sample_service_stats latest_stats;
	struct k_spinlock lock;
	struct k_thread thread;
	K_KERNEL_STACK_MEMBER(stack, CONFIG_IMU_SAMPLE_THREAD_STACK_SIZE);
	imu_sample_service_callback_t callback;
	void *user_data;
	bool started;
	bool client_ready;
};

int imu_sample_service_start(struct imu_sample_service *service,
			     const struct imu_sample_service_config *config,
			     imu_sample_service_callback_t callback,
			     void *user_data);
void imu_sample_service_get_latest(struct imu_sample_service *service,
				   struct imu_sample_service_sample *sample);
void imu_sample_service_get_stats(struct imu_sample_service *service,
				  struct imu_sample_service_stats *stats);

#endif /* IMU_SAMPLE_SERVICE_H_ */
