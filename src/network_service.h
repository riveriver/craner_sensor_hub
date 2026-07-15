#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_SERVICE_IPV4_TEXT_LEN 16
#define NETWORK_SERVICE_MAX_HANDLERS 4

enum network_service_state {
	NETWORK_SERVICE_STATE_DOWN,
	NETWORK_SERVICE_STATE_LINK_UP,
	NETWORK_SERVICE_STATE_DHCP_WAITING,
	NETWORK_SERVICE_STATE_READY,
	NETWORK_SERVICE_STATE_FAILED,
};

enum network_service_event {
	NETWORK_SERVICE_EVENT_READY,
	NETWORK_SERVICE_EVENT_LOST,
	NETWORK_SERVICE_EVENT_DHCP_FAILED,
};

struct network_service_status {
	enum network_service_state state;
	bool link_up;
	bool ready;
	uint32_t dhcp_fail_count;
	uint32_t dhcp_retry_delay_ms;
	uint32_t dhcp_lease_time_s;
	uint32_t dhcp_renewal_time_s;
	char ip[NETWORK_SERVICE_IPV4_TEXT_LEN];
	char netmask[NETWORK_SERVICE_IPV4_TEXT_LEN];
	char gateway[NETWORK_SERVICE_IPV4_TEXT_LEN];
	char dhcp_server[NETWORK_SERVICE_IPV4_TEXT_LEN];
};

typedef void (*network_service_event_handler_t)(
	enum network_service_event event,
	const struct network_service_status *status, void *user_data);

int network_service_init(void);
int network_service_start(void);
bool network_service_is_ready(void);
void network_service_get_status(struct network_service_status *out);
const char *network_service_state_name(enum network_service_state state);
int network_service_register_handler(network_service_event_handler_t handler,
				     void *user_data);

#endif /* NETWORK_SERVICE_H */
