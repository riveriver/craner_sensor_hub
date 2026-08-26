#ifndef IMU_SAMPLE_BACKEND_H_
#define IMU_SAMPLE_BACKEND_H_

#include <stdbool.h>
#include <stdint.h>

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

struct imu_sample_backend {
	const char *name;
	int (*init)(void *client, const void *config);
	int (*fetch)(void *client, struct imu_sample_service_sample *sample);
	void (*reset)(void *client);
};

#endif /* IMU_SAMPLE_BACKEND_H_ */
