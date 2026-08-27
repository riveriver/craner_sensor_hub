#include "anemometer_sample_service_internal.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(anemometer_sample_service, CONFIG_LOG_DEFAULT_LEVEL);

const char *anemometer_sample_service_name(
	const struct anemometer_sample_service *service)
{
	if (service == NULL || service->config == NULL ||
	    service->config->name == NULL) {
		return "anemometer";
	}

	return service->config->name;
}

static void anemometer_sample_service_notify(
	struct anemometer_sample_service *service, int err,
	const struct anemometer_sample_service_sample *sample)
{
	if (service->callback != NULL) {
		service->callback(service, err, sample, service->user_data);
	}
}

static void anemometer_sample_service_thread(
	void *arg1, void *arg2, void *arg3)
{
	struct anemometer_sample_service *service = arg1;
	int err;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_thread_name_set(k_current_get(),
			  anemometer_sample_service_name(service));

	if (service->config->start_delay_ms > 0U) {
		k_sleep(K_MSEC(service->config->start_delay_ms));
	}

	while (true) {
		struct anemometer_sample_service_sample sample;
		struct anemometer_sample_service_sample notify_sample;
		uint32_t start_cycles;
		uint32_t elapsed_cycles;
		uint32_t read_duration_us;

		if (!service->client_ready) {
			err = service->config->backend->init(
				service->config->backend_client,
				service->config->backend_config);
			if (err != 0) {
				anemometer_sample_service_update_error(
					service, err);
				anemometer_sample_service_notify(
					service, err, NULL);
				LOG_WRN_RATELIMIT("%s init failed: %d",
					anemometer_sample_service_name(service),
					err);
				k_sleep(K_MSEC(service->config->period_ms));
				continue;
			}

			service->client_ready = true;
			LOG_INF("%s sampling started: backend=%s iface=%s period=%u ms",
				anemometer_sample_service_name(service),
				service->config->backend->name,
				service->config->iface_name,
				service->config->period_ms);
		}

		start_cycles = k_cycle_get_32();
		err = service->config->backend->fetch(
			service->config->backend_client, &sample);
		elapsed_cycles = k_cycle_get_32() - start_cycles;
		read_duration_us = (uint32_t)k_cyc_to_us_floor64(elapsed_cycles);

		if (err == 0) {
			anemometer_sample_service_update_success(
				service, &sample, read_duration_us,
				&notify_sample);
			anemometer_sample_service_notify(service, 0,
							 &notify_sample);
		} else {
			anemometer_sample_service_update_error(service, err);
			anemometer_sample_service_notify(service, err, NULL);
			LOG_WRN_RATELIMIT("%s read failed: %d",
				anemometer_sample_service_name(service), err);
#if defined(CONFIG_ANEMOMETER_SAMPLE_SERVICE_REINIT_ON_ENODEV)
			if (err == -ENODEV) {
				if (service->config->backend->reset != NULL) {
					service->config->backend->reset(
						service->config->backend_client);
				}
				service->client_ready = false;
			}
#endif
		}

		k_sleep(K_MSEC(service->config->period_ms));
	}
}

int anemometer_sample_service_start(
	struct anemometer_sample_service *service,
	const struct anemometer_sample_service_config *config,
	anemometer_sample_service_callback_t callback, void *user_data)
{
	int err;

	if (service == NULL || config == NULL || config->name == NULL ||
	    config->iface_name == NULL || config->period_ms == 0U ||
	    config->backend == NULL || config->backend->init == NULL ||
	    config->backend->fetch == NULL ||
	    config->backend_client == NULL ||
	    config->backend_config == NULL) {
		return -EINVAL;
	}

	if (service->started) {
		return -EALREADY;
	}

	memset(service, 0, sizeof(*service));
	service->config = config;
	service->callback = callback;
	service->user_data = user_data;

	err = anemometer_sample_service_register_shell_instance(service);
	if (err != 0) {
		return err;
	}

	k_thread_create(&service->thread, service->stack,
			K_KERNEL_STACK_SIZEOF(service->stack),
			anemometer_sample_service_thread, service, NULL, NULL,
			CONFIG_ANEMOMETER_SAMPLE_SERVICE_THREAD_PRIORITY,
			0, K_NO_WAIT);

	service->started = true;

	return 0;
}

void anemometer_sample_service_get_latest(
	struct anemometer_sample_service *service,
	struct anemometer_sample_service_sample *sample)
{
	k_spinlock_key_t key;

	if (service == NULL || sample == NULL) {
		return;
	}

	key = k_spin_lock(&service->lock);
	*sample = service->latest_sample;
	k_spin_unlock(&service->lock, key);
}

void anemometer_sample_service_get_stats(
	struct anemometer_sample_service *service,
	struct anemometer_sample_service_stats *stats)
{
	if (service == NULL || stats == NULL) {
		return;
	}

#if !defined(CONFIG_ANEMOMETER_SAMPLE_SERVICE_STATS)
	memset(stats, 0, sizeof(*stats));
	return;
#endif

	k_spinlock_key_t key;

	key = k_spin_lock(&service->lock);
	*stats = service->latest_stats;
	k_spin_unlock(&service->lock, key);
}
