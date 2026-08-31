#include <errno.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "encoder_sample_service.h"
#include "idecoder_encoder_modbus.h"
#include "modbus_data_model.h"
#include "system_health_app.h"

LOG_MODULE_REGISTER(encoder_app, CONFIG_LOG_DEFAULT_LEVEL);

struct encoder_binding {
	const char *name;
	const char *timestamp_high_name;
	const char *error_code_name;
	enum system_health_event health_event;
};

static uint16_t encoder_error_code(int err)
{
	if (err < 0) {
		return (uint16_t)(-err);
	}

	return (uint16_t)err;
}

static int encoder_write_failure(
	const struct encoder_binding *binding, int err)
{
	const uint16_t values[] = {
		encoder_error_code(err),
		1U,
	};

	return modbus_data_model_write_inputs_by_name(
		binding->error_code_name, values, ARRAY_SIZE(values));
}

static int encoder_write_success(
	const struct encoder_binding *binding,
	const struct encoder_sample_service_sample *sample)
{
	uint32_t timestamp_ms = k_uptime_get_32();
	const uint16_t values[] = {
		(uint16_t)(timestamp_ms >> 16),
		(uint16_t)timestamp_ms,
		0U,
		0U,
		sample->turn_count,
		sample->single_value,
	};

	return modbus_data_model_write_inputs_by_name(
		binding->timestamp_high_name, values, ARRAY_SIZE(values));
}

static void encoder_sample_cb(
	struct encoder_sample_service *service, int err,
	const struct encoder_sample_service_sample *sample, void *user_data)
{
	const struct encoder_binding *binding = user_data;
	int write_err;

	ARG_UNUSED(service);

	if (binding == NULL) {
		return;
	}

	if (err == 0 && sample != NULL) {
		system_health_update_event(binding->health_event);

		write_err = encoder_write_success(binding, sample);
		if (write_err != 0) {
			LOG_WRN_RATELIMIT("%s failed to update input registers: %d",
					   binding->name, write_err);
		}
		return;
	}

	write_err = encoder_write_failure(binding, err);
	if (write_err != 0) {
		LOG_WRN_RATELIMIT("%s failed to update failure registers: %d",
				   binding->name, write_err);
	}
}

#if defined(CONFIG_ENABLE_SLEWING_ENCODER)
static struct encoder_sample_service slewing_encoder_service;
static struct idecoder_encoder_modbus_client slewing_encoder_client = {
	.iface = -1,
};
static const struct idecoder_encoder_modbus_config slewing_encoder_backend_config = {
	.iface_name = CONFIG_ENCODER_SLEWING_IFACE_NAME,
	.unit_id = CONFIG_ENCODER_MODBUS_UNIT_ID,
	.baud = CONFIG_ENCODER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_ENCODER_MODBUS_RX_TIMEOUT_MS * 1000U,
	.start_addr = CONFIG_ENCODER_MODBUS_START_ADDR,
};
static const struct encoder_sample_service_config slewing_encoder_config = {
	.name = "slewing",
	.iface_name = CONFIG_ENCODER_SLEWING_IFACE_NAME,
	.period_ms = CONFIG_ENCODER_SAMPLE_PERIOD_MS,
	.start_delay_ms = 0U,
	.backend = &idecoder_encoder_modbus_backend,
	.backend_client = &slewing_encoder_client,
	.backend_config = &slewing_encoder_backend_config,
};
static const struct encoder_binding slewing_encoder_binding = {
	.name = "slewing encoder",
	.timestamp_high_name = "REG_SLEWING_TIMESTAMP_H",
	.error_code_name = "REG_SLEWING_ERROR_CODE",
	.health_event = SYSTEM_HEALTH_READ_SLEWING_ENCODER,
};
#endif

#if defined(CONFIG_ENABLE_LUFFING_ENCODER)
static struct encoder_sample_service luffing_encoder_service;
static struct idecoder_encoder_modbus_client luffing_encoder_client = {
	.iface = -1,
};
static const struct idecoder_encoder_modbus_config luffing_encoder_backend_config = {
	.iface_name = CONFIG_ENCODER_LUFFING_IFACE_NAME,
	.unit_id = CONFIG_ENCODER_MODBUS_UNIT_ID,
	.baud = CONFIG_ENCODER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_ENCODER_MODBUS_RX_TIMEOUT_MS * 1000U,
	.start_addr = CONFIG_ENCODER_MODBUS_START_ADDR,
};
static const struct encoder_sample_service_config luffing_encoder_config = {
	.name = "luffing",
	.iface_name = CONFIG_ENCODER_LUFFING_IFACE_NAME,
	.period_ms = CONFIG_ENCODER_SAMPLE_PERIOD_MS,
	.start_delay_ms = 10U,
	.backend = &idecoder_encoder_modbus_backend,
	.backend_client = &luffing_encoder_client,
	.backend_config = &luffing_encoder_backend_config,
};
static const struct encoder_binding luffing_encoder_binding = {
	.name = "luffing encoder",
	.timestamp_high_name = "REG_LUFFING_TIMESTAMP_H",
	.error_code_name = "REG_LUFFING_ERROR_CODE",
	.health_event = SYSTEM_HEALTH_READ_LUFFING_ENCODER,
};
#endif

#if defined(CONFIG_ENABLE_HOISTING_ENCODER)
static struct encoder_sample_service hoisting_encoder_service;
static struct idecoder_encoder_modbus_client hoisting_encoder_client = {
	.iface = -1,
};
static const struct idecoder_encoder_modbus_config hoisting_encoder_backend_config = {
	.iface_name = CONFIG_ENCODER_HOISTING_IFACE_NAME,
	.unit_id = CONFIG_ENCODER_MODBUS_UNIT_ID,
	.baud = CONFIG_ENCODER_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_ENCODER_MODBUS_RX_TIMEOUT_MS * 1000U,
	.start_addr = CONFIG_ENCODER_MODBUS_START_ADDR,
};
static const struct encoder_sample_service_config hoisting_encoder_config = {
	.name = "hoisting",
	.iface_name = CONFIG_ENCODER_HOISTING_IFACE_NAME,
	.period_ms = CONFIG_ENCODER_SAMPLE_PERIOD_MS,
	.start_delay_ms = 20U,
	.backend = &idecoder_encoder_modbus_backend,
	.backend_client = &hoisting_encoder_client,
	.backend_config = &hoisting_encoder_backend_config,
};
static const struct encoder_binding hoisting_encoder_binding = {
	.name = "hoisting encoder",
	.timestamp_high_name = "REG_HOISTING_TIMESTAMP_H",
	.error_code_name = "REG_HOISTING_ERROR_CODE",
	.health_event = SYSTEM_HEALTH_READ_HOISTING_ENCODER,
};
#endif

static int encoder_app_init(void)
{
	int err;

#if defined(CONFIG_ENABLE_SLEWING_ENCODER)
	err = encoder_sample_service_start(&slewing_encoder_service,
					   &slewing_encoder_config,
					   encoder_sample_cb,
					   (void *)&slewing_encoder_binding);
	if (err != 0) {
		LOG_ERR("Failed to start slewing encoder service: %d", err);
		return err;
	}
#endif

#if defined(CONFIG_ENABLE_LUFFING_ENCODER)
	err = encoder_sample_service_start(&luffing_encoder_service,
					   &luffing_encoder_config,
					   encoder_sample_cb,
					   (void *)&luffing_encoder_binding);
	if (err != 0) {
		LOG_ERR("Failed to start luffing encoder service: %d", err);
		return err;
	}
#endif

#if defined(CONFIG_ENABLE_HOISTING_ENCODER)
	err = encoder_sample_service_start(&hoisting_encoder_service,
					   &hoisting_encoder_config,
					   encoder_sample_cb,
					   (void *)&hoisting_encoder_binding);
	if (err != 0) {
		LOG_ERR("Failed to start hoisting encoder service: %d", err);
		return err;
	}
#endif

	LOG_INF("Encoder app started");

	return 0;
}

SYS_INIT(encoder_app_init, APPLICATION, 95);
