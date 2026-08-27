#include "modbus_register_map.h"

#include <zephyr/app_version.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include "modbus_register_service.h"

LOG_MODULE_REGISTER(modbus_register_map, CONFIG_LOG_DEFAULT_LEVEL);

#define MODBUS_COIL_ADDRESS_SIZE 10U
#define MODBUS_INPUT_ADDRESS_SIZE 100U
#define MODBUS_HOLDING_ADDRESS_SIZE 10U

#define MODBUS_PROTOCOL_VERSION_BCD 0x3100U
#define TOWER_TYPE_FLAT_TOP 1U
#define TOWER_TYPE_LUFFING_JIB 2U

#define DEVICE_CAPABILITY_SLEWING_ENCODER BIT(0)
#define DEVICE_CAPABILITY_LUFFING_ENCODER BIT(1)
#define DEVICE_CAPABILITY_HOISTING_ENCODER BIT(2)
#define DEVICE_CAPABILITY_ANEMOMETER BIT(3)
#define DEVICE_CAPABILITY_LOAD_SENSOR BIT(4)
#define DEVICE_CAPABILITY_LUFFING_IMU BIT(5)
#define DEVICE_CAPABILITY_TOWER_LUFFING_JIB BIT(15)

#define MODBUS_TOWER_TYPE \
	(IS_ENABLED(CONFIG_TOWER_TYPE_LUFFING_JIB) ? \
	 TOWER_TYPE_LUFFING_JIB : TOWER_TYPE_FLAT_TOP)
#define MODBUS_DEVICE_CAPABILITY_FLAGS \
	((IS_ENABLED(CONFIG_ENABLE_SLEWING_ENCODER) ? DEVICE_CAPABILITY_SLEWING_ENCODER : 0U) | \
	 (IS_ENABLED(CONFIG_ENABLE_LUFFING_ENCODER) ? DEVICE_CAPABILITY_LUFFING_ENCODER : 0U) | \
	 (IS_ENABLED(CONFIG_ENABLE_HOISTING_ENCODER) ? DEVICE_CAPABILITY_HOISTING_ENCODER : 0U) | \
	 (IS_ENABLED(CONFIG_ENABLE_ANEMOMETER_SENSOR) ? DEVICE_CAPABILITY_ANEMOMETER : 0U) | \
	 (IS_ENABLED(CONFIG_ENABLE_READ_LOAD_SENSOR) ? DEVICE_CAPABILITY_LOAD_SENSOR : 0U) | \
	 (IS_ENABLED(CONFIG_ENABLE_LUFFING_IMU) ? DEVICE_CAPABILITY_LUFFING_IMU : 0U) | \
	 (IS_ENABLED(CONFIG_TOWER_TYPE_LUFFING_JIB) ? DEVICE_CAPABILITY_TOWER_LUFFING_JIB : 0U))

enum {
	REG_FW_VERSION_ADDR = 0x0000,
	REG_FW_BUILD_YYMM_ADDR = 0x0001,
	REG_FW_BUILD_DDHH_ADDR = 0x0002,
	REG_FW_BUILD_MMSS_ADDR = 0x0003,
	REG_TOWER_TYPE_ADDR = 0x000D,
	REG_PROTOCOL_VERSION_ADDR = 0x000E,
	REG_DEVICE_CAPABILITY_FLAGS_ADDR = 0x000F,
};

#define BCD_BYTE(value) ((((value) / 10U) << 4) | ((value) % 10U))
#define BCD_REG(high, low) ((uint16_t)((BCD_BYTE(high) << 8) | BCD_BYTE(low)))
#define APP_VERSION_BCD \
	((uint16_t)(((APP_VERSION_MAJOR % 10U) << 12) | \
		    ((APP_VERSION_MINOR % 10U) << 8) | \
		    BCD_BYTE(APP_PATCHLEVEL)))

BUILD_ASSERT(APP_VERSION_MAJOR <= 9, "Modbus BCD version major supports 0-9");
BUILD_ASSERT(APP_VERSION_MINOR <= 9, "Modbus BCD version minor supports 0-9");
BUILD_ASSERT(APP_PATCHLEVEL <= 99, "Modbus BCD version patch supports 0-99");

static uint8_t parse_dec2(const char *text)
{
	if (text[0] < '0' || text[0] > '9' ||
	    text[1] < '0' || text[1] > '9') {
		return 0U;
	}

	return (uint8_t)((text[0] - '0') * 10U + (text[1] - '0'));
}

static uint8_t build_month(void)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	for (size_t i = 0; i < 12U; i++) {
		if (strncmp(&months[i * 3U], __DATE__, 3U) == 0) {
			return (uint8_t)(i + 1U);
		}
	}

	return 0U;
}

static uint8_t build_day(void)
{
	uint8_t tens = __DATE__[4] == ' ' ? 0U : (uint8_t)(__DATE__[4] - '0');
	uint8_t ones = (uint8_t)(__DATE__[5] - '0');

	return (uint8_t)(tens * 10U + ones);
}

static uint8_t build_year_short(void)
{
	return parse_dec2(&__DATE__[9]);
}

static uint8_t build_hour(void)
{
	return parse_dec2(&__TIME__[0]);
}

static uint8_t build_minute(void)
{
	return parse_dec2(&__TIME__[3]);
}

static uint8_t build_second(void)
{
	return parse_dec2(&__TIME__[6]);
}

static uint16_t build_time_YYMM_bcd(void)
{
	return BCD_REG(build_year_short(), build_month());
}

static uint16_t build_time_DDHH_bcd(void)
{
	return BCD_REG(build_day(), build_hour());
}

static uint16_t build_time_MMSS_bcd(void)
{
	return BCD_REG(build_minute(), build_second());
}

static struct modbus_register_coil coil_table[] = {
	{ .name = "REG_COIL_RESERVER", .addr = 0x0000, .default_value = false,
	  .flags = MODBUS_REG_ACCESS_RW },
};

static struct modbus_register_holding holding_register_table[] = {
	{ .name = "REG_HOLDING_RESERVER", .addr = 0x0000, .default_value = 0,
	  .flags = MODBUS_REG_ACCESS_RW_PERSISTENT },
};

static struct modbus_register_input input_register_table[] = {
	{ .name = "REG_FIRMWARE_VERSION", .addr = REG_FW_VERSION_ADDR, .default_value = APP_VERSION_BCD, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_FIRMWARE_BUILD_YYMM", .addr = REG_FW_BUILD_YYMM_ADDR, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_FIRMWARE_BUILD_DDHH", .addr = REG_FW_BUILD_DDHH_ADDR, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_FIRMWARE_BUILD_MMSS", .addr = REG_FW_BUILD_MMSS_ADDR, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_0004", .addr = 0x0004, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_0005", .addr = 0x0005, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_0006", .addr = 0x0006, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_0007", .addr = 0x0007, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_0008", .addr = 0x0008, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_0009", .addr = 0x0009, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_000A", .addr = 0x000A, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_000B", .addr = 0x000B, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_000C", .addr = 0x000C, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_TOWER_TYPE", .addr = REG_TOWER_TYPE_ADDR, .default_value = MODBUS_TOWER_TYPE, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_PROTOCOL_VERSION", .addr = REG_PROTOCOL_VERSION_ADDR, .default_value = MODBUS_PROTOCOL_VERSION_BCD, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_DEVICE_CAPABILITY_FLAGS", .addr = REG_DEVICE_CAPABILITY_FLAGS_ADDR, .default_value = MODBUS_DEVICE_CAPABILITY_FLAGS, .flags = MODBUS_REG_ACCESS_RW },

	{ .name = "REG_SLEWING_TIMESTAMP_H", .addr = 0x0010, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_TIMESTAMP_L", .addr = 0x0011, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_ERROR_CODE", .addr = 0x0012, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_OFFLINE_STATUS", .addr = 0x0013, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_TRUN_CNT", .addr = 0x0014, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_SLEWING_SINAGLE_VAL", .addr = 0x0015, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },

	{ .name = "REG_LUFFING_TIMESTAMP_H", .addr = 0x0016, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_TIMESTAMP_L", .addr = 0x0017, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_ERROR_CODE", .addr = 0x0018, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_OFFLINE_STATUS", .addr = 0x0019, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_VALUE_H", .addr = 0x001A, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_VALUE_L", .addr = 0x001B, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	
	{ .name = "REG_HOISTING_TIMESTAMP_H", .addr = 0x001C, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_TIMESTAMP_L", .addr = 0x001D, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_ERROR_CODE", .addr = 0x001E, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_OFFLINE_STATUS", .addr = 0x001F, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_TRUN_CNT", .addr = 0x0020, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_HOISTING_SINAGLE_VAL", .addr = 0x0021, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	
	{ .name = "REG_ANEMOMETER_TIMESTAMP_H", .addr = 0x0022, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_TIMESTAMP_L", .addr = 0x0023, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_ERROR_CODE", .addr = 0x0024, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_OFFLINE_STATUS", .addr = 0x0025, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_TEMPERATURE", .addr = 0x0026, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_HUMIDITY", .addr = 0x0027, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_PRESSURE", .addr = 0x0028, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_WIND_SPEED", .addr = 0x0029, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_ANEMOMETER_WIND_DIRECTION", .addr = 0x002A, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_RESERVED_002B", .addr = 0x002B, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },

	{ .name = "REG_LOAD_TIMESTAMP_H", .addr = 0x002C, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_TIMESTAMP_L", .addr = 0x002D, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_ERROR_CODE", .addr = 0x002E, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_OFFLINE_STATUS", .addr = 0x002F, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LIFTING_MOMENT_H", .addr = 0x0030, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LIFTING_MOMENT_L", .addr = 0x0031, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LIFTING_MOMENT_PCT_H", .addr = 0x0032, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LIFTING_MOMENT_PCT_L", .addr = 0x0033, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_WEIGHT_H", .addr = 0x0034, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_WEIGHT_L", .addr = 0x0035, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_WEIGHT_PCT_H", .addr = 0x0036, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LOAD_WEIGHT_PCT_L", .addr = 0x0037, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },

	{ .name = "REG_LUFFING_IMU_TIMESTAMP_H", .addr = 0x0038, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_TIMESTAMP_L", .addr = 0x0039, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_ERROR_CODE", .addr = 0x003A, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_OFFLINE_STATUS", .addr = 0x003B, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_ROLL_H", .addr = 0x003C, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_ROLL_L", .addr = 0x003D, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_PITCH_H", .addr = 0x003E, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_PITCH_L", .addr = 0x003F, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_YAW_H", .addr = 0x0040, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
	{ .name = "REG_LUFFING_IMU_YAW_L", .addr = 0x0041, .default_value = 0, .flags = MODBUS_REG_ACCESS_RW },
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

static void modbus_register_map_set_input_default(uint16_t addr, uint16_t value)
{
	for (size_t i = 0; i < ARRAY_SIZE(input_register_table); i++) {
		if (input_register_table[i].addr == addr) {
			input_register_table[i].default_value = value;
			return;
		}
	}
}

static void modbus_register_map_update_build_info(void)
{
	modbus_register_map_set_input_default(REG_FW_BUILD_YYMM_ADDR,
					      build_time_YYMM_bcd());
	modbus_register_map_set_input_default(REG_FW_BUILD_DDHH_ADDR,
					      build_time_DDHH_bcd());
	modbus_register_map_set_input_default(REG_FW_BUILD_MMSS_ADDR,
					      build_time_MMSS_bcd());
}

struct modbus_register_map *modbus_register_map_get(void)
{
	return &app_register_map;
}

static int modbus_register_map_init(void)
{
	int err;

	modbus_register_map_update_build_info();

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
