#ifndef WIT_IMU_MODBUS_H_
#define WIT_IMU_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

enum wit_imu_modbus_model {
	WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION,
	WIT_IMU_MODBUS_MODEL_HIGH_PRECISION,
};

struct wit_imu_modbus_config {
	const char *iface_name;
	enum wit_imu_modbus_model model;
	uint8_t unit_id;
	uint32_t baud;
	uint32_t rx_timeout_us;
};

struct wit_imu_modbus_client {
	const struct wit_imu_modbus_config *config;
	int iface;
	bool ready;
};

struct wit_imu_modbus_sample {
	uint16_t raw_regs[6];
	uint8_t raw_reg_count;
	int32_t roll_raw;
	int32_t pitch_raw;
	int32_t yaw_raw;
	int32_t roll_mdeg;
	int32_t pitch_mdeg;
	int32_t yaw_mdeg;
};

const char *wit_imu_modbus_model_name(enum wit_imu_modbus_model model);
int wit_imu_modbus_init(struct wit_imu_modbus_client *client,
			const struct wit_imu_modbus_config *config);
int wit_imu_modbus_fetch(struct wit_imu_modbus_client *client,
			 struct wit_imu_modbus_sample *sample);

#endif /* WIT_IMU_MODBUS_H_ */
