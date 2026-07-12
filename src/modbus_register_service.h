#ifndef MODBUS_REGISTER_SERVICE_H
#define MODBUS_REGISTER_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modbus_register_map.h"

int modbus_register_service_register_map(struct modbus_register_map *map);
int modbus_register_service_init(void);

int modbus_register_service_read_coil(uint16_t addr, bool *value);
int modbus_register_service_read_coil_by_name(const char *name, bool *value);
int modbus_register_service_write_coil(uint16_t addr, bool value);
int modbus_register_service_write_coil_by_name(const char *name, bool value);
int modbus_register_service_read_coils_by_name(const char *start_name,
					       bool *values, size_t count);
int modbus_register_service_write_coils_by_name(
	const char *start_name, const bool *values, size_t count);
int modbus_register_service_read_coils(uint16_t start_addr, bool *values,
				       size_t count);
int modbus_register_service_write_coils(uint16_t start_addr,
					const bool *values, size_t count);

int modbus_register_service_read_input(uint16_t addr, uint16_t *value);
int modbus_register_service_read_input_by_name(const char *name,
					       uint16_t *value);
int modbus_register_service_write_input(uint16_t addr, uint16_t value);
int modbus_register_service_write_input_by_name(const char *name,
						uint16_t value);
int modbus_register_service_read_inputs_by_name(const char *start_name,
						uint16_t *values, size_t count);
int modbus_register_service_write_inputs_by_name(
	const char *start_name, const uint16_t *values, size_t count);
int modbus_register_service_read_inputs(uint16_t start_addr, uint16_t *values,
					size_t count);
int modbus_register_service_write_inputs(uint16_t start_addr,
					 const uint16_t *values, size_t count);

int modbus_register_service_read_holding(uint16_t addr, uint16_t *value);
int modbus_register_service_read_holding_by_name(const char *name,
						 uint16_t *value);
int modbus_register_service_write_holding(uint16_t addr, uint16_t value);
int modbus_register_service_write_holding_by_name(
	const char *name, uint16_t value);
int modbus_register_service_read_holdings_by_name(const char *start_name,
						  uint16_t *values,
						  size_t count);
int modbus_register_service_write_holdings_by_name(
	const char *start_name, const uint16_t *values, size_t count);
int modbus_register_service_read_holdings(uint16_t start_addr, uint16_t *values,
					  size_t count);
int modbus_register_service_write_holdings(uint16_t start_addr,
					   const uint16_t *values, size_t count);

#endif
