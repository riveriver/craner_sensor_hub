#ifndef ANEMOMETER_SAMPLE_BACKEND_H_
#define ANEMOMETER_SAMPLE_BACKEND_H_

#include <stdbool.h>
#include <stdint.h>

struct anemometer_sample_service_sample {
	bool online;
	uint32_t seq;
	uint32_t status;
	uint16_t raw_regs[5];
	uint8_t raw_reg_count;
	uint16_t temperature;
	uint16_t humidity;
	uint16_t pressure;
	uint16_t wind_speed;
	uint16_t wind_direction;
	int64_t sample_uptime_ms;
	uint32_t read_duration_us;
	int last_error;
};

struct anemometer_sample_backend {
	const char *name;
	int (*init)(void *client, const void *config);
	int (*fetch)(void *client,
		     struct anemometer_sample_service_sample *sample);
	void (*reset)(void *client);
};

#endif /* ANEMOMETER_SAMPLE_BACKEND_H_ */
