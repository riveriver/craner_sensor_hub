#include "anemometer_modbus.h"

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/util.h>

#include "anemometer_sample_backend.h"

LOG_MODULE_REGISTER(anemometer_modbus, CONFIG_LOG_DEFAULT_LEVEL);

#define ANEMOMETER_MODBUS_MAX_REGISTER_COUNT 5U
#define ANEMOMETER_TEMPERATURE_OFFSET 4000U

int anemometer_modbus_init(struct anemometer_modbus_client *client,
			   const struct anemometer_modbus_config *config)
{
	const struct modbus_iface_param param = {
		.mode = MODBUS_MODE_RTU,
		.rx_timeout = config != NULL ? config->rx_timeout_us : 0U,
		.serial = {
			.baud = config != NULL ? config->baud : 0U,
			.parity = UART_CFG_PARITY_NONE,
			.stop_bits = UART_CFG_STOP_BITS_1,
		},
	};
	int ret;

	if (client == NULL || config == NULL || config->iface_name == NULL ||
	    config->register_count != ANEMOMETER_MODBUS_MAX_REGISTER_COUNT) {
		return -EINVAL;
	}

	if (client->ready) {
		return 0;
	}

	client->iface = modbus_iface_get_by_name(config->iface_name);
	if (client->iface < 0) {
		LOG_ERR("Anemometer Modbus interface not found: %s",
			config->iface_name);
		return client->iface;
	}

	ret = modbus_init_client(client->iface, param);
	if (ret != 0) {
		LOG_ERR("Anemometer Modbus init failed: iface=%s err=%d",
			config->iface_name, ret);
		return ret;
	}

	client->config = config;
	client->ready = true;

	LOG_INF("Anemometer Modbus ready: iface=%s unit=0x%02x baud=%u 8N1 timeout=%u us addr=0x%04x qty=%u",
		config->iface_name, config->unit_id, config->baud,
		config->rx_timeout_us, config->start_addr,
		(unsigned int)config->register_count);

	return 0;
}

int anemometer_modbus_fetch(struct anemometer_modbus_client *client,
			    struct anemometer_modbus_sample *sample)
{
	uint16_t regs[ANEMOMETER_MODBUS_MAX_REGISTER_COUNT];
	int ret;

	if (client == NULL || sample == NULL) {
		return -EINVAL;
	}

	if (!client->ready || client->config == NULL || client->iface < 0) {
		return -ENODEV;
	}

	ret = modbus_read_holding_regs(client->iface, client->config->unit_id,
				       client->config->start_addr, regs,
				       client->config->register_count);
	if (ret != 0) {
		return ret;
	}

	sample->raw_reg_count = client->config->register_count;
	for (uint8_t i = 0U;
	     i < sample->raw_reg_count && i < ARRAY_SIZE(sample->raw_regs);
	     i++) {
		sample->raw_regs[i] = regs[i];
	}

	sample->temperature = (uint16_t)(regs[0] - ANEMOMETER_TEMPERATURE_OFFSET);
	sample->humidity = regs[1];
	sample->pressure = regs[2];
	sample->wind_speed = regs[3];
	sample->wind_direction = regs[4];

	return 0;
}

void anemometer_modbus_reset(struct anemometer_modbus_client *client)
{
	if (client == NULL) {
		return;
	}

	client->config = NULL;
	client->iface = -1;
	client->ready = false;
}

static int anemometer_backend_init(void *client, const void *config)
{
	return anemometer_modbus_init(client, config);
}

static int anemometer_backend_fetch(
	void *client, struct anemometer_sample_service_sample *sample)
{
	struct anemometer_modbus_sample raw_sample;
	int ret;

	if (sample == NULL) {
		return -EINVAL;
	}

	ret = anemometer_modbus_fetch(client, &raw_sample);
	if (ret != 0) {
		return ret;
	}

	sample->raw_reg_count = raw_sample.raw_reg_count;
	for (uint8_t i = 0U;
	     i < raw_sample.raw_reg_count && i < ARRAY_SIZE(sample->raw_regs);
	     i++) {
		sample->raw_regs[i] = raw_sample.raw_regs[i];
	}
	sample->temperature = raw_sample.temperature;
	sample->humidity = raw_sample.humidity;
	sample->pressure = raw_sample.pressure;
	sample->wind_speed = raw_sample.wind_speed;
	sample->wind_direction = raw_sample.wind_direction;

	return 0;
}

static void anemometer_backend_reset(void *client)
{
	anemometer_modbus_reset(client);
}

const struct anemometer_sample_backend anemometer_modbus_backend = {
	.name = "anemometer_modbus",
	.init = anemometer_backend_init,
	.fetch = anemometer_backend_fetch,
	.reset = anemometer_backend_reset,
};
