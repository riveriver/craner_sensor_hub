#ifndef ANEMOMETER_MODBUS_H_
#define ANEMOMETER_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

struct anemometer_modbus_config {
	const char *iface_name;
	uint8_t unit_id;
	uint32_t baud;
	uint32_t rx_timeout_us;
	uint16_t start_addr;
	uint16_t register_count;
};

struct anemometer_modbus_client {
	const struct anemometer_modbus_config *config;
	int iface;
	bool ready;
};

struct anemometer_modbus_sample {
	uint16_t raw_regs[5];
	uint8_t raw_reg_count;
	uint16_t temperature;
	uint16_t humidity;
	uint16_t pressure;
	uint16_t wind_speed;
	uint16_t wind_direction;
};

int anemometer_modbus_init(struct anemometer_modbus_client *client,
			   const struct anemometer_modbus_config *config);
int anemometer_modbus_fetch(struct anemometer_modbus_client *client,
			    struct anemometer_modbus_sample *sample);
void anemometer_modbus_reset(struct anemometer_modbus_client *client);

#endif /* ANEMOMETER_MODBUS_H_ */
