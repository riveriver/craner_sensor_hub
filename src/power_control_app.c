#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <errno.h>

LOG_MODULE_REGISTER(power_control_app, CONFIG_LOG_DEFAULT_LEVEL);

#define POWER_3V3_AND_CCTV_NODE DT_ALIAS(power_3v3_and_cctv)
#define POWER_5V_NODE DT_ALIAS(power_5v)
#define POWER_NET_BRIGDE_NODE DT_ALIAS(power_net_brigde)

BUILD_ASSERT(DT_NODE_HAS_STATUS(POWER_3V3_AND_CCTV_NODE, okay),
	     "Missing power-3v3-and-cctv alias");
BUILD_ASSERT(DT_NODE_HAS_STATUS(POWER_5V_NODE, okay),
	     "Missing power-5v alias");
BUILD_ASSERT(DT_NODE_HAS_STATUS(POWER_NET_BRIGDE_NODE, okay),
	     "Missing power-net-brigde alias");

struct power_control_gpio {
	const char *name;
	struct gpio_dt_spec gpio;
};

static const struct power_control_gpio power_control_gpios[] = {
	{
		.name = "POWER_3V3_AND_CCTV",
		.gpio = GPIO_DT_SPEC_GET(POWER_3V3_AND_CCTV_NODE, gpios),
	},
	{
		.name = "POWER_5V",
		.gpio = GPIO_DT_SPEC_GET(POWER_5V_NODE, gpios),
	},
	{
		.name = "POWER_NET_BRIGDE",
		.gpio = GPIO_DT_SPEC_GET(POWER_NET_BRIGDE_NODE, gpios),
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
