#ifndef ENCODER_SAMPLE_BACKEND_H_
#define ENCODER_SAMPLE_BACKEND_H_

#include <stdbool.h>
#include <stdint.h>

struct encoder_sample_service_sample {
	bool online;
	uint32_t seq;
	uint32_t status;
	uint16_t raw_regs[2];
	uint8_t raw_reg_count;
	uint16_t turn_count;
	uint16_t single_value;
	int64_t sample_uptime_ms;
	uint32_t read_duration_us;
	int last_error;
};

struct encoder_sample_backend {
	const char *name;
	int (*init)(void *client, const void *config);
	int (*fetch)(void *client, struct encoder_sample_service_sample *sample);
	void (*reset)(void *client);
};

#endif /* ENCODER_SAMPLE_BACKEND_H_ */
