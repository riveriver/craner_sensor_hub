#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "anemometer_sample_service.h"
#include "modbus_register_service.h"
#include "system_health_app.h"

LOG_MODULE_REGISTER(crane_anemometer_app, CONFIG_LOG_DEFAULT_LEVEL);

static struct anemometer_sample_service crane_anemometer_service;

static const struct anemometer_sample_service_config crane_anemometer_config = {
	.name = "anemometer",
	.iface_name = CONFIG_CRANE_ANEMOMETER_IFACE_NAME,
	.unit_id = CONFIG_CRANE_ANEMOMETER_MODBUS_UNIT_ID,
	.baud = CONFIG_CRANE_ANEMOMETER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_CRANE_ANEMOMETER_MODBUS_RX_TIMEOUT_US,
	.period_ms = CONFIG_CRANE_ANEMOMETER_SAMPLE_PERIOD_MS,
	.start_delay_ms = 35U,
	.start_addr = CONFIG_CRANE_ANEMOMETER_MODBUS_START_ADDR,
	.register_count = CONFIG_CRANE_ANEMOMETER_MODBUS_REGISTER_COUNT,
};

static uint16_t error_code_to_reg(int err)
{
	if (err < 0) {
		return (uint16_t)(-err);
	}

	return (uint16_t)err;
}

static int crane_anemometer_write_failure(int err)
{
	const uint16_t failure_values[] = {
		error_code_to_reg(err),
		1U,
	};

	return modbus_register_service_write_inputs_by_name(
		"REG_ANEMOMETER_ERROR_CODE", failure_values,
		ARRAY_SIZE(failure_values));
}

static int crane_anemometer_write_success(
	const struct anemometer_sample_service_sample *sample)
{
	uint32_t timestamp_ms = k_uptime_get_32();
	const uint16_t values[] = {
		(uint16_t)(timestamp_ms >> 16),
		(uint16_t)timestamp_ms,
		0U,
		0U,
		sample->temperature,
		sample->humidity,
		sample->pressure,
		sample->wind_speed,
		sample->wind_direction,
	};

	return modbus_register_service_write_inputs_by_name(
		"REG_ANEMOMETER_TIMESTAMP_H", values, ARRAY_SIZE(values));
}

static void crane_anemometer_sample_cb(
	struct anemometer_sample_service *service, int err,
	const struct anemometer_sample_service_sample *sample, void *user_data)
{
	int write_err;

	ARG_UNUSED(service);
	ARG_UNUSED(user_data);

	if (err == 0 && sample != NULL) {
		system_health_update_event(SYSTEM_HEALTH_READ_ANEMOMETER);

		write_err = crane_anemometer_write_success(sample);
		if (write_err != 0) {
			LOG_WRN_RATELIMIT("Failed to update anemometer registers: %d",
					  write_err);
		}
		return;
	}

	write_err = crane_anemometer_write_failure(err);
	if (write_err != 0) {
		LOG_WRN_RATELIMIT("Failed to update anemometer failure registers: %d",
				  write_err);
	}
}

static int crane_anemometer_app_init(void)
{
	int err;

	err = anemometer_sample_service_start(&crane_anemometer_service,
					      &crane_anemometer_config,
					      crane_anemometer_sample_cb,
					      NULL);
	if (err != 0) {
		LOG_ERR("Failed to start anemometer service: %d", err);
		return err;
	}

	LOG_INF("Crane anemometer app started");

	return 0;
}

SYS_INIT(crane_anemometer_app_init, APPLICATION, 95);
