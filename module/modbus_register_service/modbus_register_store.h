#ifndef MODBUS_REGISTER_STORE_H_
#define MODBUS_REGISTER_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct modbus_register_store_status {
	bool initialized;
	bool dirty;
	bool active_bank_valid;
	uint8_t active_bank;
	uint32_t active_sequence;
	uint32_t bank_size;
	uint32_t payload_size;
	uint32_t last_payload_size;
	uint32_t load_count;
	uint32_t save_count;
	uint32_t clear_count;
	uint32_t fail_count;
	int last_stage;
	int last_error;
};

int modbus_register_store_init(void);
int modbus_register_store_mark_dirty(void);
int modbus_register_store_save_now(void);
int modbus_register_store_load(void);
int modbus_register_store_clear(void);
void modbus_register_store_get_status(struct modbus_register_store_status *status);
int modbus_register_store_format_status(char *buf, size_t len);

#endif /* MODBUS_REGISTER_STORE_H_ */
