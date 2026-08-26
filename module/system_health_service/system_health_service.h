#ifndef SYS_HEALTH_H
#define SYS_HEALTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util_macro.h>

#define SYS_HEALTH_EVENT_SYSTEM_PROTECT 1U

#define SYS_HEALTH_ACTION_LOG BIT(0)
#define SYS_HEALTH_ACTION_CALLBACK BIT(1)
#define SYS_HEALTH_ACTION_SET_DEGRADED BIT(2)
#define SYS_HEALTH_ACTION_REQUEST_REBOOT BIT(3)
#define SYS_HEALTH_ACTION_STOP_WATCHDOG_FEED BIT(4)

#define SYS_HEALTH_DIAG_TEST_FAILED BIT(0)
#define SYS_HEALTH_DIAG_FAILED_THIS_CYCLE BIT(1)
#define SYS_HEALTH_DIAG_PENDING BIT(2)
#define SYS_HEALTH_DIAG_CONFIRMED BIT(3)

struct sys_health_event_status;

typedef void (*sys_health_event_callback_t)(
	const struct sys_health_event_status *status);

struct sys_health_action_context {
	uint16_t source_event;
	const char *source_name;
	uint32_t action_mask;
};

typedef void (*sys_health_action_callback_t)(
	const struct sys_health_action_context *context,
	void *user_data);
typedef void (*sys_health_action_handler_t)(
	const struct sys_health_action_context *context,
	void *user_data);
typedef void (*sys_health_periodic_handler_t)(uint32_t now_ms,
	void *user_data);

struct sys_health_event {
	uint16_t event;
	const char *name;
	bool enable;
	uint8_t priority;
	uint32_t offline_timeout_ms;
	uint32_t action_mask;
	uint32_t action_delay_ms;
	sys_health_event_callback_t offline_first_func;
	sys_health_event_callback_t offline_func;
	sys_health_event_callback_t online_first_func;
	sys_health_event_callback_t online_func;
};

struct sys_health_event_status {
	uint16_t event;
	const char *name;
	bool configured;
	bool enable;
	bool offline;
	bool system_protect_triggered;
	bool action_triggered;
	uint8_t priority;
	uint32_t offline_timeout_ms;
	uint32_t offline_since_ms;
	uint32_t last_update_ms;
	uint32_t action_mask;
	uint32_t action_delay_ms;
	uint32_t offline_count;
	uint32_t recover_count;
	uint32_t current_offline_duration_ms;
	uint32_t last_offline_duration_ms;
	uint32_t max_offline_duration_ms;
	uint32_t diag_flags;
};

struct sys_health_protect_status {
	bool active;
	bool degraded;
	bool reboot_requested;
	bool watchdog_feed_stop_requested;
	uint16_t source_event;
};

struct sys_health_policy {
	uint16_t event;
	uint32_t action_mask;
	uint32_t action_delay_ms;
};

struct sys_health_time_provider {
	int (*get_unix_time_s)(int64_t *unix_time_s, void *user_data);
	void *user_data;
};

struct sys_health_probe_status {
	const char *name;
	bool enabled;
	bool initialized;
	bool target_valid;
	const char *target;
	uint16_t event;
	uint32_t period_ms;
	uint32_t timeout_ms;
	uint32_t max_consecutive_failures;
	uint32_t consecutive_failures;
	uint32_t last_sequence;
	int last_error;
	int64_t last_check_uptime_ms;
};

typedef void (*sys_health_event_status_cb_t)(
	const struct sys_health_event_status *status,
	void *user_data);
typedef void (*sys_health_probe_status_cb_t)(
	const struct sys_health_probe_status *status,
	void *user_data);

int sys_health_init(
	const struct sys_health_event *event_table,
	int event_count,
	const struct sys_health_time_provider *time_provider);
bool sys_health_is_initialized(void);
int sys_health_event_register(
	const struct sys_health_event *event_obj);
int sys_health_policy_register(const struct sys_health_policy *policy);
int sys_health_action_callback_register(
	sys_health_action_callback_t callback, void *user_data);
int sys_health_action_handler_register(uint32_t action_mask,
	sys_health_action_handler_t handler, void *user_data);
int sys_health_periodic_handler_register(
	sys_health_periodic_handler_t handler, void *user_data);
void sys_health_action_request(uint32_t action_mask, uint16_t source_event,
	const char *source_name);
int sys_health_time_provider_register(
	const struct sys_health_time_provider *time_provider);
int sys_health_time_get_unix_s(int64_t *unix_time_s);
void sys_health_event_report(uint16_t event);
void sys_health_event_enable(uint16_t event);
void sys_health_event_disable(uint16_t event);
void sys_health_event_set(uint16_t event, bool offline);
bool sys_health_event_is_offline(uint16_t event);
int sys_health_event_get(uint16_t event,
	struct sys_health_event_status *status);
void sys_health_protect_set(uint16_t source_event);
bool sys_health_protect_is_active(void);
bool sys_health_watchdog_feed_stop_is_requested(void);
void sys_health_protect_get_status(
	struct sys_health_protect_status *status);
int sys_health_event_foreach(
	sys_health_event_status_cb_t cb, void *user_data);
int sys_health_probe_foreach(
	sys_health_probe_status_cb_t cb, void *user_data);

#endif /* SYS_HEALTH_H */
