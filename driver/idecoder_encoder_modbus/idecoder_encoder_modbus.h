#ifndef IDECODER_ENCODER_MODBUS_H_
#define IDECODER_ENCODER_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

struct idecoder_encoder_modbus_config {
	const char *iface_name;
	uint8_t unit_id;
	uint32_t baud;
	uint32_t rx_timeout_us;
	uint16_t start_addr;
};

struct idecoder_encoder_modbus_client {
	const struct idecoder_encoder_modbus_config *config;
	int iface;
	bool ready;
};

struct idecoder_encoder_modbus_sample {
	uint16_t raw_regs[2];
	uint8_t raw_reg_count;
	uint16_t turn_count;
	uint16_t single_value;
};

int idecoder_encoder_modbus_init(
	struct idecoder_encoder_modbus_client *client,
	const struct idecoder_encoder_modbus_config *config);
int idecoder_encoder_modbus_fetch(
	struct idecoder_encoder_modbus_client *client,
	struct idecoder_encoder_modbus_sample *sample);
void idecoder_encoder_modbus_reset(
	struct idecoder_encoder_modbus_client *client);

#endif /* IDECODER_ENCODER_MODBUS_H_ */
