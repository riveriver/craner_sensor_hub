#include "shell_app.h"
#include "network_manager_service.h"
#ifdef CONFIG_ENABLE_STORAGE_SERVICE
#include "storage_service.h"
#endif
#ifdef CONFIG_SYS_HEALTH_STACK_USAGE_CHECK
#include "stack_usage_check.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

int shell_app_init(void)
{
	return 0;
}

static int cmd_reboot(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_warn(shell, "rebooting");
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_CMD_REGISTER(reboot, NULL, "Reboot the MCU.", cmd_reboot);

static int cmd_fw_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Firmware build time: %s %s", __DATE__, __TIME__);

	return 0;
}

SHELL_CMD_REGISTER(fw_time, NULL, "Show firmware build date and time.", cmd_fw_time);

#ifdef CONFIG_SYS_HEALTH_STACK_USAGE_CHECK
static void print_stack_thread_cb(
	const struct stack_usage_check_thread_info *info, void *user_data)
{
	const struct shell *shell = user_data;

	if (info->error != 0) {
		shell_print(shell, "thread=%s error=%d", info->name,
			    info->error);
		return;
	}

	shell_print(shell,
		    "thread=%s size=%u used=%u unused=%u usage_pct=%u",
		    info->name, (uint32_t)info->stack_size,
		    (uint32_t)info->used, (uint32_t)info->unused,
		    info->usage_percent);
}

static int cmd_stack_status(const struct shell *shell, size_t argc,
			    char **argv)
{
	struct stack_usage_check_status status;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	stack_usage_check_get_status(&status);
	shell_print(shell, "initialized=%s",
		    status.initialized ? "yes" : "no");
	shell_print(shell, "warning=%s", status.warning ? "yes" : "no");
	shell_print(shell, "scan_count=%u", status.scan_count);
	shell_print(shell, "warn_count=%u", status.warn_count);
	shell_print(shell, "last_scan_uptime_ms=%u",
		    status.last_scan_uptime_ms);
	shell_print(shell, "thread_count=%u", (uint32_t)status.thread_count);
	shell_print(shell,
		    "worst_current=%s size=%u used=%u unused=%u usage_pct=%u",
		    status.worst_current.name,
		    (uint32_t)status.worst_current.stack_size,
		    (uint32_t)status.worst_current.used,
		    (uint32_t)status.worst_current.unused,
		    status.worst_current.usage_percent);
	shell_print(shell,
		    "worst_ever=%s size=%u used=%u unused=%u usage_pct=%u",
		    status.worst_ever.name,
		    (uint32_t)status.worst_ever.stack_size,
		    (uint32_t)status.worst_ever.used,
		    (uint32_t)status.worst_ever.unused,
		    status.worst_ever.usage_percent);
	shell_print(shell, "last_error=%d", status.last_error);
	shell_print(shell, "threads:");

	rc = stack_usage_check_foreach(print_stack_thread_cb, (void *)shell);
	if (rc != 0) {
		shell_error(shell, "stack thread scan failed: %d", rc);
		return rc;
	}

	return 0;
}

SHELL_CMD_REGISTER(stack_status, NULL, "Show thread stack watermarks.",
		   cmd_stack_status);
#endif

static int cmd_net_status(const struct shell *shell, size_t argc, char **argv)
{
	struct network_manager_service_status status;
	struct network_manager_service_stats stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	network_manager_service_get_status(&status);

	shell_print(shell, "state: %s",
		    network_manager_service_state_name(status.state));
	shell_print(shell, "carrier_up: %s",
		    status.carrier_up ? "yes" : "no");
	shell_print(shell, "ipv4_ready: %s",
		    status.ipv4_ready ? "yes" : "no");
	shell_print(shell, "dhcp_bound: %s",
		    status.dhcp_bound ? "yes" : "no");
	shell_print(shell, "rescue_ipv4_applied: %s",
		    status.rescue_ipv4_applied ? "yes" : "no");
	shell_print(shell, "ip: %s", status.ipv4_addr);
	shell_print(shell, "netmask: %s", status.netmask);
	shell_print(shell, "gateway: %s", status.gateway);
	shell_print(shell, "last_error: %d", status.last_error);
	if (network_manager_service_get_stats(&stats) == 0) {
		shell_print(shell, "carrier_on_count: %u",
			    stats.carrier_on_count);
		shell_print(shell, "carrier_off_count: %u",
			    stats.carrier_off_count);
		shell_print(shell, "ipv4_ready_count: %u",
			    stats.ipv4_ready_count);
		shell_print(shell, "ipv4_lost_count: %u",
			    stats.ipv4_lost_count);
		shell_print(shell, "dhcp_bound_count: %u",
			    stats.dhcp_bound_count);
		shell_print(shell, "rescue_apply_count: %u",
			    stats.rescue_apply_count);
		shell_print(shell, "rescue_remove_count: %u",
			    stats.rescue_remove_count);
		shell_print(shell, "error_count: %u", stats.error_count);
	}

	return 0;
}

SHELL_CMD_REGISTER(net_status, NULL, "Show managed network status.",
		   cmd_net_status);

#ifdef CONFIG_ENABLE_STORAGE_SERVICE
static const char *storage_shell_str(const char *value)
{
	return value != NULL ? value : "";
}

static int cmd_storage_status(const struct shell *shell, size_t argc,
			      char **argv)
{
	struct storage_service_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	storage_service_get_status(&status);

	shell_print(shell, "initialized=%s",
		    status.initialized ? "yes" : "no");
	shell_print(shell, "internal_flash_ready=%s",
		    status.internal_flash_ready ? "yes" : "no");
	shell_print(shell, "external_flash_ready=%s",
		    status.external_flash_ready ? "yes" : "no");
	shell_print(shell, "last_error=%d", status.last_error);

	shell_print(shell,
		    "coredump.available=%s coredump.device_ready=%s coredump.device=%s coredump.offset=0x%08x coredump.size=%u coredump.last_error=%d",
		    status.coredump.available ? "yes" : "no",
		    status.coredump.device_ready ? "yes" : "no",
		    storage_shell_str(status.coredump.device_name),
		    status.coredump.offset,
		    (uint32_t)status.coredump.size,
		    status.coredump.last_error);
	shell_print(shell,
		    "app_storage.available=%s app_storage.device_ready=%s app_storage.device=%s app_storage.offset=0x%08x app_storage.size=%u app_storage.last_error=%d",
		    status.app_storage.available ? "yes" : "no",
		    status.app_storage.device_ready ? "yes" : "no",
		    storage_shell_str(status.app_storage.device_name),
		    status.app_storage.offset,
		    (uint32_t)status.app_storage.size,
		    status.app_storage.last_error);
	shell_print(shell,
		    "param_store.available=%s param_store.device_ready=%s param_store.device=%s param_store.offset=0x%08x param_store.size=%u param_store.last_error=%d",
		    status.param_store.available ? "yes" : "no",
		    status.param_store.device_ready ? "yes" : "no",
		    storage_shell_str(status.param_store.device_name),
		    status.param_store.offset,
		    (uint32_t)status.param_store.size,
		    status.param_store.last_error);
	shell_print(shell,
		    "modbus_store.available=%s modbus_store.device_ready=%s modbus_store.device=%s modbus_store.offset=0x%08x modbus_store.size=%u modbus_store.last_error=%d",
		    status.modbus_store.available ? "yes" : "no",
		    status.modbus_store.device_ready ? "yes" : "no",
		    storage_shell_str(status.modbus_store.device_name),
		    status.modbus_store.offset,
		    (uint32_t)status.modbus_store.size,
		    status.modbus_store.last_error);

	return 0;
}

SHELL_CMD_REGISTER(storage_status, NULL, "Show persistent storage status.",
		   cmd_storage_status);
#endif

