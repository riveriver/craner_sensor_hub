#ifndef ENCODER_SAMPLE_SERVICE_INTERNAL_H_
#define ENCODER_SAMPLE_SERVICE_INTERNAL_H_

#include "encoder_sample_service.h"

const char *encoder_sample_service_name(
	const struct encoder_sample_service *service);
void encoder_sample_service_update_success(
	struct encoder_sample_service *service,
	const struct encoder_sample_service_sample *sample,
	uint32_t read_duration_us,
	struct encoder_sample_service_sample *notify_sample);
void encoder_sample_service_update_error(
	struct encoder_sample_service *service, int err);

#if defined(CONFIG_ENCODER_SAMPLE_SERVICE_SHELL)
int encoder_sample_service_register_shell_instance(
	struct encoder_sample_service *service);
#else
static inline int encoder_sample_service_register_shell_instance(
	struct encoder_sample_service *service)
{
	ARG_UNUSED(service);
	return 0;
}
#endif

#endif /* ENCODER_SAMPLE_SERVICE_INTERNAL_H_ */
