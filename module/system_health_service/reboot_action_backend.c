#include "system_health_service.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(system_health_service, CONFIG_LOG_DEFAULT_LEVEL);

struct reboot_action_backend_state {
	bool pending;
	uint32_t request_ms;
	uint16_t source_event;
};

static struct reboot_action_backend_state reboot_state;

static void reboot_action_backend_action(
	const struct sys_health_action_context *context, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!reboot_state.pending) {
		reboot_state.pending = true;
		reboot_state.request_ms = k_uptime_get_32();
		reboot_state.source_event = context->source_event;
		LOG_WRN("System health reboot action requested: source_event=%u",
			reboot_state.source_event);
	}
}

static void reboot_action_backend_periodic(uint32_t now_ms, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!reboot_state.pending ||
	    (uint32_t)(now_ms - reboot_state.request_ms) <
		    CONFIG_SYS_HEALTH_REBOOT_ACTION_DELAY_MS) {
		return;
	}

	sys_reboot(SYS_REBOOT_COLD);
}

static int reboot_action_backend_register(void)
{
	int err;

	err = sys_health_action_handler_register(
		SYS_HEALTH_ACTION_REQUEST_REBOOT,
		reboot_action_backend_action, NULL);
	if (err != 0) {
		return err;
	}

	return sys_health_periodic_handler_register(
		reboot_action_backend_periodic, NULL);
}

SYS_INIT(reboot_action_backend_register, APPLICATION, 90);
