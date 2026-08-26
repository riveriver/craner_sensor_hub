#include "system_health_service.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(system_health_service, CONFIG_LOG_DEFAULT_LEVEL);

#define STATUS_LED_NODE DT_ALIAS(heartbeat_led)

BUILD_ASSERT(DT_NODE_HAS_STATUS(STATUS_LED_NODE, okay),
	     "Missing heartbeat-led alias for status LED");

struct led_display_context {
	bool has_offline_event;
	uint8_t priority;
};

enum led_indicator_mode {
	LED_INDICATOR_MODE_AUTO,
	LED_INDICATOR_MODE_FORCE_ON,
	LED_INDICATOR_MODE_FORCE_OFF,
};

static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
static enum led_indicator_mode led_indicator_mode;
static bool led_indicator_initialized;
static bool led_indicator_output_active;
static uint32_t led_indicator_update_count;
static int led_indicator_last_error;

static int status_led_set(bool on)
{
	int err = gpio_pin_set_dt(&status_led, on ? 1 : 0);

	led_indicator_last_error = err;
	if (err != 0) {
		LOG_WRN_RATELIMIT("Failed to set status LED: %d", err);
		return err;
	}

	led_indicator_output_active = on;
	led_indicator_update_count++;
	return 0;
}

static void status_led_show_normal(uint32_t now_ms)
{
	uint32_t cycle_ms = CONFIG_SYS_HEALTH_NORMAL_ON_MS +
			    CONFIG_SYS_HEALTH_NORMAL_OFF_MS;
	uint32_t phase_ms;

	if (cycle_ms == 0U) {
		status_led_set(false);
		return;
	}

	phase_ms = now_ms % cycle_ms;
	status_led_set(phase_ms < CONFIG_SYS_HEALTH_NORMAL_ON_MS);
}

static void status_led_show_error(uint32_t now_ms, uint8_t priority)
{
	uint32_t blink_window_ms = (uint32_t)priority * 2U *
				   CONFIG_SYS_HEALTH_ERROR_BLINK_MS;
	uint32_t cycle_ms = blink_window_ms +
			    CONFIG_SYS_HEALTH_ERROR_PAUSE_MS;
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

	status_led_set(((phase_ms /
			 CONFIG_SYS_HEALTH_ERROR_BLINK_MS) %
			2U) == 0U);
}

static void collect_display_event_cb(
	const struct sys_health_event_status *status, void *user_data)
{
	struct led_display_context *context = user_data;

	if (!status->offline) {
		return;
	}

	if (!context->has_offline_event ||
	    status->priority < context->priority) {
		context->has_offline_event = true;
		context->priority = status->priority;
	}
}

static int led_indicator_backend_init(void)
{
	int err;

	if (led_indicator_initialized) {
		return 0;
	}

	if (!gpio_is_ready_dt(&status_led)) {
		led_indicator_last_error = -ENODEV;
		LOG_ERR_RATELIMIT("Status LED GPIO controller is not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	led_indicator_last_error = err;
	if (err != 0) {
		LOG_ERR_RATELIMIT("Failed to configure status LED: %d", err);
		return err;
	}

	led_indicator_initialized = true;
	led_indicator_output_active = false;
	LOG_INF("System health LED indicator backend started");
	return 0;
}

static void led_indicator_backend_periodic(uint32_t now_ms, void *user_data)
{
	struct led_display_context context = {
		.priority = UINT8_MAX,
	};

	ARG_UNUSED(user_data);

	if (led_indicator_backend_init() != 0) {
		return;
	}

	if (led_indicator_mode == LED_INDICATOR_MODE_FORCE_ON) {
		status_led_set(true);
		return;
	}
	if (led_indicator_mode == LED_INDICATOR_MODE_FORCE_OFF) {
		status_led_set(false);
		return;
	}

	(void)sys_health_event_foreach(collect_display_event_cb, &context);
	if (context.has_offline_event) {
		status_led_show_error(now_ms, context.priority);
	} else {
		status_led_show_normal(now_ms);
	}
}

static int led_indicator_backend_register(void)
{
	int rc = sys_health_periodic_handler_register(
		led_indicator_backend_periodic, NULL);

	if (rc != 0) {
		LOG_ERR("Register LED indicator backend failed: %d", rc);
	}

	return rc;
}

SYS_INIT(led_indicator_backend_register, APPLICATION, 90);

#ifdef CONFIG_SHELL
static const char *led_indicator_mode_name(void)
{
	switch (led_indicator_mode) {
	case LED_INDICATOR_MODE_AUTO:
		return "auto";
	case LED_INDICATOR_MODE_FORCE_ON:
		return "force_on";
	case LED_INDICATOR_MODE_FORCE_OFF:
		return "force_off";
	default:
		return "unknown";
	}
}

static int cmd_health_led_status(const struct shell *shell, size_t argc,
				 char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    "led initialized=%s mode=%s active=%s update_count=%u last_error=%d device=%s pin=%u flags=0x%08x",
		    led_indicator_initialized ? "yes" : "no",
		    led_indicator_mode_name(),
		    led_indicator_output_active ? "yes" : "no",
		    led_indicator_update_count,
		    led_indicator_last_error,
		    status_led.port->name,
		    status_led.pin,
		    status_led.dt_flags);

	return 0;
}

static int cmd_health_led_on(const struct shell *shell, size_t argc,
			     char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = led_indicator_backend_init();
	if (rc != 0) {
		shell_error(shell, "LED init failed: %d", rc);
		return rc;
	}

	led_indicator_mode = LED_INDICATOR_MODE_FORCE_ON;
	rc = status_led_set(true);
	if (rc != 0) {
		shell_error(shell, "LED set failed: %d", rc);
		return rc;
	}

	shell_print(shell, "led mode=force_on");
	return 0;
}

static int cmd_health_led_off(const struct shell *shell, size_t argc,
			      char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = led_indicator_backend_init();
	if (rc != 0) {
		shell_error(shell, "LED init failed: %d", rc);
		return rc;
	}

	led_indicator_mode = LED_INDICATOR_MODE_FORCE_OFF;
	rc = status_led_set(false);
	if (rc != 0) {
		shell_error(shell, "LED set failed: %d", rc);
		return rc;
	}

	shell_print(shell, "led mode=force_off");
	return 0;
}

static int cmd_health_led_auto(const struct shell *shell, size_t argc,
			       char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	led_indicator_mode = LED_INDICATOR_MODE_AUTO;
	shell_print(shell, "led mode=auto");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(health_led_cmds,
	SHELL_CMD(status, NULL, "Print health LED backend status.",
		  cmd_health_led_status),
	SHELL_CMD(on, NULL, "Force health LED on.", cmd_health_led_on),
	SHELL_CMD(off, NULL, "Force health LED off.", cmd_health_led_off),
	SHELL_CMD(auto, NULL, "Return health LED to automatic mode.",
		  cmd_health_led_auto),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(health_led, &health_led_cmds,
		   "System health LED indicator commands.", NULL);
#endif
