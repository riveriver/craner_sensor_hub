#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>

LOG_MODULE_REGISTER(ethernet_app, LOG_LEVEL_INF);

#define ETHERNET_APP_STACK_SIZE 1536
#define ETHERNET_APP_PRIORITY 7
#define ETHERNET_APP_STATIC_IP "192.168.18.32"
#define ETHERNET_APP_NETMASK "255.255.255.0"

static bool print_ipv4_addr(struct net_if *iface)
{
	bool found = false;

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char addr[NET_IPV4_ADDR_LEN];
		char netmask[NET_IPV4_ADDR_LEN];
		char gw[NET_IPV4_ADDR_LEN];
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		if (ipv4->unicast[i].ipv4.addr_type != NET_ADDR_MANUAL) {
			continue;
		}

		LOG_INF("Ethernet IPv4 address: %s",
			net_addr_ntop(AF_INET, &ipv4->unicast[i].ipv4.address.in_addr,
				      addr, sizeof(addr)));
		LOG_INF("Ethernet IPv4 netmask: %s",
			net_addr_ntop(AF_INET, &ipv4->unicast[i].netmask,
				      netmask, sizeof(netmask)));
		LOG_INF("Ethernet IPv4 gateway: %s",
			net_addr_ntop(AF_INET, &ipv4->gw, gw, sizeof(gw)));
		found = true;
	}

	return found;
}

static int configure_static_ipv4(struct net_if *iface)
{
	struct net_in_addr addr;
	struct net_in_addr netmask;

	if (net_addr_pton(AF_INET, ETHERNET_APP_STATIC_IP, &addr) < 0) {
		LOG_ERR("Invalid static IPv4 address: %s", ETHERNET_APP_STATIC_IP);
		return -EINVAL;
	}

	if (net_addr_pton(AF_INET, ETHERNET_APP_NETMASK, &netmask) < 0) {
		LOG_ERR("Invalid static IPv4 netmask: %s", ETHERNET_APP_NETMASK);
		return -EINVAL;
	}

	if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_ERR("Failed to add static IPv4 address %s", ETHERNET_APP_STATIC_IP);
		return -EIO;
	}

	if (!net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask)) {
		LOG_ERR("Failed to set static IPv4 netmask %s", ETHERNET_APP_NETMASK);
		return -EIO;
	}

	return 0;
}

static void ethernet_app_thread(void)
{
	struct net_if *iface;
	iface = net_if_get_default();
	if (iface == NULL) {
		LOG_ERR("No default network interface");
		return;
	}

	(void)configure_static_ipv4(iface);
	(void)print_ipv4_addr(iface);
}

K_THREAD_DEFINE(ethernet_app_tid, ETHERNET_APP_STACK_SIZE,
		ethernet_app_thread, NULL, NULL, NULL,
		ETHERNET_APP_PRIORITY, 0, 0);
