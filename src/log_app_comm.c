#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define LOG_APP_COMM_STACK_SIZE 1024
#define LOG_APP_COMM_PRIORITY 5
#define LOG_APP_COMM_PERIOD K_MSEC(5000)

LOG_MODULE_REGISTER(comm, LOG_LEVEL_DBG);

static void log_app_comm_thread(void)
{
	while (1) {
		LOG_ERR("comm error sample");
		LOG_WRN("comm warning sample");
		LOG_INF("comm info sample");
		LOG_DBG("comm debug sample");
		k_sleep(LOG_APP_COMM_PERIOD);
	}
}

// K_THREAD_DEFINE(log_app_comm_tid, LOG_APP_COMM_STACK_SIZE, log_app_comm_thread,
// 		NULL, NULL, NULL, LOG_APP_COMM_PRIORITY, 0, 0);
