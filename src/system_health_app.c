#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>

#include "system_health_app.h"
#include "system_health_event_table.h"

LOG_MODULE_REGISTER(system_health_app, LOG_LEVEL_INF);

#define STATUS_LED_NODE DT_ALIAS(heartbeat_led)
#define WATCHDOG_NODE DT_ALIAS(watchdog0)

BUILD_ASSERT(DT_NODE_HAS_STATUS(STATUS_LED_NODE, okay),
	     "Missing heartbeat-led alias for status LED");
BUILD_ASSERT(DT_NODE_HAS_STATUS(WATCHDOG_NODE, okay),
	     "Missing enabled watchdog0 alias");

#define SYSTEM_HEALTH_CHECK_INTERVAL_MS 100U		 
#define SYSTEM_HEALTH_STACK_SIZE 1536
#define SYSTEM_HEALTH_PRIORITY 5
#define SYSTEM_HEALTH_NO_WATCHDOG_CHANNEL -1
#define STATUS_LED_NORMAL_ON_MS 500U
#define STATUS_LED_NORMAL_OFF_MS 500U
#define STATUS_LED_ERROR_BLINK_MS 150U
#define STATUS_LED_ERROR_PAUSE_MS 3000U
#define SYSTEM_HEALTH_TIME_REPORT_INTERVAL_MS 6000U

struct system_health_event_state {
	bool configured;
	bool enable;
	uint8_t priority;
	uint32_t offline_timeout_ms;
	uint32_t update_timestamp_ms;
	bool offline_status;
	bool last_offline_status;
	const struct system_health_event_obj *event_obj;
	system_health_event_callback_t offline_first_func;
	system_health_event_callback_t offline_func;
	system_health_event_callback_t online_first_func;
	system_health_event_callback_t online_func;
};

static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
static const struct device *const watchdog = DEVICE_DT_GET(WATCHDOG_NODE);

static K_MUTEX_DEFINE(system_health_lock);
static struct system_health_event_state event_states[SYSTEM_HEALTH_EVENT_MAX];
static int watchdog_channel_id = SYSTEM_HEALTH_NO_WATCHDOG_CHANNEL;

static bool system_health_is_valid_event(enum system_health_event event)
{
	return event > SYSTEM_HEALTH_NONE && event < SYSTEM_HEALTH_EVENT_MAX;
}

void system_health_update_event(enum system_health_event event)
{
	if (!system_health_is_valid_event(event)) {
		return;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	if (event_states[event].configured) {
		event_states[event].update_timestamp_ms = k_uptime_get_32();
	}
	k_mutex_unlock(&system_health_lock);
}

void system_health_enable_event(enum system_health_event event)
{
	if (!system_health_is_valid_event(event)) {
		return;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	if (event_states[event].configured) {
		event_states[event].enable = true;
		event_states[event].update_timestamp_ms = k_uptime_get_32();
		event_states[event].offline_status = false;
		event_states[event].last_offline_status = false;
	}
	k_mutex_unlock(&system_health_lock);
}

void system_health_disable_event(enum system_health_event event)
{
	if (!system_health_is_valid_event(event)) {
		return;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	if (event_states[event].configured) {
		event_states[event].enable = false;
		event_states[event].offline_status = false;
		event_states[event].last_offline_status = false;
	}
	k_mutex_unlock(&system_health_lock);
}

bool system_health_is_event_offline(enum system_health_event event)
{
	bool offline_status = false;

	if (!system_health_is_valid_event(event)) {
		return false;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	if (event_states[event].configured) {
		offline_status = event_states[event].offline_status;
	}
	k_mutex_unlock(&system_health_lock);

	return offline_status;
}

static int system_health_watchdog_init(void)
{
	struct wdt_timeout_cfg config = {
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = CONFIG_CRANER_SYSTEM_HEALTH_WDT_TIMEOUT_MS,
		},
	};
	int err;

	if (!device_is_ready(watchdog)) {
		LOG_ERR("Watchdog device %s is not ready", watchdog->name);
		return -ENODEV;
	}

	watchdog_channel_id = wdt_install_timeout(watchdog, &config);
	if (watchdog_channel_id < 0) {
		LOG_ERR("Failed to install watchdog timeout: %d", watchdog_channel_id);
		return watchdog_channel_id;
	}

	err = wdt_setup(watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (err == -ENOTSUP) {
		LOG_WRN("Watchdog debug halt pause is not supported, starting without it");
		err = wdt_setup(watchdog, 0);
	}

	if (err < 0) {
		LOG_ERR("Failed to setup watchdog: %d", err);
		return err;
	}

	LOG_INF("System health watchdog started on %s, timeout=%d ms",
		watchdog->name, CONFIG_CRANER_SYSTEM_HEALTH_WDT_TIMEOUT_MS);

	return 0;
}

static void system_health_watchdog_feed(enum system_health_event display_event,
					uint8_t priority)
{
	int err;

	if (display_event != SYSTEM_HEALTH_NONE &&
	    priority <= SYSTEM_HEALTH_WATCHDOG_STOP_PRIORITY) {
		return;
	}

	if (watchdog_channel_id == SYSTEM_HEALTH_NO_WATCHDOG_CHANNEL) {
		return;
	}

	err = wdt_feed(watchdog, watchdog_channel_id);
	if (err < 0) {
		LOG_ERR("Failed to feed watchdog: %d", err);
	}
}

static void system_health_events_init(void)
{
	uint32_t now_ms = k_uptime_get_32();

	memset(event_states, 0, sizeof(event_states));

	for (int i = 0; i < system_health_event_table_size; i++) {
		enum system_health_event event = system_health_event_table[i].event;
		struct system_health_event_state *state;

		if (!system_health_is_valid_event(event)) {
			LOG_WRN("Ignoring invalid system health event: %d", event);
			continue;
		}

		state = &event_states[event];
		state->configured = true;
		state->enable = system_health_event_table[i].enable;
		state->priority = system_health_event_table[i].priority;
		state->offline_timeout_ms =
			system_health_event_table[i].offline_timeout_ms;
		state->update_timestamp_ms = now_ms;
		state->offline_status = false;
		state->last_offline_status = false;
		state->event_obj = &system_health_event_table[i];
		state->offline_first_func =
			system_health_event_table[i].offline_first_func;
		state->offline_func = system_health_event_table[i].offline_func;
		state->online_first_func =
			system_health_event_table[i].online_first_func;
		state->online_func = system_health_event_table[i].online_func;
	}

	LOG_INF("System health event table ready, events=%d",
		system_health_event_table_size);
}

static void system_health_run_callback(struct system_health_event_state *state)
{
	if (state->offline_status) {
		if (!state->last_offline_status) {
			if (state->offline_first_func != NULL) {
				state->offline_first_func(state->event_obj);
			}
		} else if (state->offline_func != NULL) {
			state->offline_func(state->event_obj);
		}
	} else {
		if (state->last_offline_status) {
			if (state->online_first_func != NULL) {
				state->online_first_func(state->event_obj);
			}
		} else if (state->online_func != NULL) {
			state->online_func(state->event_obj);
		}
	}

	state->last_offline_status = state->offline_status;
}

static enum system_health_event system_health_update_events(uint32_t now_ms,
							    uint8_t *priority)
{
	enum system_health_event display_event = SYSTEM_HEALTH_NONE;
	uint8_t display_priority = UINT8_MAX;

	k_mutex_lock(&system_health_lock, K_FOREVER);

	for (enum system_health_event event = SYSTEM_HEALTH_NONE + 1;
	     event < SYSTEM_HEALTH_EVENT_MAX; event++) {
		struct system_health_event_state *state = &event_states[event];

		if (!state->configured || !state->enable) {
			continue;
		}

		state->offline_status =
			(uint32_t)(now_ms - state->update_timestamp_ms) >
			state->offline_timeout_ms;

		system_health_run_callback(state);

		if (state->offline_status && state->priority < display_priority) {
			display_priority = state->priority;
			display_event = event;
		}
	}

	k_mutex_unlock(&system_health_lock);

	*priority = display_priority;

	return display_event;
}

static void status_led_set(bool on)
{
	gpio_pin_set_dt(&status_led, on ? 1 : 0);
}

static void status_led_show_normal(uint32_t now_ms)
{
	uint32_t cycle_ms = STATUS_LED_NORMAL_ON_MS +
			    STATUS_LED_NORMAL_OFF_MS;
	uint32_t phase_ms;

	if (cycle_ms == 0U) {
		status_led_set(false);
		return;
	}

	phase_ms = now_ms % cycle_ms;
	status_led_set(phase_ms < STATUS_LED_NORMAL_ON_MS);
}

static void status_led_show_error(uint32_t now_ms, uint8_t priority)
{
	uint32_t blink_window_ms = (uint32_t)priority * 2U *
				   STATUS_LED_ERROR_BLINK_MS;
	uint32_t cycle_ms = blink_window_ms +
			    STATUS_LED_ERROR_PAUSE_MS;
	uint32_t phase_ms;

	if (priority == 0U || cycle_ms == 0U) {
		status_led_set(false);
		return;
	}

	phase_ms = now_ms % cycle_ms;
	if (phase_ms >= blink_window_ms) {
		status_led_set(false);
		return;
	}

	status_led_set(((phase_ms / STATUS_LED_ERROR_BLINK_MS) %
			2U) == 0U);
}

static void system_health_report_device_time(uint32_t now_ms,
					     uint32_t *last_report_ms)
{
	if ((uint32_t)(now_ms - *last_report_ms) <
	    SYSTEM_HEALTH_TIME_REPORT_INTERVAL_MS) {
		return;
	}

	*last_report_ms = now_ms;
	LOG_INF("System health alive: uptime_ms=%u", now_ms);
}

static void system_health_thread(void *p1, void *p2, void *p3)
{
	uint32_t last_time_report_ms;
	int err;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_ERR("Status LED GPIO controller is not ready");
		return;
	}

	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Failed to configure status LED: %d", err);
		return;
	}

	system_health_events_init();

	err = system_health_watchdog_init();
	if (err != 0) {
		return;
	}

	LOG_INF("System health monitor started, status LED normal=%d/%d ms, error blink=%d ms, pause=%d ms",
		STATUS_LED_NORMAL_ON_MS,
		STATUS_LED_NORMAL_OFF_MS,
		STATUS_LED_ERROR_BLINK_MS,
		STATUS_LED_ERROR_PAUSE_MS);

	last_time_report_ms = k_uptime_get_32();

	while (1) {
		uint32_t now_ms = k_uptime_get_32();
		uint8_t priority;
		enum system_health_event display_event =
			system_health_update_events(now_ms, &priority);

		if (display_event == SYSTEM_HEALTH_NONE) {
			status_led_show_normal(now_ms);
		} else {
			status_led_show_error(now_ms, priority);
		}

		system_health_report_device_time(now_ms, &last_time_report_ms);
		system_health_watchdog_feed(display_event, priority);

		k_sleep(K_MSEC(SYSTEM_HEALTH_CHECK_INTERVAL_MS));
	}
}

K_THREAD_DEFINE(system_health_tid, SYSTEM_HEALTH_STACK_SIZE,
		system_health_thread, NULL, NULL, NULL,
		SYSTEM_HEALTH_PRIORITY, 0, 0);
