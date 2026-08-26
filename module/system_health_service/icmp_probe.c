#include "system_health_service.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/icmp.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(system_health_service, CONFIG_LOG_DEFAULT_LEVEL);

struct icmp_probe_status {
	bool initialized;
	bool enabled;
	bool target_valid;
	uint32_t consecutive_failures;
	uint32_t last_sequence;
	int last_error;
	int64_t last_check_uptime_ms;
};

static struct net_icmp_ctx icmp_probe_ctx;
static struct net_sockaddr_in icmp_probe_target;
static char icmp_probe_target_text[NET_IPV4_ADDR_LEN];
static K_SEM_DEFINE(icmp_probe_reply_sem, 0, 1);
static atomic_t icmp_probe_sequence;
static struct icmp_probe_status icmp_probe_status = {
	.enabled = true,
};
static uint32_t last_icmp_probe_check_ms;

static void icmp_probe_set_target_text(const char *text)
{
	(void)snprintk(icmp_probe_target_text, sizeof(icmp_probe_target_text),
		       "%s", text);
}

static enum net_verdict icmp_probe_reply_handler(struct net_icmp_ctx *ctx,
						 struct net_pkt *pkt,
						 struct net_icmp_ip_hdr *ip_hdr,
						 struct net_icmp_hdr *icmp_hdr,
						 void *user_data)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(pkt);
	ARG_UNUSED(icmp_hdr);
	ARG_UNUSED(user_data);

	if (ip_hdr == NULL || ip_hdr->family != AF_INET || ip_hdr->ipv4 == NULL) {
		return NET_CONTINUE;
	}

	if (!net_ipv4_addr_cmp_raw(ip_hdr->ipv4->src,
				   icmp_probe_target.sin_addr.s4_addr)) {
		return NET_CONTINUE;
	}

	k_sem_give(&icmp_probe_reply_sem);
	return NET_OK;
}

static int icmp_probe_target_update(void)
{
#if defined(CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_GATEWAY)
	struct net_in_addr gw;
	struct net_if *iface;
#else
	int err;
#endif

	icmp_probe_target.sin_family = AF_INET;
	icmp_probe_target.sin_port = 0;

#if defined(CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_GATEWAY)
	iface = net_if_get_default();
	if (iface == NULL) {
		icmp_probe_set_target_text("no-iface");
		return -ENODEV;
	}

	gw = net_if_ipv4_get_gw(iface);
	if (net_ipv4_is_addr_unspecified(&gw)) {
		icmp_probe_set_target_text("no-gateway");
		return -ENETUNREACH;
	}

	net_ipaddr_copy(&icmp_probe_target.sin_addr, &gw);
	(void)net_addr_ntop(AF_INET, &icmp_probe_target.sin_addr,
			    icmp_probe_target_text,
			    sizeof(icmp_probe_target_text));
	return 0;
#else
	err = zsock_inet_pton(AF_INET,
			      CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_IPV4,
			      &icmp_probe_target.sin_addr);
	if (err != 1) {
		LOG_ERR("Invalid system health ICMP probe target: %s",
			CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_IPV4);
		return -EINVAL;
	}

	icmp_probe_set_target_text(CONFIG_SYS_HEALTH_ICMP_PROBE_TARGET_IPV4);
	return 0;
#endif
}

static int icmp_probe_init(void)
{
	int err;

	err = icmp_probe_target_update();
	if (err != 0) {
		icmp_probe_status.target_valid = false;
		icmp_probe_status.last_error = err;
		return err;
	}

	err = net_icmp_init_ctx(&icmp_probe_ctx, AF_INET,
				NET_ICMPV4_ECHO_REPLY, 0,
				icmp_probe_reply_handler);
	if (err != 0) {
		LOG_ERR("ICMP probe context init failed: %d", err);
		icmp_probe_status.last_error = err;
		return err;
	}

	icmp_probe_status.initialized = true;
	icmp_probe_status.target_valid = true;
	LOG_INF("System health ICMP probe enabled: target=%s period=%d ms timeout=%d ms max_failures=%d",
		icmp_probe_target_text,
		CONFIG_SYS_HEALTH_ICMP_PROBE_PERIOD_MS,
		CONFIG_SYS_HEALTH_ICMP_PROBE_TIMEOUT_MS,
		CONFIG_SYS_HEALTH_ICMP_PROBE_MAX_CONSECUTIVE_FAILURES);

	return 0;
}

static int icmp_probe_once(void)
{
	struct net_icmp_ping_params params = {
		.identifier = (uint16_t)sys_rand32_get(),
		.sequence = (uint16_t)atomic_inc(&icmp_probe_sequence),
		.data_size = 16,
	};
	int err;

	while (k_sem_take(&icmp_probe_reply_sem, K_NO_WAIT) == 0) {
	}

	err = net_icmp_send_echo_request(&icmp_probe_ctx, NULL,
					 (struct net_sockaddr *)&icmp_probe_target,
					 &params, NULL);
	if (err != 0) {
		return err;
	}

	if (k_sem_take(&icmp_probe_reply_sem,
		       K_MSEC(CONFIG_SYS_HEALTH_ICMP_PROBE_TIMEOUT_MS)) != 0) {
		return -ETIMEDOUT;
	}

	return 0;
}

static void icmp_probe_check(uint32_t now_ms)
{
	uint16_t event = (uint16_t)CONFIG_SYS_HEALTH_ICMP_PROBE_EVENT_ID;
	int err;

	if (now_ms < CONFIG_SYS_HEALTH_ICMP_PROBE_STARTUP_GRACE_MS) {
		if (event != 0U) {
			sys_health_event_report(event);
		}
		if (!icmp_probe_status.initialized) {
			(void)icmp_probe_init();
		}
		return;
	}

	if ((uint32_t)(now_ms - last_icmp_probe_check_ms) <
	    CONFIG_SYS_HEALTH_ICMP_PROBE_PERIOD_MS) {
		return;
	}
	last_icmp_probe_check_ms = now_ms;

	if (!icmp_probe_status.initialized) {
		err = icmp_probe_init();
	} else {
		err = icmp_probe_target_update();
	}
	icmp_probe_status.target_valid = err == 0;
	if (err == 0) {
		err = icmp_probe_once();
	}
	icmp_probe_status.last_check_uptime_ms = k_uptime_get();
	icmp_probe_status.last_sequence =
		(uint32_t)atomic_get(&icmp_probe_sequence);
	icmp_probe_status.last_error = err;

	if (err == 0) {
		if (icmp_probe_status.consecutive_failures != 0U) {
			LOG_INF("System health ICMP probe recovered after %u failures",
				icmp_probe_status.consecutive_failures);
		}
		icmp_probe_status.consecutive_failures = 0U;
		if (event != 0U) {
			sys_health_event_report(event);
		}
		return;
	}

	icmp_probe_status.consecutive_failures++;
	LOG_WRN_RATELIMIT("System health ICMP probe failed: target=%s err=%d consecutive=%u/%d",
			  icmp_probe_target_text, err,
			  icmp_probe_status.consecutive_failures,
			  CONFIG_SYS_HEALTH_ICMP_PROBE_MAX_CONSECUTIVE_FAILURES);
}

static void icmp_probe_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (!sys_health_is_initialized()) {
		k_sleep(K_MSEC(100));
	}

	while (1) {
		icmp_probe_check(k_uptime_get_32());
		k_sleep(K_MSEC(100));
	}
}

int sys_health_probe_foreach(sys_health_probe_status_cb_t cb, void *user_data)
{
	struct sys_health_probe_status status = {
		.name = "icmp_probe",
		.enabled = icmp_probe_status.enabled,
		.initialized = icmp_probe_status.initialized,
		.target_valid = icmp_probe_status.target_valid,
		.target = icmp_probe_target_text,
		.event = (uint16_t)CONFIG_SYS_HEALTH_ICMP_PROBE_EVENT_ID,
		.period_ms = CONFIG_SYS_HEALTH_ICMP_PROBE_PERIOD_MS,
		.timeout_ms = CONFIG_SYS_HEALTH_ICMP_PROBE_TIMEOUT_MS,
		.max_consecutive_failures =
			CONFIG_SYS_HEALTH_ICMP_PROBE_MAX_CONSECUTIVE_FAILURES,
		.consecutive_failures = icmp_probe_status.consecutive_failures,
		.last_sequence = icmp_probe_status.last_sequence,
		.last_error = icmp_probe_status.last_error,
		.last_check_uptime_ms = icmp_probe_status.last_check_uptime_ms,
	};

	if (cb == NULL) {
		return -EINVAL;
	}

	cb(&status, user_data);
	return 0;
}

K_THREAD_DEFINE(system_health_icmp_probe_tid,
		CONFIG_SYS_HEALTH_ICMP_PROBE_THREAD_STACK_SIZE,
		icmp_probe_thread, NULL, NULL, NULL,
		CONFIG_SYS_HEALTH_ICMP_PROBE_THREAD_PRIORITY, 0, 0);
