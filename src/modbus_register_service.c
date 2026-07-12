#include "modbus_register_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(modbus_register_service, LOG_LEVEL_INF);

static K_MUTEX_DEFINE(register_lock);

static struct modbus_register_map *registered_map;
static bool service_initialized;

static struct modbus_register_coil *find_coil(uint16_t addr)
{
	for (size_t i = 0; i < registered_map->coil_count; i++) {
		if (registered_map->coils[i].addr == addr) {
			return &registered_map->coils[i];
		}
	}

	return NULL;
}

static struct modbus_register_coil *find_coil_by_name(const char *name)
{
	for (size_t i = 0; i < registered_map->coil_count; i++) {
		if (strcmp(registered_map->coils[i].name, name) == 0) {
			return &registered_map->coils[i];
		}
	}

	return NULL;
}

static struct modbus_register_input *find_input(uint16_t addr)
{
	for (size_t i = 0; i < registered_map->input_count; i++) {
		if (registered_map->inputs[i].addr == addr) {
			return &registered_map->inputs[i];
		}
	}

	return NULL;
}

static struct modbus_register_input *find_input_by_name(const char *name)
{
	for (size_t i = 0; i < registered_map->input_count; i++) {
		if (strcmp(registered_map->inputs[i].name, name) == 0) {
			return &registered_map->inputs[i];
		}
	}

	return NULL;
}

static struct modbus_register_holding *find_holding(uint16_t addr)
{
	for (size_t i = 0; i < registered_map->holding_count; i++) {
		if (registered_map->holdings[i].addr == addr) {
			return &registered_map->holdings[i];
		}
	}

	return NULL;
}

static struct modbus_register_holding *find_holding_by_name(const char *name)
{
	for (size_t i = 0; i < registered_map->holding_count; i++) {
		if (strcmp(registered_map->holdings[i].name, name) == 0) {
			return &registered_map->holdings[i];
		}
	}

	return NULL;
}

static bool range_is_valid(uint16_t start_addr, size_t count)
{
	if (count == 0) {
		return true;
	}

	return count <= (size_t)UINT16_MAX + 1U &&
	       start_addr <= UINT16_MAX - (uint16_t)(count - 1U);
}

static int init_service_if_needed(void)
{
	if (service_initialized) {
		return 0;
	}

	return modbus_register_service_init();
}

static int coil_addr_by_name_locked(const char *name, uint16_t *addr)
{
	struct modbus_register_coil *coil;

	if (name == NULL || addr == NULL) {
		return -EINVAL;
	}

	coil = find_coil_by_name(name);
	if (coil == NULL) {
		return -ENOTSUP;
	}

	*addr = coil->addr;

	return 0;
}

static int input_addr_by_name_locked(const char *name, uint16_t *addr)
{
	struct modbus_register_input *input;

	if (name == NULL || addr == NULL) {
		return -EINVAL;
	}

	input = find_input_by_name(name);
	if (input == NULL) {
		return -ENOTSUP;
	}

	*addr = input->addr;

	return 0;
}

static int holding_addr_by_name_locked(const char *name, uint16_t *addr)
{
	struct modbus_register_holding *holding;

	if (name == NULL || addr == NULL) {
		return -EINVAL;
	}

	holding = find_holding_by_name(name);
	if (holding == NULL) {
		return -ENOTSUP;
	}

	*addr = holding->addr;

	return 0;
}

static int read_coils_locked(uint16_t start_addr, bool *values, size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (values == NULL || !range_is_valid(start_addr, count)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		if (find_coil(start_addr + (uint16_t)i) == NULL) {
			return -ENOTSUP;
		}
	}

	for (size_t i = 0; i < count; i++) {
		values[i] = find_coil(start_addr + (uint16_t)i)->value;
	}

	return 0;
}

static int write_coils_locked(uint16_t start_addr, const bool *values,
			      size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (values == NULL || !range_is_valid(start_addr, count)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		if (find_coil(start_addr + (uint16_t)i) == NULL) {
			return -ENOTSUP;
		}
	}

	for (size_t i = 0; i < count; i++) {
		struct modbus_register_coil *coil =
			find_coil(start_addr + (uint16_t)i);

		coil->value = values[i];
	}

	return 0;
}

static int read_inputs_locked(uint16_t start_addr, uint16_t *values,
			      size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (values == NULL || !range_is_valid(start_addr, count)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		if (find_input(start_addr + (uint16_t)i) == NULL) {
			return -ENOTSUP;
		}
	}

	for (size_t i = 0; i < count; i++) {
		values[i] = find_input(start_addr + (uint16_t)i)->value;
	}

	return 0;
}

static int write_inputs_locked(uint16_t start_addr, const uint16_t *values,
			       size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (values == NULL || !range_is_valid(start_addr, count)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		if (find_input(start_addr + (uint16_t)i) == NULL) {
			return -ENOTSUP;
		}
	}

	for (size_t i = 0; i < count; i++) {
		find_input(start_addr + (uint16_t)i)->value = values[i];
	}

	return 0;
}

static int read_holdings_locked(uint16_t start_addr, uint16_t *values,
				size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (values == NULL || !range_is_valid(start_addr, count)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		if (find_holding(start_addr + (uint16_t)i) == NULL) {
			return -ENOTSUP;
		}
	}

	for (size_t i = 0; i < count; i++) {
		values[i] = find_holding(start_addr + (uint16_t)i)->value;
	}

	return 0;
}

static int write_holdings_locked(uint16_t start_addr, const uint16_t *values,
				 size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (values == NULL || !range_is_valid(start_addr, count)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		if (find_holding(start_addr + (uint16_t)i) == NULL) {
			return -ENOTSUP;
		}
	}

	for (size_t i = 0; i < count; i++) {
		struct modbus_register_holding *holding =
			find_holding(start_addr + (uint16_t)i);

		holding->value = values[i];
	}

	return 0;
}

int modbus_register_service_register_map(struct modbus_register_map *map)
{
	if (map == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	registered_map = map;
	service_initialized = false;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_init(void)
{
	k_mutex_lock(&register_lock, K_FOREVER);

	if (registered_map == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENODEV;
	}

	for (size_t i = 0; i < registered_map->coil_count; i++) {
		struct modbus_register_coil *coil = &registered_map->coils[i];

		coil->value = coil->default_value;
	}

	for (size_t i = 0; i < registered_map->input_count; i++) {
		struct modbus_register_input *input = &registered_map->inputs[i];

		input->value = input->default_value;
	}

	for (size_t i = 0; i < registered_map->holding_count; i++) {
		struct modbus_register_holding *holding =
			&registered_map->holdings[i];

		holding->value = holding->default_value;
	}

	service_initialized = true;
	k_mutex_unlock(&register_lock);

	LOG_INF("Registered Modbus register map: coils=%u inputs=%u holdings=%u",
		(unsigned int)registered_map->coil_count,
		(unsigned int)registered_map->input_count,
		(unsigned int)registered_map->holding_count);

	return 0;
}

int modbus_register_service_read_coil(uint16_t addr, bool *value)
{
	struct modbus_register_coil *coil;
	int err;

	if (value == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	coil = find_coil(addr);
	if (coil == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	*value = coil->value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_read_coil_by_name(const char *name, bool *value)
{
	struct modbus_register_coil *coil;
	int err;

	if (name == NULL || value == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	coil = find_coil_by_name(name);
	if (coil == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	*value = coil->value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_write_coil(uint16_t addr, bool value)
{
	struct modbus_register_coil *coil;
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	coil = find_coil(addr);
	if (coil == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	coil->value = value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_write_coil_by_name(const char *name, bool value)
{
	struct modbus_register_coil *coil;
	int err;

	if (name == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	coil = find_coil_by_name(name);
	if (coil == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	coil->value = value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_read_coils(uint16_t start_addr, bool *values,
				       size_t count)
{
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = read_coils_locked(start_addr, values, count);
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_write_coils(uint16_t start_addr,
					const bool *values, size_t count)
{
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = write_coils_locked(start_addr, values, count);
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_read_coils_by_name(const char *start_name,
					       bool *values, size_t count)
{
	uint16_t start_addr;
	int err;

	if (count == 0) {
		return 0;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = coil_addr_by_name_locked(start_name, &start_addr);
	if (err == 0) {
		err = read_coils_locked(start_addr, values, count);
	}
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_write_coils_by_name(
	const char *start_name, const bool *values, size_t count)
{
	uint16_t start_addr;
	int err;

	if (count == 0) {
		return 0;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = coil_addr_by_name_locked(start_name, &start_addr);
	if (err == 0) {
		err = write_coils_locked(start_addr, values, count);
	}
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_read_input(uint16_t addr, uint16_t *value)
{
	struct modbus_register_input *input;
	int err;

	if (value == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	input = find_input(addr);
	if (input == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	*value = input->value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_read_input_by_name(const char *name,
					       uint16_t *value)
{
	struct modbus_register_input *input;
	int err;

	if (name == NULL || value == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	input = find_input_by_name(name);
	if (input == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	*value = input->value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_write_input(uint16_t addr, uint16_t value)
{
	struct modbus_register_input *input;
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	input = find_input(addr);
	if (input == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	input->value = value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_write_input_by_name(const char *name,
						uint16_t value)
{
	struct modbus_register_input *input;
	int err;

	if (name == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	input = find_input_by_name(name);
	if (input == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	input->value = value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_read_inputs(uint16_t start_addr, uint16_t *values,
					size_t count)
{
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = read_inputs_locked(start_addr, values, count);
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_write_inputs(uint16_t start_addr,
					 const uint16_t *values, size_t count)
{
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = write_inputs_locked(start_addr, values, count);
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_read_inputs_by_name(const char *start_name,
						uint16_t *values, size_t count)
{
	uint16_t start_addr;
	int err;

	if (count == 0) {
		return 0;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = input_addr_by_name_locked(start_name, &start_addr);
	if (err == 0) {
		err = read_inputs_locked(start_addr, values, count);
	}
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_write_inputs_by_name(
	const char *start_name, const uint16_t *values, size_t count)
{
	uint16_t start_addr;
	int err;

	if (count == 0) {
		return 0;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = input_addr_by_name_locked(start_name, &start_addr);
	if (err == 0) {
		err = write_inputs_locked(start_addr, values, count);
	}
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_read_holding(uint16_t addr, uint16_t *value)
{
	struct modbus_register_holding *holding;
	int err;

	if (value == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	holding = find_holding(addr);
	if (holding == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	*value = holding->value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_read_holding_by_name(const char *name,
						 uint16_t *value)
{
	struct modbus_register_holding *holding;
	int err;

	if (name == NULL || value == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	holding = find_holding_by_name(name);
	if (holding == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	*value = holding->value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_write_holding(uint16_t addr, uint16_t value)
{
	struct modbus_register_holding *holding;
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	holding = find_holding(addr);
	if (holding == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	holding->value = value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_write_holding_by_name(const char *name,
						  uint16_t value)
{
	struct modbus_register_holding *holding;
	int err;

	if (name == NULL) {
		return -EINVAL;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	holding = find_holding_by_name(name);
	if (holding == NULL) {
		k_mutex_unlock(&register_lock);
		return -ENOTSUP;
	}

	holding->value = value;
	k_mutex_unlock(&register_lock);

	return 0;
}

int modbus_register_service_read_holdings(uint16_t start_addr, uint16_t *values,
					  size_t count)
{
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = read_holdings_locked(start_addr, values, count);
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_write_holdings(uint16_t start_addr,
					   const uint16_t *values, size_t count)
{
	int err;

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = write_holdings_locked(start_addr, values, count);
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_read_holdings_by_name(const char *start_name,
						  uint16_t *values,
						  size_t count)
{
	uint16_t start_addr;
	int err;

	if (count == 0) {
		return 0;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = holding_addr_by_name_locked(start_name, &start_addr);
	if (err == 0) {
		err = read_holdings_locked(start_addr, values, count);
	}
	k_mutex_unlock(&register_lock);

	return err;
}

int modbus_register_service_write_holdings_by_name(
	const char *start_name, const uint16_t *values, size_t count)
{
	uint16_t start_addr;
	int err;

	if (count == 0) {
		return 0;
	}

	err = init_service_if_needed();
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&register_lock, K_FOREVER);
	err = holding_addr_by_name_locked(start_name, &start_addr);
	if (err == 0) {
		err = write_holdings_locked(start_addr, values, count);
	}
	k_mutex_unlock(&register_lock);

	return err;
}
