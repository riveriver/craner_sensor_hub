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
LOG_MODULE_REGISTER(tcp_modbus, LOG_LEVEL_INF);

#define MODBUS_TCP_SERVER_STACK_SIZE 3072
#define MODBUS_TCP_SERVER_PRIORITY 8
#define MODBUS_TCP_PORT 502

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

static int modbus_tcp_connection(int client)
{
	uint8_t header[MODBUS_MBAP_AND_FC_LENGTH];
	int rc;
	int data_len;

	rc = recv(client, header, sizeof(header), MSG_WAITALL);
	if (rc <= 0) {
		return rc == 0 ? -ENOTCONN : -errno;
	}

	LOG_HEXDUMP_DBG(header, sizeof(header), "h:>");
	modbus_raw_get_header(&tmp_adu, header);
	data_len = tmp_adu.length;

	rc = recv(client, tmp_adu.data, data_len, MSG_WAITALL);
	if (rc <= 0) {
		return rc == 0 ? -ENOTCONN : -errno;
	}

	LOG_HEXDUMP_DBG(tmp_adu.data, tmp_adu.length, "d:>");
	if (modbus_raw_submit_rx(server_iface, &tmp_adu)) {
		LOG_ERR("Failed to submit raw ADU");
		return -EIO;
	}

	if (k_sem_take(&received, K_MSEC(1000)) != 0) {
		LOG_ERR("MODBUS RAW wait time expired");
		modbus_raw_set_server_failure(&tmp_adu);
	}

	return modbus_tcp_reply(client, &tmp_adu);
}

static void modbus_tcp_server_thread(void)
{
	int serv;
	struct sockaddr_in bind_addr;
	static int counter;

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

	if (listen(serv, 5) < 0) {
		LOG_ERR("error: listen: %d", errno);
		return;
	}

	LOG_INF("Started MODBUS TCP server example on port %d", MODBUS_TCP_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		char addr_str[INET_ADDRSTRLEN];
		int client;
		int rc;

		client = accept(serv, (struct sockaddr *)&client_addr,
				&client_addr_len);

		if (client < 0) {
			LOG_ERR("error: accept: %d", errno);
			continue;
		}

		inet_ntop(client_addr.sin_family, &client_addr.sin_addr,
			  addr_str, sizeof(addr_str));
		LOG_INF("Connection #%d from %s", counter++, addr_str);

		do {
			rc = modbus_tcp_connection(client);
		} while (!rc);

		close(client);
		LOG_INF("Connection from %s closed, errno %d", addr_str, rc);
	}
}

K_THREAD_DEFINE(modbus_tcp_server_tid, MODBUS_TCP_SERVER_STACK_SIZE,
		modbus_tcp_server_thread, NULL, NULL, NULL,
		MODBUS_TCP_SERVER_PRIORITY, 0, 0);
