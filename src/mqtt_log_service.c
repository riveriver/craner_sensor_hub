#include "device_identity_service.h"
#include "mqtt_service_manager.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend_mqtt.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

LOG_MODULE_REGISTER(mqtt_log_service, CONFIG_LOG_DEFAULT_LEVEL);

static char mqtt_log_topic[96];
static bool mqtt_log_topic_configured;

static int mqtt_log_service_configure_boot_topic_once(void)
{
	int len;

	if (mqtt_log_topic_configured) {
		return 0;
	}

	len = snprintk(mqtt_log_topic, sizeof(mqtt_log_topic),
		       "%s/%s/%s/log/emb",
		       device_identity_company_get(),
		       device_identity_project_get(),
		       device_identity_device_type_get());
	if (len < 0) {
		return len;
	}

	if ((size_t)len >= sizeof(mqtt_log_topic)) {
		return -EMSGSIZE;
	}

	len = log_backend_mqtt_topic_set(mqtt_log_topic);
	if (len == 0) {
		mqtt_log_topic_configured = true;
	}

	return len;
}

static void mqtt_log_service_event_handler(
	enum mqtt_service_manager_event event, struct mqtt_client *client,
	const struct mqtt_service_manager_publish *publish, void *user_data)
{
	ARG_UNUSED(publish);
	ARG_UNUSED(user_data);

	switch (event) {
	case MQTT_SERVICE_MANAGER_CONNECTED:
		(void)mqtt_log_service_configure_boot_topic_once();
		(void)log_backend_mqtt_client_set(client);
		break;

	case MQTT_SERVICE_MANAGER_DISCONNECTED:
		(void)log_backend_mqtt_client_set(NULL);
		break;

	default:
		break;
	}
}

static int mqtt_log_service_init(void)
{
	return mqtt_service_manager_register_handler(
		mqtt_log_service_event_handler, NULL);
}

SYS_INIT(mqtt_log_service_init, APPLICATION, 96);
