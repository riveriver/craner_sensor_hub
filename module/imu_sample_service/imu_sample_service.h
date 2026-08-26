#ifndef IMU_SAMPLE_SERVICE_H_
#define IMU_SAMPLE_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "imu_sample_backend.h"

enum imu_sample_service_status {
	IMU_SAMPLE_SERVICE_STATUS_ONLINE = BIT(0),
};

struct imu_sample_service_config {
	const char *name;
	const char *iface_name;
	uint32_t period_ms;
	uint32_t start_delay_ms;
	const struct imu_sample_backend *backend;
	void *backend_client;
	const void *backend_config;
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
