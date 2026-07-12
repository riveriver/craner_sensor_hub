#ifndef MODBUS_REGISTER_MAP_H
#define MODBUS_REGISTER_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct modbus_register_coil {
	const char *name;
	uint16_t addr;
	bool default_value;
	bool value;
};

struct modbus_register_input {
	const char *name;
	uint16_t addr;
	uint16_t default_value;
	uint16_t value;
};

struct modbus_register_holding {
	const char *name;
	uint16_t addr;
	uint16_t default_value;
	uint16_t value;
};

struct modbus_register_map {
	struct modbus_register_coil *coils;
	size_t coil_count;
	struct modbus_register_input *inputs;
	size_t input_count;
	struct modbus_register_holding *holdings;
	size_t holding_count;
};

struct modbus_register_map *modbus_register_map_get(void);

#endif
