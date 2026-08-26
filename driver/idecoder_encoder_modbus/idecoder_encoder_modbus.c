#include "idecoder_encoder_modbus.h"

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/util.h>

#include "encoder_sample_backend.h"

LOG_MODULE_REGISTER(idecoder_encoder_modbus, CONFIG_LOG_DEFAULT_LEVEL);

#define IDECODER_ENCODER_REG_COUNT 2U

int idecoder_encoder_modbus_init(
	struct idecoder_encoder_modbus_client *client,
	const struct idecoder_encoder_modbus_config *config)
{
	const struct modbus_iface_param param = {
		.mode = MODBUS_MODE_RTU,
		.rx_timeout = config != NULL ? config->rx_timeout_us : 0U,
		.serial = {
			.baud = config != NULL ? config->baud : 0U,
			.parity = UART_CFG_PARITY_EVEN,
			.stop_bits = UART_CFG_STOP_BITS_1,
		},
	};
	int ret;

	if (client == NULL || config == NULL || config->iface_name == NULL) {
		return -EINVAL;
	}

	if (client->ready) {
		return 0;
	}

	client->iface = modbus_iface_get_by_name(config->iface_name);
	if (client->iface < 0) {
		LOG_ERR("IDECODER encoder Modbus interface not found: %s",
			config->iface_name);
		return client->iface;
	}

	ret = modbus_init_client(client->iface, param);
	if (ret != 0) {
		LOG_ERR("IDECODER encoder Modbus init failed: iface=%s err=%d",
			config->iface_name, ret);
		return ret;
	}

	client->config = config;
	client->ready = true;

	LOG_INF("IDECODER encoder Modbus ready: iface=%s unit=0x%02x baud=%u 8E1 timeout=%u us addr=0x%04x",
		config->iface_name, config->unit_id, config->baud,
		config->rx_timeout_us, config->start_addr);

	return 0;
}

int idecoder_encoder_modbus_fetch(
	struct idecoder_encoder_modbus_client *client,
	struct idecoder_encoder_modbus_sample *sample)
{
	uint16_t regs[IDECODER_ENCODER_REG_COUNT];
	int ret;

	if (client == NULL || sample == NULL) {
		return -EINVAL;
	}

	if (!client->ready || client->config == NULL || client->iface < 0) {
		return -ENODEV;
	}

	ret = modbus_read_holding_regs(client->iface, client->config->unit_id,
				       client->config->start_addr, regs,
				       ARRAY_SIZE(regs));
	if (ret != 0) {
		return ret;
	}

	sample->raw_reg_count = ARRAY_SIZE(regs);
	sample->raw_regs[0] = regs[0];
	sample->raw_regs[1] = regs[1];
	sample->turn_count = regs[0];
	sample->single_value = regs[1];

	return 0;
}

void idecoder_encoder_modbus_reset(
	struct idecoder_encoder_modbus_client *client)
{
	if (client == NULL) {
		return;
	}

	client->config = NULL;
	client->iface = -1;
	client->ready = false;
}

static int idecoder_encoder_backend_init(void *client, const void *config)
{
	return idecoder_encoder_modbus_init(client, config);
}

static int idecoder_encoder_backend_fetch(
	void *client, struct encoder_sample_service_sample *sample)
{
	struct idecoder_encoder_modbus_sample raw_sample;
	int ret;

	if (sample == NULL) {
		return -EINVAL;
	}

	ret = idecoder_encoder_modbus_fetch(client, &raw_sample);
	if (ret != 0) {
		return ret;
	}

	sample->raw_reg_count = raw_sample.raw_reg_count;
	sample->raw_regs[0] = raw_sample.raw_regs[0];
	sample->raw_regs[1] = raw_sample.raw_regs[1];
	sample->turn_count = raw_sample.turn_count;
	sample->single_value = raw_sample.single_value;

	return 0;
}

static void idecoder_encoder_backend_reset(void *client)
{
	idecoder_encoder_modbus_reset(client);
}

const struct encoder_sample_backend idecoder_encoder_modbus_backend = {
	.name = "idecoder_encoder_modbus",
	.init = idecoder_encoder_backend_init,
	.fetch = idecoder_encoder_backend_fetch,
	.reset = idecoder_encoder_backend_reset,
};
