#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define LOG_APP_SENSOR_STACK_SIZE 1024
#define LOG_APP_SENSOR_PRIORITY 5
#define LOG_APP_SENSOR_PERIOD K_MSEC(5000)

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_DBG);

static void log_app_sensor_thread(void)
{
	while (1) {
		LOG_ERR("sensor error sample");
		LOG_WRN("sensor warning sample");
		LOG_INF("sensor info sample");
		LOG_DBG("sensor debug sample");
		k_sleep(LOG_APP_SENSOR_PERIOD);
	}
}

K_THREAD_DEFINE(log_app_sensor_tid, LOG_APP_SENSOR_STACK_SIZE, log_app_sensor_thread,
		NULL, NULL, NULL, LOG_APP_SENSOR_PRIORITY, 0, 0);
