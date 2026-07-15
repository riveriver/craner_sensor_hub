#ifdef CONFIG_CRANER_ENABLE_COREDUMP_SERVICE
#include "coredump_service.h"
#endif
#ifdef CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE
#include "device_param_store.h"
#endif
#include "mqtt_service_manager.h"
#include "network_service.h"
#include "rtc_time_provider.h"
#include "time_service.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(mqtt_shell_service, CONFIG_LOG_DEFAULT_LEVEL);

#define MQTT_SHELL_REQUEST_TOPIC CONFIG_CRANER_MQTT_SHELL_SERVICE_REQUEST_TOPIC
#define MQTT_SHELL_RESPONSE_TOPIC CONFIG_CRANER_MQTT_SHELL_SERVICE_RESPONSE_TOPIC

static bool topic_matches(const struct mqtt_service_manager_publish *publish,
			  const char *topic)
{
	return publish->topic_len == strlen(topic) &&
	       memcmp(publish->topic, topic, publish->topic_len) == 0;
}

static bool payload_equals(const struct mqtt_service_manager_publish *publish,
			   const char *command)
{
	return publish->payload_len == strlen(command) &&
	       memcmp(publish->payload, command, publish->payload_len) == 0;
}

static int payload_to_string(const struct mqtt_service_manager_publish *publish,
			     char *buf, size_t len)
{
	if (publish == NULL || buf == NULL || len == 0U) {
		return -EINVAL;
	}

	if (publish->payload_len >= len) {
		return -EMSGSIZE;
	}

	memcpy(buf, publish->payload, publish->payload_len);
	buf[publish->payload_len] = '\0';

	return 0;
}

static void publish_response(const char *status, const char *command,
			     const char *message)
{
	char response[CONFIG_CRANER_MQTT_SHELL_SERVICE_RESPONSE_SIZE];
	int len;
	int rc;

	len = snprintk(response, sizeof(response),
		      "{\"status\":\"%s\",\"cmd\":\"%s\",\"message\":\"%s\"}",
		      status, command, message);
	if (len < 0) {
		return;
	}

	rc = mqtt_service_manager_publish(MQTT_SHELL_RESPONSE_TOPIC, response,
					  MIN((size_t)len, sizeof(response) - 1U),
					  MQTT_QOS_0_AT_MOST_ONCE, false);
	if (rc != 0) {
		LOG_WRN("Failed to publish MQTT shell response: %d", rc);
	}
}

static void handle_command(const struct mqtt_service_manager_publish *publish)
{
	char command[256];
	char msg[384];
	int parse_rc;

	parse_rc = payload_to_string(publish, command, sizeof(command));
	if (parse_rc != 0) {
		publish_response("error", "unknown", "payload too large");
		return;
	}

	if (payload_equals(publish, "fw_time")) {
		snprintk(msg, sizeof(msg), "%s %s", __DATE__, __TIME__);
		publish_response("ok", "fw_time", msg);
		return;
	}

	if (payload_equals(publish, "mqtt_status")) {
		publish_response("ok", "mqtt_status",
				 mqtt_service_manager_is_connected() ?
					 "connected" : "disconnected");
		return;
	}

	if (payload_equals(publish, "net_status")) {
		struct network_service_status status;

		network_service_get_status(&status);
		snprintk(msg, sizeof(msg),
			 "state=%s,link_up=%s,ready=%s,ip=%s,gateway=%s,dhcp_server=%s,lease_s=%u,fail_count=%u,next_retry_ms=%u",
			 network_service_state_name(status.state),
			 status.link_up ? "yes" : "no",
			 status.ready ? "yes" : "no",
			 status.ip, status.gateway, status.dhcp_server,
			 status.dhcp_lease_time_s, status.dhcp_fail_count,
			 status.dhcp_retry_delay_ms);
		publish_response("ok", "net_status", msg);
		return;
	}

	if (payload_equals(publish, "time_status")) {
		struct time_service_status status;
		char iso_time[32] = "invalid";

		time_service_get_status(&status);
		(void)time_service_format_iso8601(iso_time, sizeof(iso_time));
		snprintk(msg, sizeof(msg),
			 "valid=%s,source=%s,quality=%s,mode=%s,unix=%lld,iso=%s,sync_count=%u,fail_count=%u,rtc_available=%s,rtc_valid=%s,rtc_error=%d,ntp=%s",
			 status.wall_time_valid ? "yes" : "no",
			 time_service_source_name(status.active_source),
			 time_service_quality_name(status.quality),
			 time_service_correction_mode_name(status.correction_mode),
			 (long long)status.unix_time_s, iso_time,
			 status.sync_count, status.fail_count,
			 status.rtc_available ? "yes" : "no",
			 status.rtc_valid ? "yes" : "no",
			 status.rtc_last_error,
			 status.ntp_server);
		publish_response("ok", "time_status", msg);
		return;
	}

	if (payload_equals(publish, "rtc_status")) {
		struct rtc_time_provider_status status;

		rtc_time_provider_get_status(&status);
		snprintk(msg, sizeof(msg),
			 "available=%s,ready=%s,valid=%s,time_range_valid=%s,trust_valid=%s,trust_source=%s,last_error=%d,trust_error=%d,unix=%lld,last_set_unix=%lld,last_set_uptime_ms=%lld",
			 status.available ? "yes" : "no",
			 status.ready ? "yes" : "no",
			 status.valid ? "yes" : "no",
			 status.time_range_valid ? "yes" : "no",
			 status.trust_valid ? "yes" : "no",
			 rtc_trust_source_name(status.trust_source),
			 status.last_error,
			 status.trust_error,
			 (long long)status.unix_time_s,
			 (long long)status.last_set_unix_time_s,
			 (long long)status.last_set_uptime_ms);
		publish_response("ok", "rtc_status", msg);
		return;
	}

#ifdef CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE
	if (payload_equals(publish, "param_status")) {
		int rc;

		rc = device_param_store_format_status(msg, sizeof(msg));
		publish_response(rc == 0 ? "ok" : "error", "param_status",
				 rc == 0 ? msg : "format failed");
		return;
	}

	if (strncmp(command, "param_get", strlen("param_get")) == 0) {
		char *saveptr;
		char *cmd;
		char *key;
		char value[96];
		const struct device_param_record *record;
		int rc;

		cmd = strtok_r(command, " ", &saveptr);
		key = strtok_r(NULL, " ", &saveptr);
		if (cmd == NULL || key == NULL || strtok_r(NULL, " ", &saveptr) != NULL) {
			publish_response("error", "param_get",
					 "usage: param_get <key>");
			return;
		}

		rc = device_param_store_get(key, value, sizeof(value));
		record = device_param_store_find(key);
		if (rc != 0 || record == NULL) {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "param_get", msg);
			return;
		}

		snprintk(msg, sizeof(msg),
			 "key=%s,value=%s,dirty=%s,loaded=%s,last_error=%d",
			 record->key, value, record->dirty ? "true" : "false",
			 record->loaded_from_settings ? "true" : "false",
			 record->last_error);
		publish_response("ok", "param_get", msg);
		return;
	}

	if (strncmp(command, "param_set", strlen("param_set")) == 0) {
		char *saveptr;
		char *cmd;
		char *key;
		char *value;
		int rc;

		cmd = strtok_r(command, " ", &saveptr);
		key = strtok_r(NULL, " ", &saveptr);
		value = strtok_r(NULL, " ", &saveptr);
		if (cmd == NULL || key == NULL || value == NULL ||
		    strtok_r(NULL, " ", &saveptr) != NULL) {
			publish_response("error", "param_set",
					 "usage: param_set <key> <value>");
			return;
		}

		rc = device_param_store_set(key, value);
		if (rc != 0) {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "param_set", msg);
			return;
		}

		snprintk(msg, sizeof(msg), "key=%s,value=%s,dirty=true",
			 key, value);
		publish_response("ok", "param_set", msg);
		return;
	}

	if (payload_equals(publish, "param_save")) {
		int rc = device_param_store_save();

		if (rc == 0) {
			publish_response("ok", "param_save", "saved");
		} else {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "param_save", msg);
		}
		return;
	}

	if (payload_equals(publish, "param_factory_reset")) {
		int rc = device_param_store_factory_reset();

		if (rc == 0) {
			publish_response("ok", "param_factory_reset",
					 "factory defaults restored");
		} else {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "param_factory_reset", msg);
		}
		return;
	}
#endif

#ifdef CONFIG_CRANER_ENABLE_COREDUMP_SERVICE
	if (payload_equals(publish, "coredump_status")) {
		struct coredump_service_status status;
		int rc;

		rc = coredump_service_refresh();
		coredump_service_get_status(&status);
		snprintk(msg, sizeof(msg),
			 "initialized=%s,found=%s,valid=%s,size=%u,backend_error=%d,verify_result=%d,last_error=%d",
			 status.initialized ? "yes" : "no",
			 status.stored_dump_found ? "yes" : "no",
			 status.stored_dump_valid ? "yes" : "no",
			 (uint32_t)status.stored_dump_size,
			 status.backend_error, status.verify_result,
			 status.last_error);
		publish_response(rc == 0 ? "ok" : "error",
				 "coredump_status", msg);
		return;
	}

	if (payload_equals(publish, "coredump_report")) {
		int rc;

		rc = coredump_service_publish_report();
		if (rc == 0) {
			publish_response("ok", "coredump_report", "published");
		} else {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "coredump_report", msg);
		}
		return;
	}

	if (payload_equals(publish, "coredump_export")) {
		int rc;

		rc = coredump_service_publish_export();
		if (rc == 0) {
			publish_response("ok", "coredump_export", "published");
		} else {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "coredump_export", msg);
		}
		return;
	}

	if (payload_equals(publish, "coredump_clear")) {
		int rc;

		rc = coredump_service_clear_stored_dump();
		if (rc == 0) {
			publish_response("ok", "coredump_clear", "erased");
		} else {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "coredump_clear", msg);
		}
		return;
	}
#endif

	if (payload_equals(publish, "time_sync")) {
		int rc = time_service_sync_now();

		if (rc == 0) {
			publish_response("ok", "time_sync", "requested");
		} else {
			snprintk(msg, sizeof(msg), "failed: %d", rc);
			publish_response("error", "time_sync", msg);
		}
		return;
	}

	if (payload_equals(publish, "reboot")) {
		publish_response("ok", "reboot", "rebooting");
		k_sleep(K_MSEC(250));
		sys_reboot(SYS_REBOOT_COLD);
		return;
	}

	publish_response("error", "unknown", "unsupported command");
}

static void mqtt_shell_service_event_handler(
	enum mqtt_service_manager_event event, struct mqtt_client *client,
	const struct mqtt_service_manager_publish *publish, void *user_data)
{
	int rc;

	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	switch (event) {
	case MQTT_SERVICE_MANAGER_CONNECTED:
		rc = mqtt_service_manager_subscribe(MQTT_SHELL_REQUEST_TOPIC,
						    MQTT_QOS_0_AT_MOST_ONCE);
		if (rc != 0) {
			LOG_WRN("Failed to subscribe MQTT shell topic: %d", rc);
		}
		break;

	case MQTT_SERVICE_MANAGER_PUBLISH:
		if (publish != NULL && topic_matches(publish,
						     MQTT_SHELL_REQUEST_TOPIC)) {
			handle_command(publish);
		}
		break;

	default:
		break;
	}
}

static int mqtt_shell_service_init(void)
{
	return mqtt_service_manager_register_handler(
		mqtt_shell_service_event_handler, NULL);
}

SYS_INIT(mqtt_shell_service_init, APPLICATION, 97);
