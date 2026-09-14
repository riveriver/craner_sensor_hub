#include "sensor_manage_app.h"
#include <errno.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include "device_param_server.h"

#define BOOL_DEFAULT(x) (IS_ENABLED(x) ? "1" : "0")
static const char *const keys[] = { "sensor.slewing", "sensor.luffing", "sensor.hoisting", "sensor.anemometer", "sensor.load_adc" };
static const char *const names[] = { "slewing", "luffing", "hoisting", "anemometer", "load_adc" };
static const struct device_param_desc params[] = {
	{ .key = "sensor.slewing", .type = DEVICE_PARAM_TYPE_BOOL, .default_value = BOOL_DEFAULT(CONFIG_ENABLE_SLEWING_ENCODER), .flags = DEVICE_PARAM_F_PERSISTENT | DEVICE_PARAM_F_REBOOT_REQUIRED },
	{ .key = "sensor.luffing", .type = DEVICE_PARAM_TYPE_BOOL, .default_value = BOOL_DEFAULT(CONFIG_ENABLE_LUFFING_ENCODER), .flags = DEVICE_PARAM_F_PERSISTENT | DEVICE_PARAM_F_REBOOT_REQUIRED },
	{ .key = "sensor.hoisting", .type = DEVICE_PARAM_TYPE_BOOL, .default_value = BOOL_DEFAULT(CONFIG_ENABLE_HOISTING_ENCODER), .flags = DEVICE_PARAM_F_PERSISTENT | DEVICE_PARAM_F_REBOOT_REQUIRED },
	{ .key = "sensor.anemometer", .type = DEVICE_PARAM_TYPE_BOOL, .default_value = BOOL_DEFAULT(CONFIG_ENABLE_ANEMOMETER_SENSOR), .flags = DEVICE_PARAM_F_PERSISTENT | DEVICE_PARAM_F_REBOOT_REQUIRED },
	{ .key = "sensor.load_adc", .type = DEVICE_PARAM_TYPE_BOOL, .default_value = BOOL_DEFAULT(CONFIG_ENABLE_READ_LOAD_SENSOR), .flags = DEVICE_PARAM_F_PERSISTENT | DEVICE_PARAM_F_REBOOT_REQUIRED },
};

bool sensor_manage_is_enabled(enum sensor_manage_id id)
{
	char value[8];
	if (id < 0 || id >= ARRAY_SIZE(keys) || device_param_server_get(keys[id], value, sizeof(value)) != 0) return false;
	return !strcmp(value, "1") || !strcmp(value, "y") || !strcmp(value, "true");
}

static int sensor_index(const char *name)
{
	for (int i = 0; i < ARRAY_SIZE(names); i++) if (!strcmp(name, names[i])) return i;
	return -EINVAL;
}

static int cmd_sensor_show(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	for (int i = 0; i < ARRAY_SIZE(names); i++) shell_print(shell, "%s=%s", names[i], sensor_manage_is_enabled(i) ? "enabled" : "disabled");
	return 0;
}

static int cmd_sensor_set(const struct shell *shell, size_t argc, char **argv, bool enabled)
{
	int id, err;
	if (argc != 2) return -EINVAL;
	id = sensor_index(argv[1]);
	if (id < 0) return id;
	err = device_param_server_set(keys[id], enabled ? "1" : "0");
	if (err != 0) { shell_error(shell, "failed to change %s: %d", names[id], err); return err; }
	shell_print(shell, "%s %s; run 'sensor save' and reboot to apply", names[id], enabled ? "enabled" : "disabled");
	return 0;
}
static int cmd_sensor_enable(const struct shell *shell, size_t argc, char **argv) { return cmd_sensor_set(shell, argc, argv, true); }
static int cmd_sensor_disable(const struct shell *shell, size_t argc, char **argv) { return cmd_sensor_set(shell, argc, argv, false); }

static int cmd_sensor_save(const struct shell *shell, size_t argc, char **argv)
{
	int err; ARG_UNUSED(argc); ARG_UNUSED(argv);
	err = device_param_server_save();
	if (err != 0) { shell_error(shell, "save failed: %d", err); return err; }
	shell_print(shell, "status=ok; reboot required to apply"); return 0;
}
static int cmd_sensor_reboot(const struct shell *shell, size_t argc, char **argv)
{
	int err; ARG_UNUSED(argc); ARG_UNUSED(argv);
	err = device_param_server_save();
	if (err != 0) { shell_error(shell, "save failed: %d", err); return err; }
	shell_print(shell, "status=ok; rebooting"); sys_reboot(SYS_REBOOT_COLD); return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sensor_cmds,
	SHELL_CMD(show, NULL, "Show sensor enable status.", cmd_sensor_show),
	SHELL_CMD(enable, NULL, "Enable a sensor.", cmd_sensor_enable),
	SHELL_CMD(disable, NULL, "Disable a sensor.", cmd_sensor_disable),
	SHELL_CMD(save, NULL, "Persist sensor settings.", cmd_sensor_save),
	SHELL_CMD(reboot, NULL, "Persist settings and reboot.", cmd_sensor_reboot),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(sensor, &sensor_cmds, "Manage persistent sensor settings.", NULL);

static int sensor_manage_init(void) { return device_param_server_register_table(params, ARRAY_SIZE(params)); }
SYS_INIT(sensor_manage_init, POST_KERNEL, 80);
