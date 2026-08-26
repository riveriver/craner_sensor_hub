#include "stack_usage_check.h"
#include "system_health_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(stack_usage_check, CONFIG_LOG_DEFAULT_LEVEL);

struct stack_scan_context {
	struct stack_usage_check_status status;
	bool log_warning;
};

struct stack_foreach_context {
	stack_usage_check_thread_cb_t cb;
	void *user_data;
	int first_error;
};

static K_MUTEX_DEFINE(stack_usage_check_lock);
static struct stack_usage_check_status check_status;

static uint8_t stack_usage_percent(size_t used, size_t size)
{
	if (size == 0U) {
		return 0U;
	}

	return (uint8_t)MIN((used * 100U) / size, 100U);
}

static bool stack_info_is_warning(const struct stack_usage_check_thread_info *info)
{
	if (info->error != 0 || info->stack_size == 0U) {
		return false;
	}

	return info->unused <
		       CONFIG_SYS_HEALTH_STACK_WARN_UNUSED_BYTES ||
	       info->usage_percent >=
		       CONFIG_SYS_HEALTH_STACK_WARN_USAGE_PERCENT;
}

static bool stack_info_is_worse(
	const struct stack_usage_check_thread_info *candidate,
	const struct stack_usage_check_thread_info *current)
{
	if (candidate->error != 0 || candidate->stack_size == 0U) {
		return false;
	}

	if (current->stack_size == 0U || current->error != 0) {
		return true;
	}

	if (candidate->usage_percent != current->usage_percent) {
		return candidate->usage_percent > current->usage_percent;
	}

	return candidate->unused < current->unused;
}

static int fill_stack_thread_info(const struct k_thread *thread,
				  struct stack_usage_check_thread_info *info)
{
	const char *name;
	size_t unused = 0U;
	int rc;

	if (thread == NULL || info == NULL) {
		return -EINVAL;
	}

	memset(info, 0, sizeof(*info));
	name = k_thread_name_get((k_tid_t)thread);
	if (name == NULL || name[0] == '\0') {
		name = "unknown";
	}
	(void)snprintk(info->name, sizeof(info->name), "%s", name);

	info->stack_size = thread->stack_info.size;
	rc = k_thread_stack_space_get(thread, &unused);
	if (rc != 0) {
		info->error = rc;
		return rc;
	}

	info->unused = unused;
	info->used = info->stack_size >= unused ? info->stack_size - unused : 0U;
	info->usage_percent = stack_usage_percent(info->used, info->stack_size);
	info->error = 0;

	return 0;
}

static void stack_scan_cb(const struct k_thread *thread, void *user_data)
{
	struct stack_scan_context *context = user_data;
	struct stack_usage_check_thread_info info;
	int rc;

	rc = fill_stack_thread_info(thread, &info);
	context->status.thread_count++;
	if (rc != 0) {
		context->status.last_error = rc;
		return;
	}

	if (stack_info_is_worse(&info, &context->status.worst_current)) {
		context->status.worst_current = info;
	}

	if (stack_info_is_warning(&info)) {
		context->status.warning = true;
		context->status.warn_count++;
		if (context->log_warning) {
			LOG_WRN_RATELIMIT("Thread stack high usage: thread=%s used=%u/%u (%u%%) unused=%u",
					  info.name, (uint32_t)info.used,
					  (uint32_t)info.stack_size,
					  info.usage_percent,
					  (uint32_t)info.unused);
		}
	}
}

void stack_usage_check_scan(uint32_t now_ms, bool log_warning)
{
	struct stack_scan_context context;

	memset(&context, 0, sizeof(context));
	context.log_warning = log_warning;
	context.status.initialized = true;
	context.status.last_scan_uptime_ms = now_ms;

	k_thread_foreach_unlocked(stack_scan_cb, &context);

	k_mutex_lock(&stack_usage_check_lock, K_FOREVER);
	context.status.scan_count = check_status.scan_count + 1U;
	context.status.warn_count += check_status.warn_count;
	if (stack_info_is_worse(&check_status.worst_ever,
				&context.status.worst_current)) {
		context.status.worst_ever = check_status.worst_ever;
	} else {
		context.status.worst_ever = context.status.worst_current;
	}
	check_status = context.status;
	k_mutex_unlock(&stack_usage_check_lock);
}

void stack_usage_check_get_status(struct stack_usage_check_status *status)
{
	if (status == NULL) {
		return;
	}

	k_mutex_lock(&stack_usage_check_lock, K_FOREVER);
	*status = check_status;
	k_mutex_unlock(&stack_usage_check_lock);
}

static void stack_foreach_cb(const struct k_thread *thread, void *user_data)
{
	struct stack_foreach_context *context = user_data;
	struct stack_usage_check_thread_info info;
	int rc;

	rc = fill_stack_thread_info(thread, &info);
	if (rc != 0 && context->first_error == 0) {
		context->first_error = rc;
	}

	if (context->cb != NULL) {
		context->cb(&info, context->user_data);
	}
}

int stack_usage_check_foreach(stack_usage_check_thread_cb_t cb,
			       void *user_data)
{
	struct stack_foreach_context context = {
		.cb = cb,
		.user_data = user_data,
	};

	if (cb == NULL) {
		return -EINVAL;
	}

	k_thread_foreach_unlocked(stack_foreach_cb, &context);

	return context.first_error;
}

int stack_usage_check_format_status(char *buf, size_t len)
{
	struct stack_usage_check_status status;
	int written;

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	stack_usage_check_get_status(&status);
	written = snprintk(buf, len,
			   "{\"type\":\"stack_status\",\"initialized\":%s,"
			   "\"warning\":%s,\"scan_count\":%u,"
			   "\"warn_count\":%u,\"last_scan_uptime_ms\":%u,"
			   "\"thread_count\":%u,\"worst_current\":{"
			   "\"name\":\"%s\",\"size\":%u,\"used\":%u,"
			   "\"unused\":%u,\"usage_pct\":%u},"
			   "\"worst_ever\":{\"name\":\"%s\",\"size\":%u,"
			   "\"used\":%u,\"unused\":%u,\"usage_pct\":%u},"
			   "\"last_error\":%d}",
			   status.initialized ? "true" : "false",
			   status.warning ? "true" : "false",
			   status.scan_count, status.warn_count,
			   status.last_scan_uptime_ms,
			   (uint32_t)status.thread_count,
			   status.worst_current.name,
			   (uint32_t)status.worst_current.stack_size,
			   (uint32_t)status.worst_current.used,
			   (uint32_t)status.worst_current.unused,
			   status.worst_current.usage_percent,
			   status.worst_ever.name,
			   (uint32_t)status.worst_ever.stack_size,
			   (uint32_t)status.worst_ever.used,
			   (uint32_t)status.worst_ever.unused,
			   status.worst_ever.usage_percent,
			   status.last_error);

	if (written < 0) {
		return written;
	}

	return (size_t)written >= len ? -EMSGSIZE : 0;
}

static void stack_usage_check_thread(void *p1, void *p2, void *p3)
{
	uint32_t last_scan_ms = 0U;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (!sys_health_is_initialized()) {
		k_sleep(K_MSEC(100));
	}

	last_scan_ms = k_uptime_get_32() -
		       CONFIG_SYS_HEALTH_STACK_USAGE_CHECK_INTERVAL_MS;

	while (1) {
		uint32_t now_ms = k_uptime_get_32();

		if ((uint32_t)(now_ms - last_scan_ms) >=
		    CONFIG_SYS_HEALTH_STACK_USAGE_CHECK_INTERVAL_MS) {
			stack_usage_check_scan(now_ms, true);
			last_scan_ms = now_ms;
		}

		k_sleep(K_MSEC(100));
	}
}

K_THREAD_DEFINE(system_health_stack_usage_check_tid,
		CONFIG_SYS_HEALTH_STACK_USAGE_CHECK_THREAD_STACK_SIZE,
		stack_usage_check_thread, NULL, NULL, NULL,
		CONFIG_SYS_HEALTH_STACK_USAGE_CHECK_THREAD_PRIORITY, 0, 0);
