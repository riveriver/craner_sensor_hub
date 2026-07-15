#ifndef MODBUS_REGISTER_MAP_H
#define MODBUS_REGISTER_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODBUS_REG_F_READABLE BIT(0)
#define MODBUS_REG_F_WRITABLE BIT(1)
#define MODBUS_REG_F_PERSISTENT BIT(2)

#define MODBUS_REG_ACCESS_RO (MODBUS_REG_F_READABLE)
#define MODBUS_REG_ACCESS_WO (MODBUS_REG_F_WRITABLE)
#define MODBUS_REG_ACCESS_RW (MODBUS_REG_F_READABLE | MODBUS_REG_F_WRITABLE)
#define MODBUS_REG_ACCESS_RW_PERSISTENT \
	(MODBUS_REG_F_READABLE | MODBUS_REG_F_WRITABLE | MODBUS_REG_F_PERSISTENT)

struct modbus_register_coil {
	const char *name;
	uint16_t addr;
	bool default_value;
	bool value;
	uint32_t flags;
};

struct modbus_register_input {
	const char *name;
	uint16_t addr;
	uint16_t default_value;
	uint16_t value;
	uint32_t flags;
};

struct modbus_register_holding {
	const char *name;
	uint16_t addr;
	uint16_t default_value;
	uint16_t value;
	uint32_t flags;
};

struct modbus_register_map {
	struct modbus_register_coil *coils;
	size_t coil_count;
	size_t coil_address_size;
	struct modbus_register_input *inputs;
	size_t input_count;
	size_t input_address_size;
	struct modbus_register_holding *holdings;
	size_t holding_count;
	size_t holding_address_size;
};

struct modbus_register_map *modbus_register_map_get(void);

#endif
