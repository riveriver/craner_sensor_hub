#ifndef ANEMOMETER_SAMPLE_SERVICE_INTERNAL_H_
#define ANEMOMETER_SAMPLE_SERVICE_INTERNAL_H_

#include "anemometer_sample_service.h"

const char *anemometer_sample_service_name(
	const struct anemometer_sample_service *service);
void anemometer_sample_service_update_success(
	struct anemometer_sample_service *service,
	const struct anemometer_sample_service_sample *sample,
	uint32_t read_duration_us,
	struct anemometer_sample_service_sample *notify_sample);
void anemometer_sample_service_update_error(
	struct anemometer_sample_service *service, int err);

#if defined(CONFIG_ANEMOMETER_SAMPLE_SERVICE_SHELL)
int anemometer_sample_service_register_shell_instance(
	struct anemometer_sample_service *service);
#else
static inline int anemometer_sample_service_register_shell_instance(
	struct anemometer_sample_service *service)
{
	ARG_UNUSED(service);
	return 0;
}
#endif

#endif /* ANEMOMETER_SAMPLE_SERVICE_INTERNAL_H_ */
