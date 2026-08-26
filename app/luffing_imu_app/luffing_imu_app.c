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

static struct imu_sample_service luffing_imu_service;

static const struct imu_sample_service_config luffing_imu_config = {
	.name = "luffing_imu",
	.iface_name = CONFIG_LUFFING_IMU_IFACE_NAME,
#if defined(CONFIG_LUFFING_IMU_MODEL_WIT_HIGH_PRECISION)
	.model = WIT_IMU_MODBUS_MODEL_HIGH_PRECISION,
#else
	.model = WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION,
#endif
	.unit_id = CONFIG_LUFFING_IMU_MODBUS_UNIT_ID,
	.baud = CONFIG_LUFFING_IMU_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_LUFFING_IMU_MODBUS_RX_TIMEOUT_US,
	.period_ms = CONFIG_LUFFING_IMU_SAMPLE_PERIOD_MS,
	.start_delay_ms = 0U,
};

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
	struct imu_sample_service *service, int err,
	const struct imu_sample_service_sample *sample, void *user_data)
{
	int write_err;

	ARG_UNUSED(service);
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

	err = imu_sample_service_start(&luffing_imu_service,
				       &luffing_imu_config,
				       luffing_imu_app_sample_cb,
				       NULL);
	if (err != 0) {
		LOG_ERR("Failed to start luffing IMU service: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(luffing_imu_app_init, APPLICATION, 95);
