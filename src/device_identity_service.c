#include "device_identity_service.h"
#ifdef CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE
#include "device_param_store.h"
#endif

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(device_identity_service, CONFIG_LOG_DEFAULT_LEVEL);

#define DEVICE_IDENTITY_COMPANY "craner"
#define DEVICE_IDENTITY_DEFAULT_PROJECT "project"
#define DEVICE_IDENTITY_DEFAULT_DEVICE_TYPE "type"
#define STM32_UID_MAX_SIZE 12
#define FNV1A64_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV1A64_PRIME 0x100000001b3ULL

static struct device_identity identity;
static bool identity_ready;

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
	uint64_t hash = FNV1A64_OFFSET_BASIS;

	for (size_t i = 0; i < len; i++) {
		hash ^= data[i];
		hash *= FNV1A64_PRIME;
	}

	return hash;
}

static void short_uid_from_hash(uint64_t hash, uint8_t short_uid[DEVICE_IDENTITY_SHORT_UID_SIZE])
{
	for (size_t i = 0; i < DEVICE_IDENTITY_SHORT_UID_SIZE; i++) {
		short_uid[i] = (uint8_t)(hash >> ((DEVICE_IDENTITY_SHORT_UID_SIZE - 1U - i) * 8U));
	}
}

static void format_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len)
{
	static const char hex[] = "0123456789abcdef";

	if (out_len < (len * 2U + 1U)) {
		return;
	}

	for (size_t i = 0; i < len; i++) {
		out[i * 2U] = hex[bytes[i] >> 4];
		out[i * 2U + 1U] = hex[bytes[i] & 0x0f];
	}

	out[len * 2U] = '\0';
}

static void load_identity_text(const char *key, const char *fallback, char *buf,
			       size_t len)
{
	int rc = -ENOENT;

#ifdef CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE
	rc = device_param_store_get(key, buf, len);
#endif
	if (rc != 0) {
		snprintk(buf, len, "%s", fallback);
	}
}

static int build_identity(void)
{
	uint8_t stm32_uid[STM32_UID_MAX_SIZE] = { 0 };
	ssize_t uid_len;
	uint64_t hash;

	uid_len = hwinfo_get_device_id(stm32_uid, sizeof(stm32_uid));
	if (uid_len <= 0) {
		LOG_ERR("Failed to read STM32 device UID: %d", (int)uid_len);
		return uid_len == 0 ? -ENODATA : (int)uid_len;
	}

	hash = fnv1a64(stm32_uid, (size_t)uid_len);
	short_uid_from_hash(hash, identity.short_uid);

	identity.mac[0] = 0x02;
	memcpy(&identity.mac[1], identity.short_uid, sizeof(identity.short_uid));

	format_hex(identity.short_uid, sizeof(identity.short_uid),
		   identity.short_uid_hex, sizeof(identity.short_uid_hex));
	format_hex(&identity.short_uid[DEVICE_IDENTITY_SHORT_UID_SIZE -
				       DEVICE_IDENTITY_NAME_UID_SIZE],
		   DEVICE_IDENTITY_NAME_UID_SIZE, identity.name_uid_hex,
		   sizeof(identity.name_uid_hex));

	snprintk(identity.company, sizeof(identity.company), "%s",
		 DEVICE_IDENTITY_COMPANY);
	load_identity_text("device/project", DEVICE_IDENTITY_DEFAULT_PROJECT,
			   identity.project, sizeof(identity.project));
	load_identity_text("device/type", DEVICE_IDENTITY_DEFAULT_DEVICE_TYPE,
			   identity.device_type, sizeof(identity.device_type));

	snprintk(identity.hostname, sizeof(identity.hostname), "%s-%s-%s-%s",
		 identity.company, identity.project, identity.device_type,
		 identity.name_uid_hex);
	snprintk(identity.mdns_name, sizeof(identity.mdns_name), "%s.local",
		 identity.hostname);
	snprintk(identity.mqtt_client_id, sizeof(identity.mqtt_client_id), "%s",
		 identity.hostname);

	return 0;
}

static int apply_hostname(void)
{
	int rc;

	rc = net_hostname_set(identity.hostname, strlen(identity.hostname));
	if (rc != 0) {
		LOG_ERR("Failed to set hostname %s: %d", identity.hostname, rc);
		return rc;
	}

	return 0;
}

static int apply_ethernet_mac(void)
{
	struct net_if *iface = net_if_get_default();
	const struct device *dev;
	const struct ethernet_api *eth;
	struct ethernet_config config = { 0 };
	int rc;

	if (iface == NULL) {
		return -ENODEV;
	}

	dev = net_if_get_device(iface);
	if (dev == NULL || dev->api == NULL) {
		return -ENODEV;
	}

	eth = (const struct ethernet_api *)dev->api;
	if (eth->set_config == NULL) {
		return -ENOTSUP;
	}

	memcpy(config.mac_address.addr, identity.mac, sizeof(identity.mac));

	rc = eth->set_config(dev, ETHERNET_CONFIG_TYPE_MAC_ADDRESS, &config);
	if (rc != 0) {
		LOG_ERR("Failed to set Ethernet MAC address: %d", rc);
		return rc;
	}

	rc = net_if_set_link_addr(iface, identity.mac, sizeof(identity.mac),
				  NET_LINK_ETHERNET);
	if (rc != 0) {
		LOG_ERR("Failed to set interface link address: %d", rc);
		return rc;
	}

	return 0;
}

int device_identity_service_init(void)
{
	int rc;

	if (identity_ready) {
		return 0;
	}

	rc = build_identity();
	if (rc != 0) {
		return rc;
	}

	rc = apply_hostname();
	if (rc != 0) {
		return rc;
	}

	rc = apply_ethernet_mac();
	if (rc != 0) {
		return rc;
	}

	identity_ready = true;

	LOG_INF("Device identity: hostname=%s mdns=%s mqtt_client_id=%s mac=%02x:%02x:%02x:%02x:%02x:%02x",
		identity.hostname, identity.mdns_name, identity.mqtt_client_id,
		identity.mac[0], identity.mac[1], identity.mac[2],
		identity.mac[3], identity.mac[4], identity.mac[5]);

	return 0;
}

const struct device_identity *device_identity_get(void)
{
	return identity_ready ? &identity : NULL;
}

const uint8_t *device_identity_mac_get(void)
{
	return identity.mac;
}

const char *device_identity_short_uid_get(void)
{
	return identity.short_uid_hex;
}

const char *device_identity_company_get(void)
{
	return identity.company;
}

const char *device_identity_project_get(void)
{
	return identity.project;
}

const char *device_identity_device_type_get(void)
{
	return identity.device_type;
}

const char *device_identity_hostname_get(void)
{
	return identity.hostname;
}

const char *device_identity_mdns_name_get(void)
{
	return identity.mdns_name;
}

const char *device_identity_mqtt_client_id_get(void)
{
	return identity.mqtt_client_id;
}
