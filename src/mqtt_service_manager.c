#include "mqtt_service_manager.h"
#include "device_identity_service.h"
#include "network_service.h"
#include "time_service.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(mqtt_service_manager, CONFIG_LOG_DEFAULT_LEVEL);

#define MQTT_SERVICE_MANAGER_STACK_SIZE CONFIG_CRANER_MQTT_SERVICE_MANAGER_STACK_SIZE
#define MQTT_SERVICE_MANAGER_PRIORITY 8
#define MQTT_SERVICE_MANAGER_CONNECT_RETRY_MS 5000
#define MQTT_SERVICE_MANAGER_CONNECT_TIMEOUT_MS 5000
#define MQTT_SERVICE_MANAGER_POLL_TIMEOUT_MS 500
#define MQTT_SERVICE_MANAGER_NETWORK_WAIT_MS 1000
#define MQTT_SERVICE_MANAGER_MAX_HANDLERS 4
#define MQTT_SERVICE_MANAGER_MAX_PAYLOAD_SIZE CONFIG_CRANER_MQTT_SERVICE_MANAGER_RX_PAYLOAD_SIZE

static struct mqtt_client mqtt_client_ctx;
static struct sockaddr_in broker;
static struct zsock_pollfd mqtt_fds[1];
static uint8_t mqtt_rx_buf[CONFIG_CRANER_MQTT_SERVICE_MANAGER_RX_BUFFER_SIZE];
static uint8_t mqtt_tx_buf[CONFIG_CRANER_MQTT_SERVICE_MANAGER_TX_BUFFER_SIZE];
static uint8_t publish_payload_buf[MQTT_SERVICE_MANAGER_MAX_PAYLOAD_SIZE];

static K_MUTEX_DEFINE(mqtt_manager_lock);

struct mqtt_service_handler_entry {
	mqtt_service_manager_event_handler_t handler;
	void *user_data;
};

static struct mqtt_service_handler_entry handlers[MQTT_SERVICE_MANAGER_MAX_HANDLERS];
static bool mqtt_connected;

bool mqtt_service_manager_is_connected(void)
{
	bool connected;

	k_mutex_lock(&mqtt_manager_lock, K_FOREVER);
	connected = mqtt_connected;
	k_mutex_unlock(&mqtt_manager_lock);

	return connected;
}

struct mqtt_client *mqtt_service_manager_client_get(void)
{
	return &mqtt_client_ctx;
}

const char *mqtt_service_manager_device_name_get(void)
{
	return device_identity_hostname_get();
}

const char *mqtt_service_manager_device_uid_get(void)
{
	return device_identity_short_uid_get();
}

int mqtt_service_manager_register_handler(
	mqtt_service_manager_event_handler_t handler, void *user_data)
{
	if (handler == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&mqtt_manager_lock, K_FOREVER);

	for (size_t i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].handler == NULL) {
			handlers[i].handler = handler;
			handlers[i].user_data = user_data;
			k_mutex_unlock(&mqtt_manager_lock);
			return 0;
		}
	}

	k_mutex_unlock(&mqtt_manager_lock);

	return -ENOMEM;
}

static void notify_handlers(enum mqtt_service_manager_event event,
			    struct mqtt_client *client,
			    const struct mqtt_service_manager_publish *publish)
{
	for (size_t i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].handler != NULL) {
			handlers[i].handler(event, client, publish,
					    handlers[i].user_data);
		}
	}
}

static void mqtt_service_manager_publish_online_status(void)
{
	struct network_service_status net_status;
	char topic[96];
	char payload[512];
	char iso_time[32];
	const char *uid = device_identity_short_uid_get();
	const char *time_text = "null";
	int len;
	int rc;

	network_service_get_status(&net_status);
	if (time_service_format_iso8601(iso_time, sizeof(iso_time)) == 0) {
		time_text = iso_time;
	}

	snprintk(topic, sizeof(topic), "craner/test001/mt2r/%s/status/online",
		 uid);

	len = snprintk(payload, sizeof(payload),
		      "{\"company\":\"craner\",\"project\":\"test001\","
		      "\"device_type\":\"mt2r\",\"uid\":\"%s\","
		      "\"hostname\":\"%s\",\"mdns\":\"%s\","
		      "\"mqtt_client_id\":\"%s\","
		      "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
		      "\"ip\":\"%s\",\"gateway\":\"%s\","
		      "\"time_valid\":%s,\"time\":%s%s%s,"
		      "\"uptime_ms\":%lld}",
		      uid, device_identity_hostname_get(),
		      device_identity_mdns_name_get(),
		      device_identity_mqtt_client_id_get(),
		      device_identity_mac_get()[0], device_identity_mac_get()[1],
		      device_identity_mac_get()[2], device_identity_mac_get()[3],
		      device_identity_mac_get()[4], device_identity_mac_get()[5],
		      net_status.ip, net_status.gateway,
		      time_service_is_time_valid() ? "true" : "false",
		      time_service_is_time_valid() ? "\"" : "", time_text,
		      time_service_is_time_valid() ? "\"" : "",
		      (long long)k_uptime_get());

	if (len < 0) {
		return;
	}

	rc = mqtt_service_manager_publish(topic, payload,
					  MIN((size_t)len, sizeof(payload) - 1U),
					  MQTT_QOS_0_AT_MOST_ONCE, true);
	if (rc != 0) {
		LOG_WRN("Failed to publish online status: %d", rc);
	}
}

int mqtt_service_manager_publish(const char *topic, const void *payload,
				 size_t payload_len, enum mqtt_qos qos,
				 bool retain)
{
	struct mqtt_publish_param param = { 0 };

	if (topic == NULL || payload == NULL) {
		return -EINVAL;
	}

	if (!mqtt_service_manager_is_connected()) {
		return -ENOTCONN;
	}

	param.message.topic.topic.utf8 = (uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.topic.qos = qos;
	param.message.payload.data = (void *)payload;
	param.message.payload.len = payload_len;
	param.message_id = sys_rand16_get();
	param.dup_flag = 0U;
	param.retain_flag = retain ? 1U : 0U;

	return mqtt_publish(&mqtt_client_ctx, &param);
}

int mqtt_service_manager_subscribe(const char *topic, enum mqtt_qos qos)
{
	struct mqtt_topic sub_topic = { 0 };
	struct mqtt_subscription_list sub_list = { 0 };

	if (topic == NULL) {
		return -EINVAL;
	}

	if (!mqtt_service_manager_is_connected()) {
		return -ENOTCONN;
	}

	sub_topic.topic.utf8 = (uint8_t *)topic;
	sub_topic.topic.size = strlen(topic);
	sub_topic.qos = qos;

	sub_list.list = &sub_topic;
	sub_list.list_count = 1U;
	sub_list.message_id = sys_rand16_get();

	return mqtt_subscribe(&mqtt_client_ctx, &sub_list);
}

static int broker_init(void)
{
	struct zsock_addrinfo hints = { 0 };
	struct zsock_addrinfo *result;
	char service[6];
	int rc;

	snprintk(service, sizeof(service), "%d",
		 CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_PORT);

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	rc = zsock_getaddrinfo(CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_HOST,
			       service, &hints, &result);
	if (rc != 0) {
		LOG_WRN("Failed to resolve MQTT broker %s:%s: %d",
			CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_HOST,
			service, rc);
		return -EHOSTUNREACH;
	}

	memcpy(&broker, result->ai_addr, sizeof(broker));
	zsock_freeaddrinfo(result);

	broker.sin_family = AF_INET;
	broker.sin_port = htons(CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_PORT);

	return 0;
}

static int client_init(void)
{
	static struct mqtt_utf8 username = {
		.utf8 = (uint8_t *)CONFIG_CRANER_MQTT_SERVICE_MANAGER_USERNAME,
		.size = sizeof(CONFIG_CRANER_MQTT_SERVICE_MANAGER_USERNAME) - 1U,
	};
	static struct mqtt_utf8 password = {
		.utf8 = (uint8_t *)CONFIG_CRANER_MQTT_SERVICE_MANAGER_PASSWORD,
		.size = sizeof(CONFIG_CRANER_MQTT_SERVICE_MANAGER_PASSWORD) - 1U,
	};
	int rc;

	mqtt_client_init(&mqtt_client_ctx);
	rc = broker_init();
	if (rc != 0) {
		return rc;
	}

	mqtt_client_ctx.broker = (struct sockaddr *)&broker;
	mqtt_client_ctx.evt_cb = NULL;
	mqtt_client_ctx.client_id.utf8 =
		(uint8_t *)device_identity_mqtt_client_id_get();
	mqtt_client_ctx.client_id.size =
		strlen(device_identity_mqtt_client_id_get());
	mqtt_client_ctx.password = &password;
	mqtt_client_ctx.user_name = &username;
	mqtt_client_ctx.protocol_version = MQTT_VERSION_3_1_1;
	mqtt_client_ctx.rx_buf = mqtt_rx_buf;
	mqtt_client_ctx.rx_buf_size = sizeof(mqtt_rx_buf);
	mqtt_client_ctx.tx_buf = mqtt_tx_buf;
	mqtt_client_ctx.tx_buf_size = sizeof(mqtt_tx_buf);
	mqtt_client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;

	return 0;
}

static void prepare_fds(void)
{
	mqtt_fds[0].fd = mqtt_client_ctx.transport.tcp.sock;
	mqtt_fds[0].events = ZSOCK_POLLIN;
}

static void clear_fds(void)
{
	mqtt_fds[0].fd = -1;
	mqtt_fds[0].events = 0;
	mqtt_fds[0].revents = 0;
}

static int read_publish_payload(struct mqtt_client *client,
				const struct mqtt_publish_param *param,
				struct mqtt_service_manager_publish *publish)
{
	size_t len = param->message.payload.len;
	int rc;

	if (len >= sizeof(publish_payload_buf)) {
		LOG_WRN("MQTT publish payload too large: %u", (unsigned int)len);
		return -EMSGSIZE;
	}

	rc = mqtt_read_publish_payload_blocking(client, publish_payload_buf, len);
	if (rc < 0) {
		return rc;
	}

	publish_payload_buf[len] = '\0';

	publish->topic = (const char *)param->message.topic.topic.utf8;
	publish->topic_len = param->message.topic.topic.size;
	publish->payload = publish_payload_buf;
	publish->payload_len = len;
	publish->qos = param->message.topic.qos;

	return 0;
}

static void mqtt_evt_handler(struct mqtt_client *client,
			     const struct mqtt_evt *evt)
{
	int rc;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT CONNACK failed: %d", evt->result);
			break;
		}

		k_mutex_lock(&mqtt_manager_lock, K_FOREVER);
		mqtt_connected = true;
		k_mutex_unlock(&mqtt_manager_lock);

		LOG_INF("MQTT connected to %s:%d",
			CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_HOST,
			CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_PORT);
		mqtt_service_manager_publish_online_status();
		notify_handlers(MQTT_SERVICE_MANAGER_CONNECTED, client, NULL);
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_WRN("MQTT disconnected: %d", evt->result);

		k_mutex_lock(&mqtt_manager_lock, K_FOREVER);
		mqtt_connected = false;
		k_mutex_unlock(&mqtt_manager_lock);

		clear_fds();
		notify_handlers(MQTT_SERVICE_MANAGER_DISCONNECTED, client, NULL);
		break;

	case MQTT_EVT_PUBLISH: {
		struct mqtt_service_manager_publish publish = { 0 };

		rc = read_publish_payload(client, &evt->param.publish, &publish);
		if (rc != 0) {
			LOG_WRN("Failed to read MQTT publish payload: %d", rc);
			break;
		}

		notify_handlers(MQTT_SERVICE_MANAGER_PUBLISH, client, &publish);
		break;
	}

	case MQTT_EVT_PUBACK:
	case MQTT_EVT_SUBACK:
	case MQTT_EVT_PINGRESP:
		break;

	default:
		break;
	}
}

static int connect_once(void)
{
	int rc;

	rc = client_init();
	if (rc != 0) {
		return rc;
	}

	mqtt_client_ctx.evt_cb = mqtt_evt_handler;

	rc = mqtt_connect(&mqtt_client_ctx);
	if (rc != 0) {
		return rc;
	}

	prepare_fds();

	rc = zsock_poll(mqtt_fds, ARRAY_SIZE(mqtt_fds),
			MQTT_SERVICE_MANAGER_CONNECT_TIMEOUT_MS);
	if (rc > 0 && (mqtt_fds[0].revents & ZSOCK_POLLIN) != 0) {
		rc = mqtt_input(&mqtt_client_ctx);
		if (rc != 0) {
			return rc;
		}
	}

	if (!mqtt_service_manager_is_connected()) {
		mqtt_abort(&mqtt_client_ctx);
		clear_fds();
		return -ETIMEDOUT;
	}

	return 0;
}

static int process_mqtt(void)
{
	int rc;

	rc = zsock_poll(mqtt_fds, ARRAY_SIZE(mqtt_fds),
			MQTT_SERVICE_MANAGER_POLL_TIMEOUT_MS);
	if (rc < 0) {
		return -errno;
	}

	if (rc > 0 && (mqtt_fds[0].revents & ZSOCK_POLLIN) != 0) {
		rc = mqtt_input(&mqtt_client_ctx);
		if (rc != 0) {
			return rc;
		}
	}

	rc = mqtt_live(&mqtt_client_ctx);
	if (rc != 0 && rc != -EAGAIN) {
		return rc;
	}

	return 0;
}

static void mqtt_service_manager_thread(void)
{
	int rc;

	while (1) {
		while (!network_service_is_ready()) {
			k_sleep(K_MSEC(MQTT_SERVICE_MANAGER_NETWORK_WAIT_MS));
		}

		LOG_INF("Connecting MQTT broker %s:%d",
			CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_HOST,
			CONFIG_CRANER_MQTT_SERVICE_MANAGER_BROKER_PORT);

		rc = connect_once();
		if (rc != 0) {
			LOG_WRN("MQTT connect failed: %d", rc);
			k_sleep(K_MSEC(MQTT_SERVICE_MANAGER_CONNECT_RETRY_MS));
			continue;
		}

		while (mqtt_service_manager_is_connected() &&
		       network_service_is_ready()) {
			rc = process_mqtt();
			if (rc != 0) {
				LOG_WRN("MQTT processing failed: %d", rc);
				break;
			}
		}

		k_mutex_lock(&mqtt_manager_lock, K_FOREVER);
		mqtt_connected = false;
		k_mutex_unlock(&mqtt_manager_lock);
		notify_handlers(MQTT_SERVICE_MANAGER_DISCONNECTED,
				&mqtt_client_ctx, NULL);
		mqtt_abort(&mqtt_client_ctx);
		clear_fds();
		k_sleep(K_MSEC(MQTT_SERVICE_MANAGER_CONNECT_RETRY_MS));
	}
}

K_THREAD_DEFINE(mqtt_service_manager_tid, MQTT_SERVICE_MANAGER_STACK_SIZE,
		mqtt_service_manager_thread, NULL, NULL, NULL,
		MQTT_SERVICE_MANAGER_PRIORITY, 0, 0);
