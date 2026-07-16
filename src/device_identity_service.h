#ifndef DEVICE_IDENTITY_SERVICE_H
#define DEVICE_IDENTITY_SERVICE_H

#include <stdint.h>

#define DEVICE_IDENTITY_MAC_SIZE 6
#define DEVICE_IDENTITY_SHORT_UID_SIZE 5
#define DEVICE_IDENTITY_NAME_UID_SIZE 2
#define DEVICE_IDENTITY_HOSTNAME_SIZE 64
#define DEVICE_IDENTITY_MDNS_NAME_SIZE (DEVICE_IDENTITY_HOSTNAME_SIZE + sizeof(".local"))
#define DEVICE_IDENTITY_MQTT_CLIENT_ID_SIZE 64

struct device_identity {
	uint8_t mac[DEVICE_IDENTITY_MAC_SIZE];
	uint8_t short_uid[DEVICE_IDENTITY_SHORT_UID_SIZE];
	char short_uid_hex[DEVICE_IDENTITY_SHORT_UID_SIZE * 2 + 1];
	char name_uid_hex[DEVICE_IDENTITY_NAME_UID_SIZE * 2 + 1];
	char company[8];
	char project[24];
	char device_type[16];
	char hostname[DEVICE_IDENTITY_HOSTNAME_SIZE];
	char mdns_name[DEVICE_IDENTITY_MDNS_NAME_SIZE];
	char mqtt_client_id[DEVICE_IDENTITY_MQTT_CLIENT_ID_SIZE];
};

int device_identity_service_init(void);

const struct device_identity *device_identity_get(void);
const uint8_t *device_identity_mac_get(void);
const char *device_identity_short_uid_get(void);
const char *device_identity_company_get(void);
const char *device_identity_project_get(void);
const char *device_identity_device_type_get(void);
const char *device_identity_hostname_get(void);
const char *device_identity_mdns_name_get(void);
const char *device_identity_mqtt_client_id_get(void);

#endif /* DEVICE_IDENTITY_SERVICE_H */
