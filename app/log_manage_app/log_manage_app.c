#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>

#define LOG_FILTER_THREAD_STACK_SIZE 768
#define LOG_FILTER_THREAD_PRIORITY 10
#define LOG_FILTER_START_DELAY_MS 1500
#define LOG_FILTER_RETRY_PERIOD_MS 1000
#define LOG_FILTER_RETRY_COUNT 5

static void set_log_source_runtime_level(const char *name, uint32_t level)
{
	int source_id = log_source_id_get(name);

	if (source_id < 0) {
		return;
	}

	(void)log_filter_set(NULL, 0, source_id, level);
}

static void apply_modbus_runtime_log_filter(void)
{
	static const char *const modbus_log_sources[] = {
		"modbus", "modbus_c", "modbus_s", "modbus_serial", "modbus_raw",
	};

	for (size_t i = 0; i < ARRAY_SIZE(modbus_log_sources); i++) {
		set_log_source_runtime_level(modbus_log_sources[i], LOG_LEVEL_ERR);
	}
	set_log_source_runtime_level("modbus_tcp_server", LOG_LEVEL_WRN);
}

static void log_runtime_filter_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_msleep(LOG_FILTER_START_DELAY_MS);

	for (uint32_t i = 0; i < LOG_FILTER_RETRY_COUNT; i++) {
		apply_modbus_runtime_log_filter();
		k_msleep(LOG_FILTER_RETRY_PERIOD_MS);
	}
}

K_THREAD_DEFINE(log_runtime_filter_tid, LOG_FILTER_THREAD_STACK_SIZE,
		log_runtime_filter_thread, NULL, NULL, NULL,
		LOG_FILTER_THREAD_PRIORITY, 0, 0);
