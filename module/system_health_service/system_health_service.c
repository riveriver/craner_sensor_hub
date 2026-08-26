#include "system_health_service.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SYS_HEALTH_SCHEDULED_REBOOT
#include "scheduled_reboot_policy.h"
#endif

LOG_MODULE_REGISTER(system_health_service, CONFIG_LOG_DEFAULT_LEVEL);

#define SYSTEM_HEALTH_STACK_SIZE 3072
#define SYSTEM_HEALTH_PRIORITY 5
#define SYSTEM_HEALTH_NAME_LEN 24U

struct system_health_action_callback_slot {
	sys_health_action_callback_t callback;
	void *user_data;
};

struct system_health_action_handler_slot {
	uint32_t action_mask;
	sys_health_action_handler_t handler;
	void *user_data;
};

struct system_health_periodic_handler_slot {
	sys_health_periodic_handler_t handler;
	void *user_data;
};

struct system_health_event_state {
	struct sys_health_event obj;
	char name[SYSTEM_HEALTH_NAME_LEN];
	bool configured;
	bool offline_status;
	bool last_offline_status;
	bool manual_offline;
	bool system_protect_triggered;
	bool action_triggered;
	uint32_t update_timestamp_ms;
	uint32_t offline_since_ms;
	uint32_t offline_count;
	uint32_t recover_count;
	uint32_t last_offline_duration_ms;
	uint32_t max_offline_duration_ms;
	uint32_t diag_flags;
};

static K_MUTEX_DEFINE(system_health_lock);
static struct system_health_event_state event_states[
	CONFIG_SYS_HEALTH_MAX_EVENTS];
static bool system_protect_latched;
static bool system_degraded_latched;
static bool reboot_requested_latched;
static bool watchdog_feed_stop_requested;
static bool service_initialized;
static uint16_t system_protect_source_event;
static struct sys_health_time_provider time_provider;
static struct system_health_action_callback_slot action_callbacks[
	CONFIG_SYS_HEALTH_MAX_ACTION_CALLBACKS];
static struct system_health_action_handler_slot action_handlers[
	CONFIG_SYS_HEALTH_MAX_ACTION_HANDLERS];
static struct system_health_periodic_handler_slot periodic_handlers[
	CONFIG_SYS_HEALTH_MAX_PERIODIC_HANDLERS];
static K_SEM_DEFINE(system_health_init_sem, 0, 1);

static void register_builtin_events(void);
static void run_action_callbacks(
	const struct sys_health_action_context *context);

int sys_health_init(
	const struct sys_health_event *event_table,
	int event_count,
	const struct sys_health_time_provider *new_time_provider)
{
	int rc;

	if (event_count < 0 || (event_count > 0 && event_table == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	if (service_initialized) {
		k_mutex_unlock(&system_health_lock);
		return -EALREADY;
	}
	if (new_time_provider != NULL) {
		time_provider = *new_time_provider;
	}
	k_mutex_unlock(&system_health_lock);

	register_builtin_events();

	for (int i = 0; i < event_count; i++) {
		rc = sys_health_event_register(&event_table[i]);
		if (rc != 0) {
			LOG_ERR("Register health event failed: event=%u rc=%d",
				event_table[i].event, rc);
			return rc;
		}
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	service_initialized = true;
	k_mutex_unlock(&system_health_lock);

	k_sem_give(&system_health_init_sem);
	return 0;
}

static struct system_health_event_state *find_event_locked(uint16_t event)
{
	for (size_t i = 0; i < ARRAY_SIZE(event_states); i++) {
		if (event_states[i].configured &&
		    event_states[i].obj.event == event) {
			return &event_states[i];
		}
	}

	return NULL;
}

static void fill_event_status(
	const struct system_health_event_state *state,
	struct sys_health_event_status *status)
{
	uint32_t now_ms = k_uptime_get_32();

	memset(status, 0, sizeof(*status));
	status->event = state->obj.event;
	status->name = state->obj.name;
	status->configured = state->configured;
	status->enable = state->obj.enable;
	status->offline = state->offline_status;
	status->system_protect_triggered = state->system_protect_triggered;
	status->action_triggered = state->action_triggered;
	status->priority = state->obj.priority;
	status->offline_timeout_ms = state->obj.offline_timeout_ms;
	status->offline_since_ms = state->offline_since_ms;
	status->last_update_ms = state->update_timestamp_ms;
	status->action_mask = state->obj.action_mask;
	status->action_delay_ms = state->obj.action_delay_ms;
	status->offline_count = state->offline_count;
	status->recover_count = state->recover_count;
	if (state->offline_status && state->offline_since_ms != 0U) {
		status->current_offline_duration_ms =
			now_ms - state->offline_since_ms;
	}
	status->last_offline_duration_ms = state->last_offline_duration_ms;
	status->max_offline_duration_ms = state->max_offline_duration_ms;
	status->diag_flags = state->diag_flags;
}

int sys_health_event_register(
	const struct sys_health_event *event_obj)
{
	struct system_health_event_state *state = NULL;
	uint32_t now_ms = k_uptime_get_32();

	if (event_obj == NULL || event_obj->event == 0U ||
	    event_obj->priority == 0U || event_obj->offline_timeout_ms == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);

	state = find_event_locked(event_obj->event);
	if (state == NULL) {
		for (size_t i = 0; i < ARRAY_SIZE(event_states); i++) {
			if (!event_states[i].configured) {
				state = &event_states[i];
				break;
			}
		}
	}

	if (state == NULL) {
		k_mutex_unlock(&system_health_lock);
		return -ENOMEM;
	}

	memset(state, 0, sizeof(*state));
	state->obj = *event_obj;
	state->configured = true;
	state->update_timestamp_ms = now_ms;

	if (event_obj->name != NULL) {
		(void)snprintk(state->name, sizeof(state->name), "%s",
			       event_obj->name);
		state->obj.name = state->name;
	} else {
		(void)snprintk(state->name, sizeof(state->name), "event_%u",
			       event_obj->event);
		state->obj.name = state->name;
	}

	k_mutex_unlock(&system_health_lock);

	return 0;
}

int sys_health_policy_register(const struct sys_health_policy *policy)
{
	struct system_health_event_state *state;

	if (policy == NULL || policy->event == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(policy->event);
	if (state == NULL) {
		k_mutex_unlock(&system_health_lock);
		return -ENOENT;
	}

	state->obj.action_mask = policy->action_mask;
	state->obj.action_delay_ms = policy->action_delay_ms;
	state->system_protect_triggered = false;
	state->action_triggered = false;
	k_mutex_unlock(&system_health_lock);

	return 0;
}

int sys_health_action_callback_register(
	sys_health_action_callback_t callback, void *user_data)
{
	if (callback == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(action_callbacks); i++) {
		if (action_callbacks[i].callback == NULL) {
			action_callbacks[i].callback = callback;
			action_callbacks[i].user_data = user_data;
			k_mutex_unlock(&system_health_lock);
			return 0;
		}
	}
	k_mutex_unlock(&system_health_lock);

	return -ENOMEM;
}

int sys_health_action_handler_register(uint32_t action_mask,
	sys_health_action_handler_t handler, void *user_data)
{
	if (handler == NULL || action_mask == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(action_handlers); i++) {
		if (action_handlers[i].handler == NULL) {
			action_handlers[i].action_mask = action_mask;
			action_handlers[i].handler = handler;
			action_handlers[i].user_data = user_data;
			k_mutex_unlock(&system_health_lock);
			return 0;
		}
	}
	k_mutex_unlock(&system_health_lock);

	return -ENOMEM;
}

int sys_health_periodic_handler_register(
	sys_health_periodic_handler_t handler, void *user_data)
{
	if (handler == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(periodic_handlers); i++) {
		if (periodic_handlers[i].handler == NULL) {
			periodic_handlers[i].handler = handler;
			periodic_handlers[i].user_data = user_data;
			k_mutex_unlock(&system_health_lock);
			return 0;
		}
	}
	k_mutex_unlock(&system_health_lock);

	return -ENOMEM;
}

void sys_health_action_request(uint32_t action_mask, uint16_t source_event,
	const char *source_name)
{
	struct sys_health_action_context context = {
		.source_event = source_event,
		.source_name = source_name,
		.action_mask = action_mask,
	};

	if (action_mask == 0U) {
		return;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	if ((action_mask & SYS_HEALTH_ACTION_SET_DEGRADED) != 0U) {
		system_degraded_latched = true;
		system_protect_latched = true;
		system_protect_source_event = source_event;
	}
	if ((action_mask & SYS_HEALTH_ACTION_REQUEST_REBOOT) != 0U) {
		reboot_requested_latched = true;
		if (system_protect_source_event == 0U) {
			system_protect_source_event = source_event;
		}
	}
	if ((action_mask & SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED) != 0U) {
		watchdog_feed_stop_requested = true;
		if (system_protect_source_event == 0U) {
			system_protect_source_event = source_event;
		}
	}
	k_mutex_unlock(&system_health_lock);

	if ((action_mask & SYS_HEALTH_ACTION_LOG) != 0U) {
		LOG_WRN("Health action requested: source_event=%u name=%s actions=0x%08x",
			source_event, source_name != NULL ? source_name : "",
			action_mask);
	}

	run_action_callbacks(&context);
}

int sys_health_time_provider_register(
	const struct sys_health_time_provider *new_time_provider)
{
	if (new_time_provider == NULL ||
	    new_time_provider->get_unix_time_s == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	time_provider = *new_time_provider;
	k_mutex_unlock(&system_health_lock);

	return 0;
}

int sys_health_time_get_unix_s(int64_t *unix_time_s)
{
	struct sys_health_time_provider provider;

	if (unix_time_s == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	provider = time_provider;
	k_mutex_unlock(&system_health_lock);

	if (provider.get_unix_time_s == NULL) {
		return -ENODATA;
	}

	return provider.get_unix_time_s(unix_time_s, provider.user_data);
}

void sys_health_event_report(uint16_t event)
{
	struct system_health_event_state *state;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(event);
	if (state != NULL) {
		state->update_timestamp_ms = k_uptime_get_32();
		state->manual_offline = false;
	}
	k_mutex_unlock(&system_health_lock);
}

void sys_health_event_enable(uint16_t event)
{
	struct system_health_event_state *state;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(event);
	if (state != NULL) {
		state->obj.enable = true;
		state->update_timestamp_ms = k_uptime_get_32();
		state->manual_offline = false;
		state->offline_status = false;
		state->last_offline_status = false;
		state->diag_flags &= ~SYS_HEALTH_DIAG_TEST_FAILED;
	}
	k_mutex_unlock(&system_health_lock);
}

void sys_health_event_disable(uint16_t event)
{
	struct system_health_event_state *state;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(event);
	if (state != NULL) {
		state->obj.enable = false;
		state->manual_offline = false;
		state->offline_status = false;
		state->last_offline_status = false;
		state->offline_since_ms = 0U;
		state->diag_flags &= ~SYS_HEALTH_DIAG_TEST_FAILED;
	}
	k_mutex_unlock(&system_health_lock);
}

void sys_health_event_set(uint16_t event, bool offline)
{
	struct system_health_event_state *state;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(event);
	if (state != NULL) {
		state->manual_offline = offline;
		if (!offline) {
			state->update_timestamp_ms = k_uptime_get_32();
		}
	}
	k_mutex_unlock(&system_health_lock);
}

bool sys_health_event_is_offline(uint16_t event)
{
	struct system_health_event_state *state;
	bool offline = false;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(event);
	if (state != NULL) {
		offline = state->offline_status;
	}
	k_mutex_unlock(&system_health_lock);

	return offline;
}

int sys_health_event_get(uint16_t event,
	struct sys_health_event_status *status)
{
	struct system_health_event_state *state;

	if (status == NULL || event == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	state = find_event_locked(event);
	if (state == NULL) {
		k_mutex_unlock(&system_health_lock);
		return -ENOENT;
	}

	fill_event_status(state, status);
	k_mutex_unlock(&system_health_lock);

	return 0;
}

void sys_health_protect_set(uint16_t source_event)
{
	struct system_health_event_state *state;
	uint32_t now_ms = k_uptime_get_32();

	k_mutex_lock(&system_health_lock, K_FOREVER);
	system_protect_latched = true;
	system_protect_source_event = source_event;

	state = find_event_locked(SYS_HEALTH_EVENT_SYSTEM_PROTECT);
	if (state != NULL) {
		state->manual_offline = true;
		if (state->offline_since_ms == 0U) {
			state->offline_since_ms = now_ms;
		}
		if (!state->offline_status) {
			state->offline_count++;
		}
		state->offline_status = true;
		state->diag_flags |= SYS_HEALTH_DIAG_TEST_FAILED |
				     SYS_HEALTH_DIAG_FAILED_THIS_CYCLE |
				     SYS_HEALTH_DIAG_PENDING;
		if (state->offline_count >= 2U) {
			state->diag_flags |= SYS_HEALTH_DIAG_CONFIRMED;
		}
	}
	k_mutex_unlock(&system_health_lock);
}

bool sys_health_protect_is_active(void)
{
	bool protected_state;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	protected_state = system_protect_latched;
	k_mutex_unlock(&system_health_lock);

	return protected_state;
}

bool sys_health_watchdog_feed_stop_is_requested(void)
{
	bool requested;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	requested = watchdog_feed_stop_requested;
	k_mutex_unlock(&system_health_lock);

	return requested;
}

void sys_health_protect_get_status(
	struct sys_health_protect_status *status)
{
	if (status == NULL) {
		return;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	status->active = system_protect_latched;
	status->degraded = system_degraded_latched;
	status->reboot_requested = reboot_requested_latched;
	status->watchdog_feed_stop_requested = watchdog_feed_stop_requested;
	status->source_event = system_protect_source_event;
	k_mutex_unlock(&system_health_lock);
}

int sys_health_event_foreach(
	sys_health_event_status_cb_t cb, void *user_data)
{
	struct sys_health_event_status status;

	if (cb == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&system_health_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(event_states); i++) {
		if (!event_states[i].configured) {
			continue;
		}

		fill_event_status(&event_states[i], &status);
		cb(&status, user_data);
	}
	k_mutex_unlock(&system_health_lock);

	return 0;
}

bool sys_health_is_initialized(void)
{
	bool initialized;

	k_mutex_lock(&system_health_lock, K_FOREVER);
	initialized = service_initialized;
	k_mutex_unlock(&system_health_lock);

	return initialized;
}

#ifndef CONFIG_SYS_HEALTH_ICMP_PROBE
int sys_health_probe_foreach(
	sys_health_probe_status_cb_t cb, void *user_data)
{
	if (cb == NULL) {
		return -EINVAL;
	}

	ARG_UNUSED(user_data);
	return 0;
}
#endif

static void run_event_callback(struct system_health_event_state *state)
{
	struct sys_health_event_status status;

	fill_event_status(state, &status);

	if (state->offline_status) {
		if (!state->last_offline_status) {
			if (state->obj.offline_first_func != NULL) {
				state->obj.offline_first_func(&status);
			}
		} else if (state->obj.offline_func != NULL) {
			state->obj.offline_func(&status);
		}
	} else {
		if (state->last_offline_status) {
			if (state->obj.online_first_func != NULL) {
				state->obj.online_first_func(&status);
			}
		} else if (state->obj.online_func != NULL) {
			state->obj.online_func(&status);
		}
	}

	state->last_offline_status = state->offline_status;
}

static void set_system_degraded_locked(struct system_health_event_state *source)
{
	struct system_health_event_state *system_event;

	if (system_protect_latched) {
		return;
	}

	system_protect_latched = true;
	system_protect_source_event = source->obj.event;
	source->system_protect_triggered = true;

	system_event = find_event_locked(SYS_HEALTH_EVENT_SYSTEM_PROTECT);
	if (system_event != NULL) {
		system_event->manual_offline = true;
		if (system_event->offline_since_ms == 0U) {
			system_event->offline_since_ms = k_uptime_get_32();
		}
	}

	LOG_ERR("System protect latched by event=%u name=%s",
		source->obj.event, source->obj.name);
}

static void apply_event_actions_locked(struct system_health_event_state *source)
{
	uint32_t actions = source->obj.action_mask;

	source->action_triggered = true;
	source->system_protect_triggered =
		(actions & SYS_HEALTH_ACTION_SET_DEGRADED) != 0U;

	if ((actions & SYS_HEALTH_ACTION_LOG) != 0U) {
		LOG_WRN("Health event action: event=%u name=%s actions=0x%08x",
			source->obj.event, source->obj.name, actions);
	}

	if ((actions & SYS_HEALTH_ACTION_SET_DEGRADED) != 0U) {
		system_degraded_latched = true;
		set_system_degraded_locked(source);
	}

	if ((actions & SYS_HEALTH_ACTION_REQUEST_REBOOT) != 0U) {
		reboot_requested_latched = true;
	}

	if ((actions & SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED) != 0U) {
		watchdog_feed_stop_requested = true;
	}
}

static void run_action_callbacks(
	const struct sys_health_action_context *context)
{
	struct system_health_action_callback_slot callbacks[
		CONFIG_SYS_HEALTH_MAX_ACTION_CALLBACKS];
	struct system_health_action_handler_slot handlers[
		CONFIG_SYS_HEALTH_MAX_ACTION_HANDLERS];

	k_mutex_lock(&system_health_lock, K_FOREVER);
	memcpy(callbacks, action_callbacks, sizeof(callbacks));
	memcpy(handlers, action_handlers, sizeof(handlers));
	k_mutex_unlock(&system_health_lock);

	if ((context->action_mask & SYS_HEALTH_ACTION_CALLBACK) != 0U) {
		for (size_t i = 0; i < ARRAY_SIZE(callbacks); i++) {
			if (callbacks[i].callback != NULL) {
				callbacks[i].callback(
					context, callbacks[i].user_data);
			}
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].handler != NULL &&
		    (handlers[i].action_mask & context->action_mask) != 0U) {
			handlers[i].handler(context, handlers[i].user_data);
		}
	}
}

static void run_periodic_handlers(uint32_t now_ms)
{
	struct system_health_periodic_handler_slot handlers[
		CONFIG_SYS_HEALTH_MAX_PERIODIC_HANDLERS];

	k_mutex_lock(&system_health_lock, K_FOREVER);
	memcpy(handlers, periodic_handlers, sizeof(handlers));
	k_mutex_unlock(&system_health_lock);

	for (size_t i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].handler != NULL) {
			handlers[i].handler(now_ms, handlers[i].user_data);
		}
	}
}

static uint16_t update_events(uint32_t now_ms, uint8_t *priority)
{
	uint16_t display_event = 0U;
	uint8_t display_priority = UINT8_MAX;
	struct sys_health_action_context action_contexts[
		CONFIG_SYS_HEALTH_MAX_EVENTS];
	size_t action_context_count = 0U;

	k_mutex_lock(&system_health_lock, K_FOREVER);

	for (size_t i = 0; i < ARRAY_SIZE(event_states); i++) {
		struct system_health_event_state *state = &event_states[i];
		bool offline;
		bool was_offline;

		if (!state->configured || !state->obj.enable) {
			continue;
		}

		was_offline = state->offline_status;
		offline = state->manual_offline ||
			  ((uint32_t)(now_ms - state->update_timestamp_ms) >
			   state->obj.offline_timeout_ms);

		if (offline && !was_offline) {
			state->offline_since_ms = now_ms;
			state->offline_count++;
			state->diag_flags |= SYS_HEALTH_DIAG_TEST_FAILED |
					     SYS_HEALTH_DIAG_FAILED_THIS_CYCLE |
					     SYS_HEALTH_DIAG_PENDING;
			if (state->offline_count >= 2U) {
				state->diag_flags |=
					SYS_HEALTH_DIAG_CONFIRMED;
			}
		} else if (!offline && was_offline) {
			state->recover_count++;
			if (state->offline_since_ms != 0U) {
				state->last_offline_duration_ms =
					now_ms - state->offline_since_ms;
				if (state->last_offline_duration_ms >
				    state->max_offline_duration_ms) {
					state->max_offline_duration_ms =
						state->last_offline_duration_ms;
				}
			}
			state->offline_since_ms = 0U;
			state->system_protect_triggered = false;
			state->action_triggered = false;
			state->diag_flags &= ~SYS_HEALTH_DIAG_TEST_FAILED;
		}

		state->offline_status = offline;
		if (state->offline_status && state->offline_since_ms != 0U) {
			uint32_t current_duration =
				now_ms - state->offline_since_ms;

			if (current_duration > state->max_offline_duration_ms) {
				state->max_offline_duration_ms =
					current_duration;
			}
		}

		if (state->offline_status && state->obj.action_mask != 0U &&
		    !state->action_triggered &&
		    (uint32_t)(now_ms - state->offline_since_ms) >=
			    state->obj.action_delay_ms) {
			apply_event_actions_locked(state);

			if (action_context_count < ARRAY_SIZE(action_contexts)) {
				action_contexts[action_context_count++] =
					(struct sys_health_action_context) {
						.source_event = state->obj.event,
						.source_name = state->obj.name,
						.action_mask = state->obj.action_mask,
					};
			}
		}

		run_event_callback(state);

		if (state->offline_status &&
		    state->obj.priority < display_priority) {
			display_priority = state->obj.priority;
			display_event = state->obj.event;
		}
	}

	k_mutex_unlock(&system_health_lock);

	for (size_t i = 0; i < action_context_count; i++) {
		run_action_callbacks(&action_contexts[i]);
	}

	*priority = display_priority;
	return display_event;
}

static void log_offline_event(
	const struct sys_health_event_status *status)
{
	LOG_ERR("System health event offline: event=%u name=%s priority=%u timeout=%u ms offline_count=%u",
		status->event,
		status->name != NULL ? status->name : "",
		status->priority,
		status->offline_timeout_ms,
		status->offline_count);
}

static void log_online_event(
	const struct sys_health_event_status *status)
{
	LOG_ERR("System health event recovered: event=%u name=%s offline_duration=%u ms recover_count=%u max_offline=%u ms",
		status->event,
		status->name != NULL ? status->name : "",
		status->last_offline_duration_ms,
		status->recover_count,
		status->max_offline_duration_ms);
}

static void log_system_protect_watchdog_pending(
	const struct sys_health_event_status *status)
{
	LOG_ERR_RATELIMIT_RATE(
		5000,
		"System protect is offline: source_event=%u offline_duration=%u ms, watchdog reset is pending",
		system_protect_source_event,
		status->current_offline_duration_ms);
}

static void register_builtin_events(void)
{
	static const struct sys_health_event system_event = {
		.event = SYS_HEALTH_EVENT_SYSTEM_PROTECT,
		.name = "system_protect",
		.enable = true,
		.priority = 1,
		.offline_timeout_ms = UINT32_MAX,
		.offline_first_func = log_offline_event,
		.offline_func = log_system_protect_watchdog_pending,
		.online_first_func = log_online_event,
	};
	(void)sys_health_event_register(&system_event);
}

static void health_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_sem_take(&system_health_init_sem, K_FOREVER);

	LOG_INF("System health service started, events=%d",
		CONFIG_SYS_HEALTH_MAX_EVENTS);

	while (1) {
		uint32_t now_ms = k_uptime_get_32();
		uint8_t priority;

#ifdef CONFIG_SYS_HEALTH_SCHEDULED_REBOOT
		scheduled_reboot_policy_check(now_ms);
#endif

		(void)update_events(now_ms, &priority);
		run_periodic_handlers(now_ms);

		k_sleep(K_MSEC(CONFIG_SYS_HEALTH_CHECK_INTERVAL_MS));
	}
}

static void count_health_event_cb(
	const struct sys_health_event_status *status, void *user_data)
{
	size_t *count = user_data;

	ARG_UNUSED(status);
	(*count)++;
}

static void count_health_probe_cb(
	const struct sys_health_probe_status *status, void *user_data)
{
	size_t *count = user_data;

	ARG_UNUSED(status);
	(*count)++;
}

static int cmd_health_status(const struct shell *shell, size_t argc,
			     char **argv)
{
	struct sys_health_protect_status protect_status;
	size_t event_count = 0U;
	size_t probe_count = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sys_health_protect_get_status(&protect_status);
	(void)sys_health_event_foreach(count_health_event_cb, &event_count);
	(void)sys_health_probe_foreach(count_health_probe_cb, &probe_count);

	shell_print(shell, "service_initialized: %s",
		    service_initialized ? "yes" : "no");
	shell_print(shell, "events: %u", (uint32_t)event_count);
	shell_print(shell, "probes: %u", (uint32_t)probe_count);
	shell_print(shell, "protect_active: %s",
		    protect_status.active ? "yes" : "no");
	shell_print(shell, "degraded: %s",
		    protect_status.degraded ? "yes" : "no");
	shell_print(shell, "reboot_requested: %s",
		    protect_status.reboot_requested ? "yes" : "no");
	shell_print(shell, "watchdog_feed_stop_requested: %s",
		    protect_status.watchdog_feed_stop_requested ? "yes" : "no");
	shell_print(shell, "protect_source_event: %u",
		    protect_status.source_event);

	return 0;
}

static const char *yes_no(bool value)
{
	return value ? "yes" : "no";
}

static void format_ms(char *buf, size_t len, uint32_t ms)
{
	if (ms == UINT32_MAX) {
		(void)snprintk(buf, len, "never");
	} else if (ms >= 3600000U) {
		(void)snprintk(buf, len, "%uh%02um",
			       ms / 3600000U, (ms / 60000U) % 60U);
	} else if (ms >= 60000U) {
		(void)snprintk(buf, len, "%um%02us",
			       ms / 60000U, (ms / 1000U) % 60U);
	} else if (ms >= 1000U) {
		(void)snprintk(buf, len, "%u.%03us",
			       ms / 1000U, ms % 1000U);
	} else {
		(void)snprintk(buf, len, "%ums", ms);
	}
}

static void format_diag_flags(char *buf, size_t len, uint32_t flags)
{
	(void)snprintk(buf, len, "%c%c%c%c",
		       (flags & SYS_HEALTH_DIAG_TEST_FAILED) != 0U ? 'F' : '-',
		       (flags & SYS_HEALTH_DIAG_FAILED_THIS_CYCLE) != 0U ? 'C' : '-',
		       (flags & SYS_HEALTH_DIAG_PENDING) != 0U ? 'P' : '-',
		       (flags & SYS_HEALTH_DIAG_CONFIRMED) != 0U ? 'D' : '-');
}

static void shell_print_event_status(const struct shell *shell,
				     const struct sys_health_event_status *s)
{
	char timeout[16];
	char offline_age[16];
	char last_update[16];
	char current[16];
	char last[16];
	char max[16];
	char flags[8];

	format_ms(timeout, sizeof(timeout), s->offline_timeout_ms);
	format_ms(offline_age, sizeof(offline_age), s->offline_since_ms);
	format_ms(last_update, sizeof(last_update), s->last_update_ms);
	format_ms(current, sizeof(current), s->current_offline_duration_ms);
	format_ms(last, sizeof(last), s->last_offline_duration_ms);
	format_ms(max, sizeof(max), s->max_offline_duration_ms);
	format_diag_flags(flags, sizeof(flags), s->diag_flags);

	shell_print(shell, "id: %u", s->event);
	shell_print(shell, "name: %s", s->name != NULL ? s->name : "");
	shell_print(shell, "configured: %s", yes_no(s->configured));
	shell_print(shell, "enabled: %s", yes_no(s->enable));
	shell_print(shell, "offline: %s", yes_no(s->offline));
	shell_print(shell, "priority: %u", s->priority);
	shell_print(shell, "offline_timeout: %s", timeout);
	shell_print(shell, "offline_since: %s", offline_age);
	shell_print(shell, "last_update: %s", last_update);
	shell_print(shell, "actions: mask=0x%08x delay_ms=%u triggered=%s protect_triggered=%s",
		    s->action_mask,
		    s->action_delay_ms,
		    yes_no(s->action_triggered),
		    yes_no(s->system_protect_triggered));
	shell_print(shell,
		    "diag: flags=%s offline_count=%u recover_count=%u current=%s last=%s max=%s",
		    flags,
		    s->offline_count,
		    s->recover_count,
		    current,
		    last,
		    max);
	shell_print(shell, "diag_flags: F=current_failed C=failed_this_cycle P=pending D=confirmed");
}

static void cmd_health_events_cb(
	const struct sys_health_event_status *status, void *user_data)
{
	const struct shell *shell = user_data;
	char timeout[16];
	char age[16];
	char last_update[16];
	char flags[8];

	format_ms(timeout, sizeof(timeout), status->offline_timeout_ms);
	format_ms(age, sizeof(age), status->current_offline_duration_ms);
	format_ms(last_update, sizeof(last_update), status->last_update_ms);
	format_diag_flags(flags, sizeof(flags), status->diag_flags);

	shell_print(shell,
		    "[%3u] %-22s %-6s %-7s %4u %8s %8s %8s %4s",
		    status->event,
		    status->name != NULL ? status->name : "",
		    yes_no(status->enable),
		    yes_no(status->offline),
		    status->priority,
		    timeout,
		    age,
		    last_update,
		    flags);
}

static int cmd_health_events(const struct shell *shell, size_t argc,
			     char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    " id   name                   enable offline prio  timeout      age     last diag");
	return sys_health_event_foreach(cmd_health_events_cb, (void *)shell);
}

static void cmd_health_stats_cb(
	const struct sys_health_event_status *status, void *user_data)
{
	const struct shell *shell = user_data;
	char current[16];
	char last[16];
	char max[16];
	char flags[8];

	format_ms(current, sizeof(current), status->current_offline_duration_ms);
	format_ms(last, sizeof(last), status->last_offline_duration_ms);
	format_ms(max, sizeof(max), status->max_offline_duration_ms);
	format_diag_flags(flags, sizeof(flags), status->diag_flags);

	shell_print(shell,
		    "[%3u] %-22s %7u %7u %8s %8s %8s %4s",
		    status->event,
		    status->name != NULL ? status->name : "",
		    status->offline_count,
		    status->recover_count,
		    current,
		    last,
		    max,
		    flags);
}

static int cmd_health_stats(const struct shell *shell, size_t argc,
			    char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    " id   name                     count recover  current     last      max diag");
	shell_print(shell,
		    "diag: F=current_failed C=failed_this_cycle P=pending D=confirmed");
	return sys_health_event_foreach(cmd_health_stats_cb, (void *)shell);
}

static int parse_u16_arg(const char *text, uint16_t *value)
{
	char *end;
	unsigned long parsed;

	if (text == NULL || value == NULL) {
		return -EINVAL;
	}

	parsed = strtoul(text, &end, 0);
	if (*text == '\0' || *end != '\0' || parsed > UINT16_MAX) {
		return -EINVAL;
	}

	*value = (uint16_t)parsed;
	return 0;
}

static int cmd_health_event(const struct shell *shell, size_t argc,
			    char **argv)
{
	struct sys_health_event_status status;
	uint16_t event;
	int rc;

	if (argc != 2U) {
		shell_error(shell, "usage: health event <id>");
		return -EINVAL;
	}

	rc = parse_u16_arg(argv[1], &event);
	if (rc != 0) {
		shell_error(shell, "invalid event id: %s", argv[1]);
		return rc;
	}

	rc = sys_health_event_get(event, &status);
	if (rc != 0) {
		shell_error(shell, "event not found: id=%u rc=%d",
			    event, rc);
		return rc;
	}

	shell_print_event_status(shell, &status);
	return 0;
}

static int cmd_health_enable(const struct shell *shell, size_t argc,
			     char **argv)
{
	struct sys_health_event_status status;
	uint16_t event;
	int rc;

	if (argc != 2U) {
		shell_error(shell, "usage: health enable <id>");
		return -EINVAL;
	}

	rc = parse_u16_arg(argv[1], &event);
	if (rc != 0) {
		shell_error(shell, "invalid event id: %s", argv[1]);
		return rc;
	}

	rc = sys_health_event_get(event, &status);
	if (rc != 0) {
		shell_error(shell, "event not found: id=%u rc=%d",
			    event, rc);
		return rc;
	}

	sys_health_event_enable(event);
	(void)sys_health_event_get(event, &status);
	shell_print(shell, "event enabled: id=%u name=%s",
		    status.event, status.name != NULL ? status.name : "");
	return 0;
}

static int cmd_health_disable(const struct shell *shell, size_t argc,
			      char **argv)
{
	struct sys_health_event_status status;
	uint16_t event;
	int rc;

	if (argc != 2U) {
		shell_error(shell, "usage: health disable <id>");
		return -EINVAL;
	}

	rc = parse_u16_arg(argv[1], &event);
	if (rc != 0) {
		shell_error(shell, "invalid event id: %s", argv[1]);
		return rc;
	}

	rc = sys_health_event_get(event, &status);
	if (rc != 0) {
		shell_error(shell, "event not found: id=%u rc=%d",
			    event, rc);
		return rc;
	}

	sys_health_event_disable(event);
	(void)sys_health_event_get(event, &status);
	shell_print(shell, "event disabled: id=%u name=%s",
		    status.event, status.name != NULL ? status.name : "");
	return 0;
}

static int cmd_health_protect(const struct shell *shell, size_t argc,
			      char **argv)
{
	struct sys_health_protect_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sys_health_protect_get_status(&status);
	shell_print(shell,
		    "protect active=%s degraded=%s reboot_requested=%s watchdog_feed_stop_requested=%s source_event=%u",
		    status.active ? "yes" : "no",
		    status.degraded ? "yes" : "no",
		    status.reboot_requested ? "yes" : "no",
		    status.watchdog_feed_stop_requested ? "yes" : "no",
		    status.source_event);
	return 0;
}

static void cmd_health_probes_cb(
	const struct sys_health_probe_status *status, void *user_data)
{
	const struct shell *shell = user_data;

	shell_print(shell,
		    "probe name=%s event=%u enabled=%s initialized=%s target_valid=%s target=%s period_ms=%u timeout_ms=%u max_failures=%u consecutive_failures=%u last_error=%d last_sequence=%u last_check_uptime_ms=%lld",
		    status->name != NULL ? status->name : "",
		    status->event,
		    status->enabled ? "yes" : "no",
		    status->initialized ? "yes" : "no",
		    status->target_valid ? "yes" : "no",
		    status->target != NULL ? status->target : "",
		    status->period_ms,
		    status->timeout_ms,
		    status->max_consecutive_failures,
		    status->consecutive_failures,
		    status->last_error,
		    status->last_sequence,
		    (long long)status->last_check_uptime_ms);
}

static int cmd_health_probes(const struct shell *shell, size_t argc,
			     char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return sys_health_probe_foreach(cmd_health_probes_cb,
					(void *)shell);
}

SHELL_STATIC_SUBCMD_SET_CREATE(system_health_cmds,
	SHELL_CMD(status, NULL, "Print system health status.",
		  cmd_health_status),
	SHELL_CMD(events, NULL, "Print all health events.",
		  cmd_health_events),
	SHELL_CMD(event, NULL, "Print one health event.",
		  cmd_health_event),
	SHELL_CMD(stats, NULL, "Print health event diagnostic statistics.",
		  cmd_health_stats),
	SHELL_CMD(enable, NULL, "Enable one health event.",
		  cmd_health_enable),
	SHELL_CMD(disable, NULL, "Disable one health event.",
		  cmd_health_disable),
	SHELL_CMD(protect, NULL, "Print system protect status.",
		  cmd_health_protect),
	SHELL_CMD(probes, NULL, "Print health probe status.",
		  cmd_health_probes),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(health, &system_health_cmds,
		   "System health service commands.", NULL);

K_THREAD_DEFINE(system_health_service_tid, SYSTEM_HEALTH_STACK_SIZE,
		health_thread, NULL, NULL, NULL,
		SYSTEM_HEALTH_PRIORITY, 0, 0);
