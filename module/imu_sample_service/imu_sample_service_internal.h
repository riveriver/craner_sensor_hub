#ifndef IMU_SAMPLE_SERVICE_INTERNAL_H_
#define IMU_SAMPLE_SERVICE_INTERNAL_H_

#include "imu_sample_service.h"

const char *imu_sample_service_name(
	const struct imu_sample_service *service);
void imu_sample_service_update_success(
	struct imu_sample_service *service,
	const struct imu_sample_service_sample *sample,
	uint32_t read_duration_us,
	struct imu_sample_service_sample *notify_sample);
void imu_sample_service_update_error(
	struct imu_sample_service *service, int err);

#if defined(CONFIG_IMU_SAMPLE_SERVICE_SHELL)
int imu_sample_service_register_shell_instance(
	struct imu_sample_service *service);
#else
static inline int imu_sample_service_register_shell_instance(
	struct imu_sample_service *service)
{
	ARG_UNUSED(service);
	return 0;
}
#endif

#endif /* IMU_SAMPLE_SERVICE_INTERNAL_H_ */
