#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <errno.h>

LOG_MODULE_REGISTER(power_control_app, CONFIG_LOG_DEFAULT_LEVEL);

#define LOAD_SWITCH_3V3_NODE DT_ALIAS(load_switch_3v3)
#define LOAD_SWITCH_5V_NODE DT_ALIAS(load_switch_5v)
#define LOAD_SWITCH_NET_BRIDGE_NODE DT_ALIAS(load_switch_net_bridge)

BUILD_ASSERT(DT_NODE_HAS_STATUS(LOAD_SWITCH_3V3_NODE, okay),
	     "Missing load-switch-3v3 alias");
BUILD_ASSERT(DT_NODE_HAS_STATUS(LOAD_SWITCH_5V_NODE, okay),
	     "Missing load-switch-5v alias");
BUILD_ASSERT(DT_NODE_HAS_STATUS(LOAD_SWITCH_NET_BRIDGE_NODE, okay),
	     "Missing load-switch-net-bridge alias");

struct power_control_gpio {
	const char *name;
	struct gpio_dt_spec gpio;
};

static const struct power_control_gpio power_control_gpios[] = {
	{
		.name = "LOAD_SWITCH_3V3",
		.gpio = GPIO_DT_SPEC_GET(LOAD_SWITCH_3V3_NODE, gpios),
	},
	{
		.name = "LOAD_SWITCH_5V",
		.gpio = GPIO_DT_SPEC_GET(LOAD_SWITCH_5V_NODE, gpios),
	},
	{
		.name = "LOAD_SWITCH_NET_BRIDGE",
		.gpio = GPIO_DT_SPEC_GET(LOAD_SWITCH_NET_BRIDGE_NODE, gpios),
	},
};

static int power_control_app_init(void)
{
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(power_control_gpios); i++) {
		const struct power_control_gpio *power = &power_control_gpios[i];

		if (!gpio_is_ready_dt(&power->gpio)) {
			LOG_ERR("%s GPIO controller is not ready", power->name);
			return -ENODEV;
		}

		err = gpio_pin_configure_dt(&power->gpio, GPIO_OUTPUT_ACTIVE);
		if (err != 0) {
			LOG_ERR("Failed to enable %s: %d", power->name, err);
			return err;
		}

		LOG_INF("%s enabled", power->name);
	}

	return 0;
}

SYS_INIT(power_control_app_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
