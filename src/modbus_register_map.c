#include "modbus_register_map.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "modbus_register_service.h"

LOG_MODULE_REGISTER(modbus_register_map, CONFIG_LOG_DEFAULT_LEVEL);

#define MODBUS_COIL_ADDRESS_SIZE 10U
#define MODBUS_INPUT_ADDRESS_SIZE 100U
#define MODBUS_HOLDING_ADDRESS_SIZE 10U

static struct modbus_register_coil coil_table[] = {
	{ .name = "REG_COIL_RESERVER", .addr = 0x0000, .default_value = false,
	  .flags = MODBUS_REG_ACCESS_RW },
};

static struct modbus_register_holding holding_register_table[] = {
	{ .name = "REG_HOLDING_RESERVER", .addr = 0x0000, .default_value = 0,
	  .flags = MODBUS_REG_ACCESS_RW_PERSISTENT },
};

static struct modbus_register_input input_register_table[] = {
	{ .name = "REG_SLEWING_TIMESTAMP_H", .addr = 0x0000, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_TIMESTAMP_L", .addr = 0x0001, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_ERROR_CODE", .addr = 0x0002, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_OFFLINE_STATUS", .addr = 0x0003, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_TRUN_CNT", .addr = 0x0004, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_SINAGLE_VAL", .addr = 0x0005, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },

	{ .name = "REG_LUFFING_TIMESTAMP_H", .addr = 0x0006, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_TIMESTAMP_L", .addr = 0x0007, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_ERROR_CODE", .addr = 0x0008, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_OFFLINE_STATUS", .addr = 0x0009, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_TRUN_CNT", .addr = 0x000A, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_SINAGLE_VAL", .addr = 0x000B, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	
	{ .name = "REG_HOISTING_TIMESTAMP_H", .addr = 0x000C, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_TIMESTAMP_L", .addr = 0x000D, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_ERROR_CODE", .addr = 0x000E, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_OFFLINE_STATUS", .addr = 0x000F, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_TRUN_CNT", .addr = 0x0010, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_SINAGLE_VAL", .addr = 0x0011, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	
	{ .name = "REG_ANEMOMETER_TIMESTAMP_H", .addr = 0x0012, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_TIMESTAMP_L", .addr = 0x0013, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_ERROR_CODE", .addr = 0x0014, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_OFFLINE_STATUS", .addr = 0x0015, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_TEMPERATURE", .addr = 0x0016, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_HUMIDITY", .addr = 0x0017, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_PRESSURE", .addr = 0x0018, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_WIND_SPEED", .addr = 0x0019, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_WIND_DIRECTION", .addr = 0x001A, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
};

static struct modbus_register_map app_register_map = {
	.coils = coil_table,
	.coil_count = ARRAY_SIZE(coil_table),
	.coil_address_size = MODBUS_COIL_ADDRESS_SIZE,
	.inputs = input_register_table,
	.input_count = ARRAY_SIZE(input_register_table),
	.input_address_size = MODBUS_INPUT_ADDRESS_SIZE,
	.holdings = holding_register_table,
	.holding_count = ARRAY_SIZE(holding_register_table),
	.holding_address_size = MODBUS_HOLDING_ADDRESS_SIZE,
};

struct modbus_register_map *modbus_register_map_get(void)
{
	return &app_register_map;
}

static int modbus_register_map_init(void)
{
	int err;

	err = modbus_register_service_register_map(&app_register_map);
	if (err != 0) {
		return err;
	}

	err = modbus_register_service_init();
	if (err != 0) {
		return err;
	}

	LOG_INF("Project Modbus register map is ready");

	return 0;
}

SYS_INIT(modbus_register_map_init, APPLICATION, 90);
