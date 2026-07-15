#include "mqtt_service_manager.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend_mqtt.h>

LOG_MODULE_REGISTER(mqtt_log_service, CONFIG_LOG_DEFAULT_LEVEL);

static void mqtt_log_service_event_handler(
	enum mqtt_service_manager_event event, struct mqtt_client *client,
	const struct mqtt_service_manager_publish *publish, void *user_data)
{
	ARG_UNUSED(publish);
	ARG_UNUSED(user_data);

	switch (event) {
	case MQTT_SERVICE_MANAGER_CONNECTED:
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
