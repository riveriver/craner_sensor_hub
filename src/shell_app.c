#ifdef CONFIG_ENABLE_COREDUMP_SERVICE
#include "coredump_service.h"
#endif
#include "shell_app.h"
#ifdef CONFIG_ENABLE_DEVICE_PARAM_STORE
#include "device_param_store.h"
#endif
#ifdef CONFIG_MODBUS_REGISTER_STORE
#include "modbus_register_store.h"
#endif
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

#ifdef CONFIG_ENABLE_FAULT_INJECTION_SHELL
#include <zephyr/kernel.h>
#endif
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#define SHELL_OUTPUT_FORMAT_PARAM "shell/output_format"

static bool shell_output_json;

static int shell_output_format_refresh(void)
{
#ifdef CONFIG_ENABLE_DEVICE_PARAM_STORE
	char value[16];
	int rc;

	rc = device_param_store_get(SHELL_OUTPUT_FORMAT_PARAM, value,
				    sizeof(value));
	if (rc != 0) {
		shell_output_json = false;
		return rc;
	}

	shell_output_json = strcmp(value, "json") == 0;
	return 0;
#else
	shell_output_json = false;
	return 0;
#endif
}

static void shell_output_format_apply(const char *key, const char *value)
{
	if (key != NULL && value != NULL &&
	    strcmp(key, SHELL_OUTPUT_FORMAT_PARAM) == 0) {
		shell_output_json = strcmp(value, "json") == 0;
	}
}

int shell_app_init(void)
{
	return shell_output_format_refresh();
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

static bool shell_output_is_json(void)
{
	return shell_output_json;
}

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
	char json[512];
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (shell_output_is_json()) {
		rc = stack_usage_check_format_status(json, sizeof(json));
		if (rc != 0) {
			shell_error(shell, "format stack status failed: %d", rc);
			return rc;
		}
		shell_print(shell, "%s", json);
		return 0;
	}

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
	char json[1536];
	int len;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	storage_service_get_status(&status);

	if (!shell_output_is_json()) {
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

	len = snprintf(json, sizeof(json),
		       "{\"type\":\"storage\",\"initialized\":%s,"
		       "\"internal_flash_ready\":%s,"
		       "\"external_flash_ready\":%s,\"last_error\":%d,"
		       "\"partitions\":{"
		       "\"coredump\":{\"name\":\"%s\",\"available\":%s,"
		       "\"device_ready\":%s,\"device\":\"%s\","
		       "\"area_id\":%u,\"offset\":%u,"
		       "\"offset_hex\":\"0x%08x\",\"size\":%u,"
		       "\"last_error\":%d},"
		       "\"app_storage\":{\"name\":\"%s\",\"available\":%s,"
		       "\"device_ready\":%s,\"device\":\"%s\","
		       "\"area_id\":%u,\"offset\":%u,"
		       "\"offset_hex\":\"0x%08x\",\"size\":%u,"
		       "\"last_error\":%d},"
		       "\"param_store\":{\"name\":\"%s\",\"available\":%s,"
		       "\"device_ready\":%s,\"device\":\"%s\","
		       "\"area_id\":%u,\"offset\":%u,"
		       "\"offset_hex\":\"0x%08x\",\"size\":%u,"
		       "\"last_error\":%d},"
		       "\"modbus_store\":{\"name\":\"%s\",\"available\":%s,"
		       "\"device_ready\":%s,\"device\":\"%s\","
		       "\"area_id\":%u,\"offset\":%u,"
		       "\"offset_hex\":\"0x%08x\",\"size\":%u,"
		       "\"last_error\":%d}}}",
		       status.initialized ? "true" : "false",
		       status.internal_flash_ready ? "true" : "false",
		       status.external_flash_ready ? "true" : "false",
		       status.last_error,
		       storage_shell_str(status.coredump.name),
		       status.coredump.available ? "true" : "false",
		       status.coredump.device_ready ? "true" : "false",
		       storage_shell_str(status.coredump.device_name),
		       status.coredump.area_id, status.coredump.offset,
		       status.coredump.offset,
		       (uint32_t)status.coredump.size,
		       status.coredump.last_error,
		       storage_shell_str(status.app_storage.name),
		       status.app_storage.available ? "true" : "false",
		       status.app_storage.device_ready ? "true" : "false",
		       storage_shell_str(status.app_storage.device_name),
		       status.app_storage.area_id, status.app_storage.offset,
		       status.app_storage.offset,
		       (uint32_t)status.app_storage.size,
		       status.app_storage.last_error,
		       storage_shell_str(status.param_store.name),
		       status.param_store.available ? "true" : "false",
		       status.param_store.device_ready ? "true" : "false",
		       storage_shell_str(status.param_store.device_name),
		       status.param_store.area_id, status.param_store.offset,
		       status.param_store.offset,
		       (uint32_t)status.param_store.size,
		       status.param_store.last_error,
		       storage_shell_str(status.modbus_store.name),
		       status.modbus_store.available ? "true" : "false",
		       status.modbus_store.device_ready ? "true" : "false",
		       storage_shell_str(status.modbus_store.device_name),
		       status.modbus_store.area_id, status.modbus_store.offset,
		       status.modbus_store.offset,
		       (uint32_t)status.modbus_store.size,
		       status.modbus_store.last_error);
	if (len < 0) {
		shell_error(shell, "format storage status failed: %d", len);
		return len;
	}

	if ((size_t)len >= sizeof(json)) {
		shell_error(shell, "storage status JSON truncated");
		return -EMSGSIZE;
	}

	shell_print(shell, "%s", json);

	return 0;
}

SHELL_CMD_REGISTER(storage_status, NULL, "Show persistent storage status.",
		   cmd_storage_status);
#endif

#ifdef CONFIG_ENABLE_DEVICE_PARAM_STORE
static int print_param_pairs(const struct shell *shell, bool json_output)
{
	char json[768];
	size_t used = 0U;
	size_t count = device_param_store_count();

	if (!json_output) {
		for (size_t i = 0U; i < count; i++) {
			const struct device_param_record *record =
				device_param_store_get_by_index(i);

			if (record != NULL) {
				shell_print(shell, "%s=%s", record->key,
					    record->value);
			}
		}

		return 0;
	}

	used += snprintk(json + used, sizeof(json) - used, "{");
	for (size_t i = 0U; i < count; i++) {
		const struct device_param_record *record =
			device_param_store_get_by_index(i);
		int len;

		if (record == NULL) {
			continue;
		}

		len = snprintk(json + used, sizeof(json) - used,
			       "%s\"%s\":\"%s\"", used > 1U ? "," : "",
			       record->key, record->value);
		if (len < 0 || (size_t)len >= sizeof(json) - used) {
			return -EMSGSIZE;
		}

		used += (size_t)len;
	}

	if (used + 2U > sizeof(json)) {
		return -EMSGSIZE;
	}

	json[used++] = '}';
	json[used] = '\0';
	shell_print(shell, "%s", json);

	return 0;
}

static int cmd_param_status(const struct shell *shell, size_t argc,
			    char **argv)
{
	struct device_param_store_status status;
	char json[384];
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!shell_output_is_json()) {
		device_param_store_get_status(&status);
		shell_print(shell, "initialized=%s",
			    status.initialized ? "yes" : "no");
		shell_print(shell, "settings_ready=%s",
			    status.settings_ready ? "yes" : "no");
		shell_print(shell, "dirty=%s", status.dirty ? "yes" : "no");
		shell_print(shell, "param_count=%u",
			    (uint32_t)status.param_count);
		shell_print(shell, "load_count=%d", status.load_count);
		shell_print(shell, "save_count=%d", status.save_count);
		shell_print(shell, "fail_count=%d", status.fail_count);
		shell_print(shell, "last_error=%d", status.last_error);
		return 0;
	}

	rc = device_param_store_format_status(json, sizeof(json));
	if (rc != 0) {
		shell_error(shell, "format param status failed: %d", rc);
		return rc;
	}

	shell_print(shell, "%s", json);
	return 0;
}

SHELL_CMD_REGISTER(param_status, NULL, "Show device parameter store status.",
		   cmd_param_status);

static int cmd_param_get(const struct shell *shell, size_t argc, char **argv)
{
	char value[96];
	const char *key;
	int rc;
	bool json_output = shell_output_is_json();

	if (argc == 1U) {
		rc = print_param_pairs(shell, json_output);
		if (rc != 0) {
			shell_error(shell, "format parameters failed: %d", rc);
			return rc;
		}

		return 0;
	}

	if (argc != 2U) {
		shell_error(shell, "usage: param_get [key]");
		return -EINVAL;
	}

	key = argv[1];
	rc = device_param_store_get(key, value, sizeof(value));
	if (rc != 0) {
		shell_error(shell, "get parameter failed: %d", rc);
		return rc;
	}

	if (json_output) {
		shell_print(shell, "{\"key\":\"%s\",\"value\":\"%s\"}", key,
			    value);
	} else {
		shell_print(shell, "%s=%s", key, value);
	}

	return 0;
}

SHELL_CMD_REGISTER(param_get, NULL, "Get device parameter value.",
		   cmd_param_get);

static int cmd_param_set(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	if (argc != 3U) {
		shell_error(shell, "usage: param_set <key> <value>");
		return -EINVAL;
	}

	rc = device_param_store_set(argv[1], argv[2]);
	if (rc != 0) {
		shell_error(shell, "set parameter failed: %d", rc);
		return rc;
	}

	shell_output_format_apply(argv[1], argv[2]);

	if (shell_output_is_json()) {
		shell_print(shell,
			    "{\"type\":\"param_set\",\"key\":\"%s\",\"value\":\"%s\",\"dirty\":true}",
			    argv[1], argv[2]);
	} else {
		shell_print(shell, "status=ok");
		shell_print(shell, "%s=%s", argv[1], argv[2]);
		shell_print(shell, "dirty=yes");
	}

	return 0;
}

SHELL_CMD_REGISTER(param_set, NULL, "Set device parameter value.",
		   cmd_param_set);

static int cmd_param_save(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = device_param_store_save();
	if (rc != 0) {
		shell_error(shell, "save parameters failed: %d", rc);
		return rc;
	}

	if (shell_output_is_json()) {
		shell_print(shell, "{\"type\":\"param_save\",\"status\":\"ok\"}");
	} else {
		shell_print(shell, "status=ok");
	}

	return 0;
}

SHELL_CMD_REGISTER(param_save, NULL, "Save device parameters to settings/NVS.",
		   cmd_param_save);

static int cmd_param_factory_reset(const struct shell *shell, size_t argc,
				   char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = device_param_store_factory_reset();
	if (rc != 0) {
		shell_error(shell, "factory reset parameters failed: %d", rc);
		return rc;
	}

	rc = shell_output_format_refresh();
	if (rc != 0) {
		shell_error(shell, "refresh shell output format failed: %d", rc);
		return rc;
	}

	if (shell_output_is_json()) {
		shell_warn(shell,
			   "{\"type\":\"param_factory_reset\",\"status\":\"ok\"}");
	} else {
		shell_warn(shell, "status=ok");
	}

	return 0;
}

SHELL_CMD_REGISTER(param_factory_reset, NULL,
		   "Delete saved device parameters and restore defaults.",
		   cmd_param_factory_reset);
#endif

#ifdef CONFIG_MODBUS_REGISTER_STORE
static int cmd_modbus_store_status(const struct shell *shell, size_t argc,
				   char **argv)
{
	struct modbus_register_store_status status;
	char json[384];
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (shell_output_is_json()) {
		rc = modbus_register_store_format_status(json, sizeof(json));
		if (rc != 0) {
			shell_error(shell, "format modbus store status failed: %d",
				    rc);
			return rc;
		}

		shell_print(shell, "%s", json);
		return 0;
	}

	modbus_register_store_get_status(&status);
	shell_print(shell, "initialized=%s", status.initialized ? "yes" : "no");
	shell_print(shell, "dirty=%s", status.dirty ? "yes" : "no");
	shell_print(shell, "active_bank_valid=%s",
		    status.active_bank_valid ? "yes" : "no");
	shell_print(shell, "active_bank=%u", status.active_bank);
	shell_print(shell, "active_sequence=%u", status.active_sequence);
	shell_print(shell, "bank_size=%u", status.bank_size);
	shell_print(shell, "payload_size=%u", status.payload_size);
	shell_print(shell, "last_payload_size=%u", status.last_payload_size);
	shell_print(shell, "load_count=%u", status.load_count);
	shell_print(shell, "save_count=%u", status.save_count);
	shell_print(shell, "clear_count=%u", status.clear_count);
	shell_print(shell, "fail_count=%u", status.fail_count);
	shell_print(shell, "last_stage=%d", status.last_stage);
	shell_print(shell, "last_error=%d", status.last_error);

	return 0;
}

SHELL_CMD_REGISTER(modbus_store_status, NULL,
		   "Show Modbus persistent register store status.",
		   cmd_modbus_store_status);

static int cmd_modbus_store_save(const struct shell *shell, size_t argc,
				 char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = modbus_register_store_save_now();
	if (rc != 0) {
		shell_error(shell, "save modbus store failed: %d", rc);
		return rc;
	}

	shell_print(shell, shell_output_is_json() ?
			    "{\"type\":\"modbus_store_save\",\"status\":\"ok\"}" :
			    "status=ok");
	return 0;
}

SHELL_CMD_REGISTER(modbus_store_save, NULL,
		   "Immediately save persistent Modbus registers.",
		   cmd_modbus_store_save);

static int cmd_modbus_store_load(const struct shell *shell, size_t argc,
				 char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = modbus_register_store_load();
	if (rc != 0) {
		shell_error(shell, "load modbus store failed: %d", rc);
		return rc;
	}

	shell_print(shell, shell_output_is_json() ?
			    "{\"type\":\"modbus_store_load\",\"status\":\"ok\"}" :
			    "status=ok");
	return 0;
}

SHELL_CMD_REGISTER(modbus_store_load, NULL,
		   "Reload persistent Modbus registers from flash.",
		   cmd_modbus_store_load);

static int cmd_modbus_store_clear(const struct shell *shell, size_t argc,
				  char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = modbus_register_store_clear();
	if (rc != 0) {
		shell_error(shell, "clear modbus store failed: %d", rc);
		return rc;
	}

	shell_warn(shell, shell_output_is_json() ?
			   "{\"type\":\"modbus_store_clear\",\"status\":\"ok\"}" :
			   "status=ok");
	return 0;
}

SHELL_CMD_REGISTER(modbus_store_clear, NULL,
		   "Erase persistent Modbus register store.",
		   cmd_modbus_store_clear);
#endif

#ifdef CONFIG_ENABLE_COREDUMP_SERVICE
static int cmd_coredump_status(const struct shell *shell, size_t argc,
			       char **argv)
{
	struct coredump_service_status status;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = coredump_service_refresh();
	coredump_service_get_status(&status);

	shell_print(shell, "initialized: %s",
		    status.initialized ? "yes" : "no");
	shell_print(shell, "stored_dump_found: %s",
		    status.stored_dump_found ? "yes" : "no");
	shell_print(shell, "stored_dump_valid: %s",
		    status.stored_dump_valid ? "yes" : "no");
	shell_print(shell, "stored_dump_size: %u",
		    (uint32_t)status.stored_dump_size);
	shell_print(shell, "backend_error: %d", status.backend_error);
	shell_print(shell, "verify_result: %d", status.verify_result);
	shell_print(shell, "last_error: %d", status.last_error);

	return rc;
}

SHELL_CMD_REGISTER(coredump_status, NULL, "Show stored coredump status.",
		   cmd_coredump_status);

static int cmd_coredump_clear(const struct shell *shell, size_t argc,
			      char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = coredump_service_clear_stored_dump();
	if (rc != 0) {
		shell_error(shell, "clear stored coredump failed: %d", rc);
		return rc;
	}

	shell_warn(shell, "stored coredump erased");
	return 0;
}

SHELL_CMD_REGISTER(coredump_clear, NULL, "Erase stored coredump.",
		   cmd_coredump_clear);

static int cmd_coredump_report(const struct shell *shell, size_t argc,
			       char **argv)
{
	char report[384];
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = coredump_service_format_report(report, sizeof(report));
	if (rc != 0) {
		shell_error(shell, "format coredump report failed: %d", rc);
		return rc;
	}

	shell_print(shell, "%s", report);
	return 0;
}

SHELL_CMD_REGISTER(coredump_report, NULL,
		   "Print coredump status report.",
		   cmd_coredump_report);

static int cmd_coredump_export(const struct shell *shell, size_t argc,
			       char **argv)
{
	uint8_t chunk[CONFIG_COREDUMP_EXPORT_CHUNK_BYTES];
	char line[(CONFIG_COREDUMP_EXPORT_CHUNK_BYTES * 2U) + 5U];
	struct coredump_service_status status;
	size_t remaining;
	off_t offset = 0;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = coredump_service_refresh();
	coredump_service_get_status(&status);
	if (rc != 0) {
		shell_error(shell, "refresh coredump status failed: %d", rc);
		return rc;
	}

	if (!status.stored_dump_found) {
		shell_error(shell, "no stored coredump");
		return -ENOENT;
	}

	if (!status.stored_dump_valid) {
		shell_error(shell, "stored coredump is invalid");
		return -EBADMSG;
	}

	if (status.stored_dump_size == 0U) {
		shell_error(shell, "stored coredump is empty");
		return -ENODATA;
	}

	shell_print(shell, "#CD:BEGIN#");
	remaining = status.stored_dump_size;
	while (remaining > 0U) {
		size_t chunk_len = MIN(remaining, sizeof(chunk));

		rc = coredump_service_read_stored_dump(offset, chunk, chunk_len);
		if (rc != 0) {
			shell_error(shell, "read coredump failed at %lld: %d",
				    (long long)offset, rc);
			return rc;
		}

		rc = coredump_service_format_hex_line(chunk, chunk_len, line,
						      sizeof(line));
		if (rc != 0) {
			shell_error(shell, "format coredump chunk failed: %d",
				    rc);
			return rc;
		}

		shell_print(shell, "%s", line);
		offset += chunk_len;
		remaining -= chunk_len;
	}
	shell_print(shell, "#CD:END#");
	return 0;
}

SHELL_CMD_REGISTER(coredump_export, NULL,
		   "Export full stored coredump to shell.",
		   cmd_coredump_export);
#endif

#ifdef CONFIG_ENABLE_FAULT_INJECTION_SHELL
static int cmd_fault_oops(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_warn(shell, "triggering k_oops for coredump validation");
	k_oops();

	return 0;
}

SHELL_CMD_REGISTER(fault_oops, NULL, "Trigger k_oops for coredump testing.",
		   cmd_fault_oops);
#endif
