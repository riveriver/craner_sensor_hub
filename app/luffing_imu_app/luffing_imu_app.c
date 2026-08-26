#include <errno.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "imu_sample_service.h"
#include "modbus_register_service.h"
#include "system_health_app.h"

LOG_MODULE_REGISTER(luffing_imu_app, CONFIG_LOG_DEFAULT_LEVEL);

static uint16_t error_code_to_reg(int err)
{
	if (err < 0) {
		return (uint16_t)(-err);
	}

	return (uint16_t)err;
}

static uint16_t value_high16(int32_t value)
{
	return (uint16_t)((uint32_t)value >> 16);
}

static uint16_t value_low16(int32_t value)
{
	return (uint16_t)((uint32_t)value & 0xffffU);
}

static int luffing_imu_app_write_failure(int err)
{
	const uint16_t failure_values[] = {
		error_code_to_reg(err),
		1U,
	};

	return modbus_register_service_write_inputs_by_name(
		"REG_LUFFING_IMU_ERROR_CODE", failure_values,
		ARRAY_SIZE(failure_values));
}

static int luffing_imu_app_write_success(
	const struct imu_sample_service_sample *sample)
{
	uint32_t timestamp_ms = k_uptime_get_32();
	const uint16_t imu_values[] = {
		(uint16_t)(timestamp_ms >> 16),
		(uint16_t)timestamp_ms,
		0U,
		0U,
		value_high16(sample->roll_mdeg),
		value_low16(sample->roll_mdeg),
		value_high16(sample->pitch_mdeg),
		value_low16(sample->pitch_mdeg),
		value_high16(sample->yaw_mdeg),
		value_low16(sample->yaw_mdeg),
	};

	return modbus_register_service_write_inputs_by_name(
		"REG_LUFFING_IMU_TIMESTAMP_H", imu_values,
		ARRAY_SIZE(imu_values));
}

static void luffing_imu_app_sample_cb(
	int err, const struct imu_sample_service_sample *sample,
	void *user_data)
{
	int write_err;

	ARG_UNUSED(user_data);

	if (err == 0 && sample != NULL) {
		system_health_update_event(SYSTEM_HEALTH_READ_LUFFING_ENCODER);

		write_err = luffing_imu_app_write_success(sample);
		if (write_err != 0) {
			LOG_WRN_RATELIMIT("Failed to update luffing IMU registers: %d",
					  write_err);
		}
		return;
	}

	write_err = luffing_imu_app_write_failure(err);
	if (write_err != 0) {
		LOG_WRN_RATELIMIT("Failed to update luffing IMU failure registers: %d",
				  write_err);
	}
}

static int luffing_imu_app_init(void)
{
	int err;

	err = imu_sample_service_register_callback(luffing_imu_app_sample_cb,
						   NULL);
	if (err != 0) {
		LOG_ERR("Failed to register luffing IMU callback: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(luffing_imu_app_init, APPLICATION, 80);
