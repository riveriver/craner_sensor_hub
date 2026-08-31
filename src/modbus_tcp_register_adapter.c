#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "modbus_data_model.h"
#include "modbus_tcp_server.h"

LOG_MODULE_REGISTER(modbus_tcp_register_adapter, CONFIG_LOG_DEFAULT_LEVEL);

static int adapter_coil_read(uint16_t addr, bool *value, void *user_data)
{
	ARG_UNUSED(user_data);

	return modbus_data_model_read_coil(addr, value);
}

static int adapter_coil_write(uint16_t addr, bool value, void *user_data)
{
	ARG_UNUSED(user_data);

	return modbus_data_model_write_coil(addr, value);
}

static int adapter_input_read(uint16_t addr, uint16_t *value, void *user_data)
{
	ARG_UNUSED(user_data);

	return modbus_data_model_read_input(addr, value);
}

static int adapter_holding_read(uint16_t addr, uint16_t *value, void *user_data)
{
	ARG_UNUSED(user_data);

	return modbus_data_model_read_holding(addr, value);
}

static int adapter_holding_write(uint16_t addr, uint16_t value,
				 void *user_data)
{
	ARG_UNUSED(user_data);

	return modbus_data_model_write_holding(addr, value);
}

static const struct modbus_tcp_server_callbacks adapter_callbacks = {
	.coil_read = adapter_coil_read,
	.coil_write = adapter_coil_write,
	.input_read = adapter_input_read,
	.holding_read = adapter_holding_read,
	.holding_write = adapter_holding_write,
};

static int modbus_tcp_register_adapter_init(void)
{
	int err;

	err = modbus_data_model_init();
	if (err != 0) {
		LOG_ERR("Failed to initialize Modbus data model: %d", err);
		return err;
	}

	err = modbus_tcp_server_register_callbacks(&adapter_callbacks, NULL);
	if (err != 0) {
		LOG_ERR("Failed to register Modbus TCP callbacks: %d", err);
		return err;
	}

	LOG_INF("Modbus TCP register adapter is ready");

	return 0;
}

SYS_INIT(modbus_tcp_register_adapter_init, APPLICATION, 95);
