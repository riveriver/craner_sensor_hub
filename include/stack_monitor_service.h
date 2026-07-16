#ifndef STACK_MONITOR_SERVICE_H
#define STACK_MONITOR_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STACK_MONITOR_THREAD_NAME_LEN 32U

struct stack_monitor_thread_info {
	char name[STACK_MONITOR_THREAD_NAME_LEN];
	size_t stack_size;
	size_t unused;
	size_t used;
	uint8_t usage_percent;
	int error;
};

struct stack_monitor_status {
	bool initialized;
	bool warning;
	uint32_t scan_count;
	uint32_t warn_count;
	uint32_t last_scan_uptime_ms;
	size_t thread_count;
	struct stack_monitor_thread_info worst_current;
	struct stack_monitor_thread_info worst_ever;
	int last_error;
};

typedef void (*stack_monitor_thread_cb_t)(
	const struct stack_monitor_thread_info *info, void *user_data);

void stack_monitor_service_scan(uint32_t now_ms, bool log_warning);
void stack_monitor_service_get_status(struct stack_monitor_status *status);
int stack_monitor_service_foreach(stack_monitor_thread_cb_t cb,
				  void *user_data);
int stack_monitor_service_format_status(char *buf, size_t len);

#endif /* STACK_MONITOR_SERVICE_H */
