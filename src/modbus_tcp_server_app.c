/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/modbus/modbus.h>

#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/poll.h>
#include <zephyr/posix/netdb.h>

#include <zephyr/net/socket.h>

#include "modbus_register_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tcp_modbus, CONFIG_LOG_DEFAULT_LEVEL);

#define MODBUS_TCP_SERVER_STACK_SIZE 3072
#define MODBUS_TCP_SERVER_PRIORITY 8
#define MODBUS_TCP_PORT 502
#define MODBUS_TCP_MAX_CLIENTS CONFIG_CRANER_MODBUS_TCP_MAX_CLIENTS
#define MODBUS_TCP_POLL_FD_COUNT (MODBUS_TCP_MAX_CLIENTS + 1)
#define MODBUS_TCP_LISTEN_BACKLOG MODBUS_TCP_MAX_CLIENTS
#define MODBUS_TCP_RECV_TIMEOUT_MS 1000

static int custom_read_count;

static bool custom_handler(const int iface,
			   const struct modbus_adu *rx_adu,
			   struct modbus_adu *tx_adu,
			   uint8_t *const excep_code,
			   void *const user_data)
{
	const uint8_t request_len = 2;
	const uint8_t response_len = 6;
	int *read_counter = (int *)user_data;
	uint8_t subfunc;
	uint8_t data_len;

	ARG_UNUSED(iface);

	LOG_INF("Custom Modbus handler called");

	if (rx_adu->length != request_len) {
		LOG_WRN("Custom request length doesn't match");
		*excep_code = MODBUS_EXC_ILLEGAL_DATA_VAL;
		return true;
	}

	subfunc = rx_adu->data[0];
	data_len = rx_adu->data[1];

	LOG_INF("Custom function called with subfunc=%u, data_len=%u", subfunc, data_len);
	(*read_counter)++;
	sys_put_be16(0x5555, tx_adu->data);
	sys_put_be16(0xAAAA, &tx_adu->data[2]);
	sys_put_be16(*read_counter, &tx_adu->data[4]);
	tx_adu->length = response_len;

	return true;
}

MODBUS_CUSTOM_FC_DEFINE(custom, custom_handler, 101, &custom_read_count);

static int coil_rd(uint16_t addr, bool *state)
{
	int err;

	err = modbus_register_service_read_coil(addr, state);
	if (err != 0) {
		return err;
	}

	LOG_DBG("Coil read, addr %u, %d", addr, (int)*state);
	return 0;
}

static int coil_wr(uint16_t addr, bool state)
{
	int err;

	err = modbus_register_service_write_coil(addr, state);
	if (err != 0) {
		return err;
	}

	LOG_INF("Coil write, addr %u, %d", addr, (int)state);
	return 0;
}

static int input_reg_rd(uint16_t addr, uint16_t *reg)
{
	int err;

	err = modbus_register_service_read_input(addr, reg);
	if (err != 0) {
		return err;
	}

	LOG_DBG("Input register read, addr %u, value=0x%04x", addr, *reg);
	return 0;
}

static int holding_reg_rd(uint16_t addr, uint16_t *reg)
{
	int err;

	err = modbus_register_service_read_holding(addr, reg);
	if (err != 0) {
		return err;
	}

	LOG_DBG("Holding register read, addr %u, value=0x%04x", addr, *reg);
	return 0;
}

static int holding_reg_wr(uint16_t addr, uint16_t reg)
{
	int err;

	err = modbus_register_service_write_holding(addr, reg);
	if (err != 0) {
		return err;
	}

	LOG_INF("Holding register write, addr %u, value=0x%04x", addr, reg);
	return 0;
}

static struct modbus_user_callbacks mbs_cbs = {
	.coil_rd = coil_rd,
	.coil_wr = coil_wr,
	.input_reg_rd = input_reg_rd,
	.holding_reg_rd = holding_reg_rd,
	.holding_reg_wr = holding_reg_wr,
};

static struct modbus_adu tmp_adu;
K_SEM_DEFINE(received, 0, 1);
static K_MUTEX_DEFINE(raw_modbus_lock);
static int server_iface;

static int server_raw_cb(const int iface, const struct modbus_adu *adu,
			 void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_DBG("Server raw callback from interface %d", iface);

	tmp_adu.trans_id = adu->trans_id;
	tmp_adu.proto_id = adu->proto_id;
	tmp_adu.length = adu->length;
	tmp_adu.unit_id = adu->unit_id;
	tmp_adu.fc = adu->fc;
	memcpy(tmp_adu.data, adu->data,
	       MIN(adu->length, CONFIG_MODBUS_BUFFER_SIZE));

	LOG_HEXDUMP_DBG(tmp_adu.data, tmp_adu.length, "resp");
	k_sem_give(&received);

	return 0;
}

const static struct modbus_iface_param server_param = {
	.mode = MODBUS_MODE_RAW,
	.server = {
		.user_cb = &mbs_cbs,
		.unit_id = 1,
	},
	.rawcb.raw_tx_cb = server_raw_cb,
	.rawcb.user_data = NULL
};

static int init_modbus_server(void)
{
	char iface_name[] = "RAW_0";
	int err;

	server_iface = modbus_iface_get_by_name(iface_name);

	if (server_iface < 0) {
		LOG_ERR("Failed to get iface index for %s", iface_name);
		return -ENODEV;
	}

	err = modbus_init_server(server_iface, server_param);

	if (err < 0) {
		return err;
	}

	return modbus_register_user_fc(server_iface, &modbus_cfg_custom);
}

static int modbus_tcp_reply(int client, struct modbus_adu *adu)
{
	uint8_t header[MODBUS_MBAP_AND_FC_LENGTH];

	modbus_raw_put_header(adu, header);
	if (send(client, header, sizeof(header), 0) < 0) {
		return -errno;
	}

	if (send(client, adu->data, adu->length, 0) < 0) {
		return -errno;
	}

	return 0;
}

static int modbus_tcp_recv_exact(int client, uint8_t *buf, size_t len)
{
	size_t received_len = 0U;

	while (received_len < len) {
		struct pollfd pfd = {
			.fd = client,
			.events = POLLIN,
		};
		int rc;

		rc = poll(&pfd, 1, MODBUS_TCP_RECV_TIMEOUT_MS);
		if (rc == 0) {
			return -ETIMEDOUT;
		}

		if (rc < 0) {
			return -errno;
		}

		if ((pfd.revents & POLLIN) == 0) {
			if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
				return -ECONNRESET;
			}

			return -EIO;
		}

		rc = recv(client, &buf[received_len], len - received_len, 0);
		if (rc == 0) {
			return -ENOTCONN;
		}

		if (rc < 0) {
			return -errno;
		}

		received_len += (size_t)rc;
	}

	return 0;
}

static int modbus_tcp_connection(int client)
{
	uint8_t header[MODBUS_MBAP_AND_FC_LENGTH];
	int rc;
	int data_len;

	rc = modbus_tcp_recv_exact(client, header, sizeof(header));
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&raw_modbus_lock, K_FOREVER);

	while (k_sem_take(&received, K_NO_WAIT) == 0) {
	}

	LOG_HEXDUMP_DBG(header, sizeof(header), "h:>");
	modbus_raw_get_header(&tmp_adu, header);
	data_len = tmp_adu.length;
	if (data_len > CONFIG_MODBUS_BUFFER_SIZE) {
		k_mutex_unlock(&raw_modbus_lock);
		return -EMSGSIZE;
	}

	rc = modbus_tcp_recv_exact(client, tmp_adu.data, data_len);
	if (rc != 0) {
		k_mutex_unlock(&raw_modbus_lock);
		return rc;
	}

	LOG_HEXDUMP_DBG(tmp_adu.data, tmp_adu.length, "d:>");
	if (modbus_raw_submit_rx(server_iface, &tmp_adu)) {
		LOG_ERR("Failed to submit raw ADU");
		k_mutex_unlock(&raw_modbus_lock);
		return -EIO;
	}

	if (k_sem_take(&received, K_MSEC(1000)) != 0) {
		LOG_ERR("MODBUS RAW wait time expired");
		modbus_raw_set_server_failure(&tmp_adu);
	}

	rc = modbus_tcp_reply(client, &tmp_adu);
	k_mutex_unlock(&raw_modbus_lock);

	return rc;
}

static void modbus_tcp_client_close(struct pollfd *fds, int index)
{
	if (fds[index].fd >= 0) {
		(void)shutdown(fds[index].fd, SHUT_RDWR);
		close(fds[index].fd);
	}

	fds[index].fd = -1;
	fds[index].events = POLLIN;
	fds[index].revents = 0;
}

static int modbus_tcp_client_add(struct pollfd *fds, int client)
{
	for (int i = 1; i < MODBUS_TCP_POLL_FD_COUNT; i++) {
		if (fds[i].fd < 0) {
			fds[i].fd = client;
			fds[i].events = POLLIN;
			fds[i].revents = 0;
			return 0;
		}
	}

	return -ENOMEM;
}

static void modbus_tcp_server_thread(void)
{
	int serv;
	struct sockaddr_in bind_addr;
	static int counter;
	struct pollfd fds[MODBUS_TCP_POLL_FD_COUNT];

	if (init_modbus_server()) {
		LOG_ERR("Modbus TCP server initialization failed");
		return;
	}

	serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (serv < 0) {
		LOG_ERR("error: socket: %d", errno);
		return;
	}

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(MODBUS_TCP_PORT);

	if (bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		LOG_ERR("error: bind: %d", errno);
		return;
	}

	if (listen(serv, MODBUS_TCP_LISTEN_BACKLOG) < 0) {
		LOG_ERR("error: listen: %d", errno);
		return;
	}

	fds[0].fd = serv;
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	for (int i = 1; i < MODBUS_TCP_POLL_FD_COUNT; i++) {
		fds[i].fd = -1;
		fds[i].events = POLLIN;
		fds[i].revents = 0;
	}

	LOG_INF("Started MODBUS TCP server on port %d, max_clients=%d",
		MODBUS_TCP_PORT, MODBUS_TCP_MAX_CLIENTS);

	while (1) {
		int rc;

		rc = poll(fds, ARRAY_SIZE(fds), -1);
		if (rc < 0) {
			LOG_ERR("error: poll: %d", errno);
			continue;
		}

		if ((fds[0].revents & POLLIN) != 0) {
			struct sockaddr_in client_addr;
			socklen_t client_addr_len = sizeof(client_addr);
			char addr_str[INET_ADDRSTRLEN];
			int client;

			client = accept(serv, (struct sockaddr *)&client_addr,
					&client_addr_len);
			if (client < 0) {
				LOG_ERR("error: accept: %d", errno);
			} else {
				inet_ntop(client_addr.sin_family,
					  &client_addr.sin_addr,
					  addr_str, sizeof(addr_str));

				if (modbus_tcp_client_add(fds, client) != 0) {
					LOG_WRN("Rejecting Modbus TCP client from %s: pool full",
						addr_str);
					close(client);
				} else {
					LOG_INF("Connection #%d from %s, fd=%d",
						counter++, addr_str, client);
				}
			}
		}

		for (int i = 1; i < MODBUS_TCP_POLL_FD_COUNT; i++) {
			if (fds[i].fd < 0 || fds[i].revents == 0) {
				continue;
			}

			if ((fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
				LOG_INF("Closing Modbus TCP client fd=%d, revents=0x%x",
					fds[i].fd, fds[i].revents);
				modbus_tcp_client_close(fds, i);
				continue;
			}

			if ((fds[i].revents & POLLIN) != 0) {
				rc = modbus_tcp_connection(fds[i].fd);
				if (rc != 0) {
					LOG_INF("Closing Modbus TCP client fd=%d, rc=%d",
						fds[i].fd, rc);
					modbus_tcp_client_close(fds, i);
				}
			}
		}
	}
}

K_THREAD_DEFINE(modbus_tcp_server_tid, MODBUS_TCP_SERVER_STACK_SIZE,
		modbus_tcp_server_thread, NULL, NULL, NULL,
		MODBUS_TCP_SERVER_PRIORITY, 0, 0);
