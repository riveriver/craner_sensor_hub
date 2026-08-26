#include "anemometer_sample_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/slist.h>

LOG_MODULE_REGISTER(anemometer_sample_service, CONFIG_LOG_DEFAULT_LEVEL);

static sys_slist_t service_list = SYS_SLIST_STATIC_INIT(&service_list);
static K_MUTEX_DEFINE(service_list_lock);

struct anemometer_sample_service_node {
	sys_snode_t node;
	struct anemometer_sample_service *service;
};

static struct anemometer_sample_service_node service_nodes[
	CONFIG_ANEMOMETER_SAMPLE_SERVICE_MAX_INSTANCES];

static const char *anemometer_sample_service_name(
	const struct anemometer_sample_service *service)
{
	if (service == NULL || service->config == NULL ||
	    service->config->name == NULL) {
		return "anemometer";
	}

	return service->config->name;
}

static void anemometer_sample_service_update_success(
	struct anemometer_sample_service *service,
	const struct anemometer_sample_service_sample *sample,
	uint32_t read_duration_us,
	struct anemometer_sample_service_sample *notify_sample)
{
	k_spinlock_key_t key;
	uint32_t success_count;

	key = k_spin_lock(&service->lock);

	success_count = service->latest_stats.success_count + 1U;
	service->latest_stats.success_count = success_count;
	service->latest_stats.consecutive_error_count = 0;
	service->latest_stats.last_error = 0;
	service->latest_stats.last_success_uptime_ms = k_uptime_get();
	service->latest_stats.success_total_time_us += read_duration_us;
	service->latest_stats.success_avg_time_us =
		(uint32_t)(service->latest_stats.success_total_time_us /
			   success_count);
	if (read_duration_us > service->latest_stats.success_max_time_us) {
		service->latest_stats.success_max_time_us = read_duration_us;
	}

	service->latest_sample.online = true;
	service->latest_sample.seq++;
	service->latest_sample.status =
		ANEMOMETER_SAMPLE_SERVICE_STATUS_ONLINE;
	service->latest_sample.raw_reg_count = sample->raw_reg_count;
	for (uint8_t i = 0U;
	     i < sample->raw_reg_count &&
	     i < ARRAY_SIZE(service->latest_sample.raw_regs);
	     i++) {
		service->latest_sample.raw_regs[i] = sample->raw_regs[i];
	}
	service->latest_sample.temperature = sample->temperature;
	service->latest_sample.humidity = sample->humidity;
	service->latest_sample.pressure = sample->pressure;
	service->latest_sample.wind_speed = sample->wind_speed;
	service->latest_sample.wind_direction = sample->wind_direction;
	service->latest_sample.sample_uptime_ms =
		service->latest_stats.last_success_uptime_ms;
	service->latest_sample.read_duration_us = read_duration_us;
	service->latest_sample.last_error = 0;
	*notify_sample = service->latest_sample;

	k_spin_unlock(&service->lock, key);
}

static void anemometer_sample_service_update_error(
	struct anemometer_sample_service *service, int err)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&service->lock);
	service->latest_sample.online = false;
	service->latest_sample.status &=
		~ANEMOMETER_SAMPLE_SERVICE_STATUS_ONLINE;
	service->latest_sample.last_error = err;
	service->latest_stats.error_count++;
	service->latest_stats.consecutive_error_count++;
	service->latest_stats.last_error = err;
	service->latest_stats.last_fault_error = err;
	service->latest_stats.last_error_uptime_ms = k_uptime_get();
	if (service->latest_stats.consecutive_error_count >
	    service->latest_stats.max_consecutive_error_count) {
		service->latest_stats.max_consecutive_error_count =
			service->latest_stats.consecutive_error_count;
	}
	k_spin_unlock(&service->lock, key);
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
			if (err == -ENODEV) {
				if (service->config->backend->reset != NULL) {
					service->config->backend->reset(
						service->config->backend_client);
				}
				service->client_ready = false;
			}
		}

		k_sleep(K_MSEC(service->config->period_ms));
	}
}

static int anemometer_sample_service_register_shell_instance(
	struct anemometer_sample_service *service)
{
	k_mutex_lock(&service_list_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(service_nodes); i++) {
		if (service_nodes[i].service == NULL) {
			service_nodes[i].service = service;
			sys_slist_append(&service_list, &service_nodes[i].node);
			k_mutex_unlock(&service_list_lock);
			return 0;
		}
	}
	k_mutex_unlock(&service_list_lock);

	return -ENOMEM;
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
	k_spinlock_key_t key;

	if (service == NULL || stats == NULL) {
		return;
	}

	key = k_spin_lock(&service->lock);
	*stats = service->latest_stats;
	k_spin_unlock(&service->lock, key);
}

static void anemometer_sample_service_print_stats(
	const struct shell *shell,
	struct anemometer_sample_service *service)
{
	struct anemometer_sample_service_stats stats;
	uint32_t total_count;
	uint32_t fail_rate_x100 = 0U;

	anemometer_sample_service_get_stats(service, &stats);

	total_count = stats.success_count + stats.error_count;
	if (total_count > 0U) {
		fail_rate_x100 =
			(uint32_t)((((uint64_t)stats.error_count * 10000U) +
				    (total_count / 2U)) / total_count);
	}

	shell_print(shell, "%-12s %3u.%02u%% %7u/%-7u %8u %8u %d",
		    anemometer_sample_service_name(service),
		    fail_rate_x100 / 100U, fail_rate_x100 % 100U,
		    stats.error_count, total_count,
		    (stats.success_avg_time_us + 500U) / 1000U,
		    (stats.success_max_time_us + 500U) / 1000U,
		    stats.last_fault_error);
}

static int cmd_anemometer_stats(const struct shell *shell, size_t argc,
				char **argv)
{
	sys_snode_t *snode;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    "name         fail_rate   fail/total      avg_ms   max_ms last_fault");
	shell_print(shell,
		    "------------------------------------------------------------------");

	k_mutex_lock(&service_list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_NODE(&service_list, snode) {
		struct anemometer_sample_service_node *node =
			CONTAINER_OF(snode,
				     struct anemometer_sample_service_node,
				     node);

		anemometer_sample_service_print_stats(shell, node->service);
	}
	k_mutex_unlock(&service_list_lock);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(anemometer_cmds,
	SHELL_CMD(stats, NULL, "Show anemometer sample statistics.",
		  cmd_anemometer_stats),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(anemometer, &anemometer_cmds,
		   "Anemometer sample service commands.", NULL);
