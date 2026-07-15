#ifndef MQTT_SERVICE_MANAGER_H
#define MQTT_SERVICE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/net/mqtt.h>

enum mqtt_service_manager_event {
	MQTT_SERVICE_MANAGER_CONNECTED,
	MQTT_SERVICE_MANAGER_DISCONNECTED,
	MQTT_SERVICE_MANAGER_PUBLISH,
};

struct mqtt_service_manager_publish {
	const char *topic;
	size_t topic_len;
	const uint8_t *payload;
	size_t payload_len;
	enum mqtt_qos qos;
};

typedef void (*mqtt_service_manager_event_handler_t)(
	enum mqtt_service_manager_event event, struct mqtt_client *client,
	const struct mqtt_service_manager_publish *publish, void *user_data);

bool mqtt_service_manager_is_connected(void);
struct mqtt_client *mqtt_service_manager_client_get(void);
const char *mqtt_service_manager_device_name_get(void);
const char *mqtt_service_manager_device_uid_get(void);

int mqtt_service_manager_publish(const char *topic, const void *payload,
				 size_t payload_len, enum mqtt_qos qos,
				 bool retain);
int mqtt_service_manager_subscribe(const char *topic, enum mqtt_qos qos);
int mqtt_service_manager_register_handler(
	mqtt_service_manager_event_handler_t handler, void *user_data);

#endif /* MQTT_SERVICE_MANAGER_H */
