#include "network_service.h"
#include "rtc_time_provider.h"
#ifdef CONFIG_CRANER_ENABLE_STORAGE_SERVICE
#include "storage_service.h"
#endif
#include "time_service.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/timeutil.h>

static int parse_iso8601_utc(const char *text, int64_t *unix_time_s)
{
	struct tm tm_time;
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;
	char suffix;
	int matched;

	if (text == NULL || unix_time_s == NULL) {
		return -EINVAL;
	}

	matched = sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%c",
			 &year, &month, &day, &hour, &minute, &second,
			 &suffix);
	if (matched != 7 || suffix != 'Z') {
		return -EINVAL;
	}

	if (month < 1 || month > 12 || day < 1 || day > 31 ||
	    hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
	    second < 0 || second > 59) {
		return -EINVAL;
	}

	memset(&tm_time, 0, sizeof(tm_time));
	tm_time.tm_year = year - 1900;
	tm_time.tm_mon = month - 1;
	tm_time.tm_mday = day;
	tm_time.tm_hour = hour;
	tm_time.tm_min = minute;
	tm_time.tm_sec = second;
	tm_time.tm_isdst = -1;

	*unix_time_s = timeutil_timegm64(&tm_time);
	return 0;
}

static int cmd_fw_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Firmware build time: %s %s", __DATE__, __TIME__);

	return 0;
}

SHELL_CMD_REGISTER(fw_time, NULL, "Show firmware build date and time.", cmd_fw_time);

static int cmd_net_status(const struct shell *shell, size_t argc, char **argv)
{
	struct network_service_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	network_service_get_status(&status);

	shell_print(shell, "state: %s", network_service_state_name(status.state));
	shell_print(shell, "link_up: %s", status.link_up ? "yes" : "no");
	shell_print(shell, "ready: %s", status.ready ? "yes" : "no");
	shell_print(shell, "ip: %s", status.ip);
	shell_print(shell, "netmask: %s", status.netmask);
	shell_print(shell, "gateway: %s", status.gateway);
	shell_print(shell, "dhcp_server: %s", status.dhcp_server);
	shell_print(shell, "lease_s: %u", status.dhcp_lease_time_s);
	shell_print(shell, "renew_s: %u", status.dhcp_renewal_time_s);
	shell_print(shell, "fail_count: %u", status.dhcp_fail_count);
	shell_print(shell, "next_retry_ms: %u", status.dhcp_retry_delay_ms);

	return 0;
}

SHELL_CMD_REGISTER(net_status, NULL, "Show managed network status.",
		   cmd_net_status);

#ifdef CONFIG_CRANER_ENABLE_STORAGE_SERVICE
static const char *storage_shell_str(const char *value)
{
	return value != NULL ? value : "";
}

static void print_storage_partition(const struct shell *shell,
				    const char *prefix,
				    const struct storage_partition_status *status)
{
	shell_print(shell, "%s_name: %s", prefix,
		    storage_shell_str(status->name));
	shell_print(shell, "%s_available: %s", prefix,
		    status->available ? "yes" : "no");
	shell_print(shell, "%s_device_ready: %s", prefix,
		    status->device_ready ? "yes" : "no");
	shell_print(shell, "%s_device: %s", prefix,
		    storage_shell_str(status->device_name));
	shell_print(shell, "%s_area_id: %u", prefix, status->area_id);
	shell_print(shell, "%s_offset: 0x%08x", prefix, status->offset);
	shell_print(shell, "%s_size: %u", prefix, (uint32_t)status->size);
	shell_print(shell, "%s_last_error: %d", prefix, status->last_error);
}

static int cmd_storage_status(const struct shell *shell, size_t argc,
			      char **argv)
{
	struct storage_service_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	storage_service_get_status(&status);

	shell_print(shell, "initialized: %s",
		    status.initialized ? "yes" : "no");
	shell_print(shell, "internal_flash_ready: %s",
		    status.internal_flash_ready ? "yes" : "no");
	shell_print(shell, "last_error: %d", status.last_error);
	print_storage_partition(shell, "coredump", &status.coredump);
	print_storage_partition(shell, "app_storage", &status.app_storage);

	return 0;
}

SHELL_CMD_REGISTER(storage_status, NULL, "Show persistent storage status.",
		   cmd_storage_status);
#endif

static int cmd_time_status(const struct shell *shell, size_t argc, char **argv)
{
	struct time_service_status status;
	char iso_time[32];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	time_service_get_status(&status);

	shell_print(shell, "valid: %s", status.wall_time_valid ? "yes" : "no");
	shell_print(shell, "source: %s",
		    time_service_source_name(status.active_source));
	shell_print(shell, "quality: %s",
		    time_service_quality_name(status.quality));
	shell_print(shell, "mode: %s",
		    time_service_correction_mode_name(status.correction_mode));
	shell_print(shell, "unix_time_s: %lld", (long long)status.unix_time_s);
	shell_print(shell, "uptime_ms: %lld", (long long)status.uptime_ms);
	shell_print(shell, "last_sync_uptime_ms: %lld",
		    (long long)status.last_sync_uptime_ms);
	shell_print(shell, "last_rtc_writeback_unix_time_s: %lld",
		    (long long)status.last_rtc_writeback_unix_time_s);
	shell_print(shell, "last_rtc_writeback_uptime_ms: %lld",
		    (long long)status.last_rtc_writeback_uptime_ms);
	shell_print(shell, "sync_count: %u", status.sync_count);
	shell_print(shell, "fail_count: %u", status.fail_count);
	shell_print(shell, "retry_delay_ms: %u", status.retry_delay_ms);
	shell_print(shell, "rtc_available: %s",
		    status.rtc_available ? "yes" : "no");
	shell_print(shell, "rtc_valid: %s", status.rtc_valid ? "yes" : "no");
	shell_print(shell, "rtc_last_error: %d", status.rtc_last_error);
	shell_print(shell, "ntp_server: %s", status.ntp_server);

	if (time_service_format_iso8601(iso_time, sizeof(iso_time)) == 0) {
		shell_print(shell, "iso8601_utc: %s", iso_time);
	}

	return 0;
}

SHELL_CMD_REGISTER(time_status, NULL, "Show managed time status.",
		   cmd_time_status);

static int cmd_time_sync(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = time_service_sync_now();
	if (rc != 0) {
		shell_error(shell, "time sync request failed: %d", rc);
		return rc;
	}

	shell_print(shell, "time sync requested");
	return 0;
}

SHELL_CMD_REGISTER(time_sync, NULL, "Request immediate NTP sync.",
		   cmd_time_sync);

static int cmd_time_mode(const struct shell *shell, size_t argc, char **argv)
{
	enum time_service_correction_mode mode;
	int rc;

	if (argc == 1) {
		mode = time_service_correction_mode_get();
		shell_print(shell, "time mode: %s",
			    time_service_correction_mode_name(mode));
		return 0;
	}

	if (argc != 2) {
		shell_error(shell, "usage: time_mode <auto|manual>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "auto") == 0) {
		mode = TIME_SERVICE_CORRECTION_MODE_AUTO;
	} else if (strcmp(argv[1], "manual") == 0) {
		mode = TIME_SERVICE_CORRECTION_MODE_MANUAL;
	} else {
		shell_error(shell, "usage: time_mode <auto|manual>");
		return -EINVAL;
	}

	rc = time_service_correction_mode_set(mode);
	if (rc != 0) {
		shell_error(shell, "set time mode failed: %d", rc);
		return rc;
	}

	shell_print(shell, "time mode: %s",
		    time_service_correction_mode_name(mode));
	return 0;
}

SHELL_CMD_REGISTER(time_mode, NULL, "Get or set time correction mode.",
		   cmd_time_mode);

static int cmd_time_set(const struct shell *shell, size_t argc, char **argv)
{
	int64_t unix_time_s;
	int rc;

	if (argc != 2) {
		shell_error(shell, "usage: time_set YYYY-MM-DDTHH:MM:SSZ");
		return -EINVAL;
	}

	rc = parse_iso8601_utc(argv[1], &unix_time_s);
	if (rc != 0) {
		shell_error(shell, "invalid UTC time: %s", argv[1]);
		return rc;
	}

	rc = time_service_set_unix_time(unix_time_s);
	if (rc != 0) {
		shell_error(shell, "set time failed: %d", rc);
		return rc;
	}

	shell_print(shell, "time set: %s", argv[1]);
	return 0;
}

SHELL_CMD_REGISTER(time_set, NULL, "Set system time and RTC from UTC ISO8601.",
		   cmd_time_set);

static void print_rtc_status(const struct shell *shell)
{
	struct rtc_time_provider_status status;
	time_t now;
	struct tm tm_now;
	char iso_time[32] = "invalid";

	rtc_time_provider_get_status(&status);

	if (status.valid) {
		now = (time_t)status.unix_time_s;
		if (gmtime_r(&now, &tm_now) != NULL &&
		    strftime(iso_time, sizeof(iso_time),
			     "%Y-%m-%dT%H:%M:%SZ", &tm_now) == 0U) {
			strcpy(iso_time, "invalid");
		}
	}

	shell_print(shell, "available: %s", status.available ? "yes" : "no");
	shell_print(shell, "ready: %s", status.ready ? "yes" : "no");
	shell_print(shell, "valid: %s", status.valid ? "yes" : "no");
	shell_print(shell, "time_range_valid: %s",
		    status.time_range_valid ? "yes" : "no");
	shell_print(shell, "trust_valid: %s",
		    status.trust_valid ? "yes" : "no");
	shell_print(shell, "trust_source: %s",
		    rtc_trust_source_name(status.trust_source));
	shell_print(shell, "last_error: %d", status.last_error);
	shell_print(shell, "trust_error: %d", status.trust_error);
	shell_print(shell, "unix_time_s: %lld",
		    (long long)status.unix_time_s);
	shell_print(shell, "iso8601_utc: %s", iso_time);
	shell_print(shell, "last_set_unix_time_s: %lld",
		    (long long)status.last_set_unix_time_s);
	shell_print(shell, "last_set_uptime_ms: %lld",
		    (long long)status.last_set_uptime_ms);
}

static int cmd_rtc_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	print_rtc_status(shell);
	return 0;
}

SHELL_CMD_REGISTER(rtc_status, NULL, "Show RTC provider status.",
		   cmd_rtc_status);

static int cmd_rtc_get(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	print_rtc_status(shell);
	return 0;
}

SHELL_CMD_REGISTER(rtc_get, NULL, "Read RTC time.", cmd_rtc_get);

static int cmd_rtc_set(const struct shell *shell, size_t argc, char **argv)
{
	int64_t unix_time_s;
	int rc;

	if (argc != 2) {
		shell_error(shell, "usage: rtc_set YYYY-MM-DDTHH:MM:SSZ");
		return -EINVAL;
	}

	rc = parse_iso8601_utc(argv[1], &unix_time_s);
	if (rc != 0) {
		shell_error(shell, "invalid UTC time: %s", argv[1]);
		return rc;
	}

	rc = rtc_time_provider_set_unix_time(unix_time_s);
	if (rc != 0) {
		shell_error(shell, "set RTC failed: %d", rc);
		return rc;
	}

	rc = time_service_set_unix_time(unix_time_s);
	if (rc != 0) {
		shell_error(shell, "set system time failed: %d", rc);
		return rc;
	}

	shell_print(shell, "RTC set: %s", argv[1]);
	return 0;
}

SHELL_CMD_REGISTER(rtc_set, NULL, "Set RTC and system time from UTC ISO8601.",
		   cmd_rtc_set);

static int cmd_rtc_trust_clear(const struct shell *shell, size_t argc,
			       char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = rtc_time_provider_clear_trust();
	if (rc != 0) {
		shell_error(shell, "clear RTC trust failed: %d", rc);
		return rc;
	}

	shell_warn(shell, "RTC trust record cleared");
	return 0;
}

SHELL_CMD_REGISTER(rtc_trust_clear, NULL, "Clear RTC trust record.",
		   cmd_rtc_trust_clear);
