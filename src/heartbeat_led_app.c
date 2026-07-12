#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(heartbeat_led_app, LOG_LEVEL_INF);

#define HEARTBEAT_LED_NODE DT_ALIAS(heartbeat_led)

BUILD_ASSERT(DT_NODE_HAS_STATUS(HEARTBEAT_LED_NODE, okay),
	     "Missing heartbeat-led alias");

#define HEARTBEAT_LED_STACK_SIZE 512
#define HEARTBEAT_LED_PRIORITY 7
#define HEARTBEAT_LED_ON_TIME_MS 1000
#define HEARTBEAT_LED_OFF_TIME_MS 1000

static const struct gpio_dt_spec heartbeat_led =
	GPIO_DT_SPEC_GET(HEARTBEAT_LED_NODE, gpios);

static void heartbeat_led_thread(void *p1, void *p2, void *p3)
{
	int err;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(&heartbeat_led)) {
		LOG_ERR("Heartbeat LED GPIO controller is not ready");
		return;
	}

	err = gpio_pin_configure_dt(&heartbeat_led, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Failed to configure heartbeat LED: %d", err);
		return;
	}

	LOG_INF("Heartbeat LED started on PD10, 1s on / 1s off");

	while (1) {
		gpio_pin_set_dt(&heartbeat_led, 1);
		k_sleep(K_MSEC(HEARTBEAT_LED_ON_TIME_MS));

		gpio_pin_set_dt(&heartbeat_led, 0);
		k_sleep(K_MSEC(HEARTBEAT_LED_OFF_TIME_MS));
	}
}

K_THREAD_DEFINE(heartbeat_led_tid, HEARTBEAT_LED_STACK_SIZE,
		heartbeat_led_thread, NULL, NULL, NULL,
		HEARTBEAT_LED_PRIORITY, 0, 0);
