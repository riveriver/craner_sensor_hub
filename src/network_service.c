#include "network_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(network_service, CONFIG_LOG_DEFAULT_LEVEL);

#define DHCP_RETRY_INITIAL_MS CONFIG_NETWORK_DHCP_RETRY_INITIAL_MS
#define DHCP_RETRY_MAX_MS CONFIG_NETWORK_DHCP_RETRY_MAX_MS

struct network_handler_entry {
	network_service_event_handler_t handler;
	void *user_data;
};

static struct network_service_status status = {
	.state = NETWORK_SERVICE_STATE_DOWN,
};
static struct network_handler_entry handlers[NETWORK_SERVICE_MAX_HANDLERS];
static struct net_mgmt_event_callback network_mgmt_cb;
static struct k_work_delayable dhcp_retry_work;
static K_MUTEX_DEFINE(network_lock);
static bool initialized;

static void notify_handlers(enum network_service_event event)
{
	struct network_service_status snapshot;

	network_service_get_status(&snapshot);

	for (size_t i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].handler != NULL) {
			handlers[i].handler(event, &snapshot,
					    handlers[i].user_data);
		}
	}
}

const char *network_service_state_name(enum network_service_state state)
{
	switch (state) {
	case NETWORK_SERVICE_STATE_DOWN:
		return "down";
	case NETWORK_SERVICE_STATE_LINK_UP:
		return "link_up";
	case NETWORK_SERVICE_STATE_DHCP_WAITING:
		return "dhcp_waiting";
	case NETWORK_SERVICE_STATE_READY:
		return "ready";
	case NETWORK_SERVICE_STATE_FAILED:
		return "failed";
	default:
		return "unknown";
	}
}

static void set_state(enum network_service_state new_state)
{
	k_mutex_lock(&network_lock, K_FOREVER);
	status.state = new_state;
	status.ready = new_state == NETWORK_SERVICE_STATE_READY;
	k_mutex_unlock(&network_lock);
}

static void ipv4_to_text(const struct net_in_addr *addr, char *buf,
			 size_t buf_len)
{
	if (net_addr_ntop(AF_INET, addr, buf, buf_len) == NULL) {
		snprintk(buf, buf_len, "0.0.0.0");
	}
}

static void clear_ipv4_status(void)
{
	k_mutex_lock(&network_lock, K_FOREVER);
	status.ip[0] = '\0';
	status.netmask[0] = '\0';
	status.gateway[0] = '\0';
	status.dhcp_server[0] = '\0';
	status.dhcp_lease_time_s = 0;
	status.dhcp_renewal_time_s = 0;
	status.ready = false;
	k_mutex_unlock(&network_lock);
}

static void update_ipv4_status(struct net_if *iface)
{
	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
	const struct net_if_addr_ipv4 *dhcp_addr = NULL;

	if (ipv4 == NULL) {
		return;
	}

	for (size_t i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		const struct net_if_addr_ipv4 *addr = &ipv4->unicast[i];

		if (addr->ipv4.is_used && addr->ipv4.addr_type == NET_ADDR_DHCP) {
			dhcp_addr = addr;
			break;
		}
	}

	if (dhcp_addr == NULL) {
		return;
	}

	k_mutex_lock(&network_lock, K_FOREVER);
	ipv4_to_text(&dhcp_addr->ipv4.address.in_addr, status.ip,
		     sizeof(status.ip));
	ipv4_to_text(&dhcp_addr->netmask, status.netmask,
		     sizeof(status.netmask));
	ipv4_to_text(&ipv4->gw, status.gateway, sizeof(status.gateway));
	ipv4_to_text(&iface->config.dhcpv4.server_id, status.dhcp_server,
		     sizeof(status.dhcp_server));
	status.dhcp_lease_time_s = iface->config.dhcpv4.lease_time;
	status.dhcp_renewal_time_s = iface->config.dhcpv4.renewal_time;
	status.dhcp_retry_delay_ms = DHCP_RETRY_INITIAL_MS;
	status.state = NETWORK_SERVICE_STATE_READY;
	status.ready = true;
	k_mutex_unlock(&network_lock);
}

static bool refresh_status_from_iface(struct net_if *iface)
{
	struct net_if_ipv4 *ipv4;
	const struct net_if_addr_ipv4 *dhcp_addr = NULL;

	if (iface == NULL) {
		return false;
	}

	k_mutex_lock(&network_lock, K_FOREVER);
	status.link_up = net_if_is_carrier_ok(iface);
	k_mutex_unlock(&network_lock);

	ipv4 = iface->config.ip.ipv4;
	if (ipv4 == NULL) {
		set_state(status.link_up ? NETWORK_SERVICE_STATE_LINK_UP :
					   NETWORK_SERVICE_STATE_DOWN);
		return false;
	}

	for (size_t i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		const struct net_if_addr_ipv4 *addr = &ipv4->unicast[i];

		if (addr->ipv4.is_used && addr->ipv4.addr_type == NET_ADDR_DHCP) {
			dhcp_addr = addr;
			break;
		}
	}

	if (dhcp_addr == NULL) {
		set_state(status.link_up ? NETWORK_SERVICE_STATE_DHCP_WAITING :
					   NETWORK_SERVICE_STATE_DOWN);
		return false;
	}

	update_ipv4_status(iface);
	return true;
}

static uint32_t next_retry_delay_ms(void)
{
	uint32_t delay;

	k_mutex_lock(&network_lock, K_FOREVER);
	delay = status.dhcp_retry_delay_ms;
	if (delay == 0U) {
		delay = DHCP_RETRY_INITIAL_MS;
	}
	status.dhcp_retry_delay_ms = MIN(delay * 2U, DHCP_RETRY_MAX_MS);
	k_mutex_unlock(&network_lock);

	return delay;
}

static void dhcp_retry_work_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_default();
	uint32_t delay;

	ARG_UNUSED(work);

	if (network_service_is_ready()) {
		return;
	}

	if (iface == NULL || !net_if_is_carrier_ok(iface)) {
		set_state(NETWORK_SERVICE_STATE_DOWN);
		delay = next_retry_delay_ms();
		k_work_reschedule(&dhcp_retry_work, K_MSEC(delay));
		return;
	}

	if (refresh_status_from_iface(iface)) {
		k_work_cancel_delayable(&dhcp_retry_work);
		notify_handlers(NETWORK_SERVICE_EVENT_READY);
		return;
	}

	set_state(NETWORK_SERVICE_STATE_DHCP_WAITING);
	LOG_INF("Restarting DHCPv4 client");
	net_dhcpv4_restart(iface);

	delay = next_retry_delay_ms();
	k_work_reschedule(&dhcp_retry_work, K_MSEC(delay));
}

static void schedule_dhcp_retry(void)
{
	uint32_t delay = next_retry_delay_ms();

	LOG_WRN("DHCP not ready, retry in %u ms", delay);
	k_work_reschedule(&dhcp_retry_work, K_MSEC(delay));
}

static void network_event_handler(struct net_mgmt_event_callback *cb,
				  uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (iface == NULL || iface != net_if_get_default()) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_IF_UP:
	case NET_EVENT_ETHERNET_CARRIER_ON:
		k_mutex_lock(&network_lock, K_FOREVER);
		status.link_up = true;
		if (!status.ready) {
			status.state = NETWORK_SERVICE_STATE_LINK_UP;
		}
		k_mutex_unlock(&network_lock);
		break;

	case NET_EVENT_IPV4_DHCP_START:
		set_state(NETWORK_SERVICE_STATE_DHCP_WAITING);
		break;

	case NET_EVENT_IPV4_DHCP_BOUND:
	case NET_EVENT_IPV4_ADDR_ADD:
		update_ipv4_status(iface);
		k_work_cancel_delayable(&dhcp_retry_work);
		LOG_INF("DHCP bound: ip=%s gateway=%s lease=%us",
			status.ip, status.gateway, status.dhcp_lease_time_s);
		notify_handlers(NETWORK_SERVICE_EVENT_READY);
		break;

	case NET_EVENT_IPV4_ADDR_DEL:
	case NET_EVENT_IPV4_DHCP_STOP:
	case NET_EVENT_IF_DOWN:
	case NET_EVENT_ETHERNET_CARRIER_OFF:
		k_mutex_lock(&network_lock, K_FOREVER);
		status.link_up = net_if_is_carrier_ok(iface);
		k_mutex_unlock(&network_lock);
		clear_ipv4_status();
		set_state(status.link_up ? NETWORK_SERVICE_STATE_LINK_UP :
					   NETWORK_SERVICE_STATE_DOWN);
		notify_handlers(NETWORK_SERVICE_EVENT_LOST);
		schedule_dhcp_retry();
		break;

	default:
		break;
	}
}

int network_service_register_handler(network_service_event_handler_t handler,
				     void *user_data)
{
	if (handler == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&network_lock, K_FOREVER);

	for (size_t i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].handler == NULL) {
			handlers[i].handler = handler;
			handlers[i].user_data = user_data;
			k_mutex_unlock(&network_lock);
			return 0;
		}
	}

	k_mutex_unlock(&network_lock);

	return -ENOMEM;
}

int network_service_init(void)
{
	if (initialized) {
		return 0;
	}

	k_work_init_delayable(&dhcp_retry_work, dhcp_retry_work_handler);

	net_mgmt_init_event_callback(&network_mgmt_cb, network_event_handler,
				     NET_EVENT_IF_UP |
					     NET_EVENT_IF_DOWN |
					     NET_EVENT_ETHERNET_CARRIER_ON |
					     NET_EVENT_ETHERNET_CARRIER_OFF |
					     NET_EVENT_IPV4_DHCP_START |
					     NET_EVENT_IPV4_DHCP_BOUND |
					     NET_EVENT_IPV4_DHCP_STOP |
					     NET_EVENT_IPV4_ADDR_ADD |
					     NET_EVENT_IPV4_ADDR_DEL);
	net_mgmt_add_event_callback(&network_mgmt_cb);

	k_mutex_lock(&network_lock, K_FOREVER);
	status.dhcp_retry_delay_ms = DHCP_RETRY_INITIAL_MS;
	k_mutex_unlock(&network_lock);

	initialized = true;
	return 0;
}

int network_service_start(void)
{
	struct net_if *iface = net_if_get_default();
	int rc;

	(void)network_service_init();

	if (refresh_status_from_iface(iface)) {
		notify_handlers(NETWORK_SERVICE_EVENT_READY);
		return 0;
	}

	set_state(NETWORK_SERVICE_STATE_DHCP_WAITING);

	rc = net_config_init("Initializing Craner Ethernet", NET_CONFIG_NEED_IPV4,
			     CONFIG_NET_CONFIG_INIT_TIMEOUT * MSEC_PER_SEC);

	if (refresh_status_from_iface(iface)) {
		notify_handlers(NETWORK_SERVICE_EVENT_READY);
		return 0;
	}

	if (rc != 0 && !network_service_is_ready()) {
		k_mutex_lock(&network_lock, K_FOREVER);
		status.state = NETWORK_SERVICE_STATE_FAILED;
		status.ready = false;
		status.dhcp_fail_count++;
		k_mutex_unlock(&network_lock);
		notify_handlers(NETWORK_SERVICE_EVENT_DHCP_FAILED);
		schedule_dhcp_retry();
		return rc;
	}

	return 0;
}

bool network_service_is_ready(void)
{
	bool ready;

	k_mutex_lock(&network_lock, K_FOREVER);
	ready = status.ready;
	k_mutex_unlock(&network_lock);

	return ready;
}

void network_service_get_status(struct network_service_status *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&network_lock, K_FOREVER);
	*out = status;
	k_mutex_unlock(&network_lock);
}
