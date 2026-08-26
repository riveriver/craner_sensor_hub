#include "system_health_service.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(system_health_service, CONFIG_LOG_DEFAULT_LEVEL);

#define WATCHDOG_NODE DT_ALIAS(watchdog0)
#define WATCHDOG_NO_CHANNEL -1

BUILD_ASSERT(DT_NODE_HAS_STATUS(WATCHDOG_NODE, okay),
	     "Missing enabled watchdog0 alias");

static const struct device *const watchdog = DEVICE_DT_GET(WATCHDOG_NODE);
static int watchdog_channel_id = WATCHDOG_NO_CHANNEL;

static int watchdog_feed_backend_init(void)
{
	struct wdt_timeout_cfg config = {
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = CONFIG_SYS_HEALTH_WDT_TIMEOUT_MS,
		},
	};
	int err;

	if (!device_is_ready(watchdog)) {
		LOG_ERR("Watchdog device %s is not ready", watchdog->name);
		return -ENODEV;
	}

	watchdog_channel_id = wdt_install_timeout(watchdog, &config);
	if (watchdog_channel_id < 0) {
		LOG_ERR("Failed to install watchdog timeout: %d",
			watchdog_channel_id);
		return watchdog_channel_id;
	}

	err = wdt_setup(watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (err == -ENOTSUP) {
		err = wdt_setup(watchdog, 0);
	}

	if (err < 0) {
		LOG_ERR("Failed to setup watchdog: %d", err);
		return err;
	}

	LOG_INF("System health watchdog feed backend started on %s, timeout=%d ms",
		watchdog->name, CONFIG_SYS_HEALTH_WDT_TIMEOUT_MS);
	return 0;
}

static void watchdog_feed_backend_action(
	const struct sys_health_action_context *context, void *user_data)
{
	ARG_UNUSED(context);
	ARG_UNUSED(user_data);

	LOG_WRN("System health watchdog feed stop requested");
}

static void watchdog_feed_backend_periodic(uint32_t now_ms, void *user_data)
{
	static bool initialized;
	static uint32_t last_feed_ms;
	int err;

	ARG_UNUSED(user_data);

	if (!initialized && watchdog_feed_backend_init() != 0) {
		return;
	}
	initialized = true;

	if (sys_health_watchdog_feed_stop_is_requested() ||
	    watchdog_channel_id == WATCHDOG_NO_CHANNEL ||
	    (uint32_t)(now_ms - last_feed_ms) <
		    CONFIG_SYS_HEALTH_WATCHDOG_FEED_INTERVAL_MS) {
		return;
	}
	last_feed_ms = now_ms;

	err = wdt_feed(watchdog, watchdog_channel_id);
	if (err < 0) {
		LOG_ERR_RATELIMIT("Failed to feed watchdog: %d", err);
	}
}

static int watchdog_feed_backend_register(void)
{
	int err;

	err = sys_health_action_handler_register(
		SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED,
		watchdog_feed_backend_action, NULL);
	if (err != 0) {
		return err;
	}

	return sys_health_periodic_handler_register(
		watchdog_feed_backend_periodic, NULL);
}

SYS_INIT(watchdog_feed_backend_register, APPLICATION, 90);
