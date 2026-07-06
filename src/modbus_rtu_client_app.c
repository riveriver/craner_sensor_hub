#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(modbus_rtu_client_app, LOG_LEVEL_INF);

#define MODBUS_CLIENT_STACK_SIZE 2048
#define MODBUS_CLIENT_PRIORITY 6

#define MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

#define MODBUS_CLIENT_UNIT_ID 1
#define MODBUS_CLIENT_BAUDRATE 115200
#define MODBUS_CLIENT_RX_TIMEOUT_US 20000
#define MODBUS_CLIENT_POLL_PERIOD_MS 40
#define MODBUS_CLIENT_START_ADDR 0x0002
#define MODBUS_CLIENT_REGISTER_COUNT 2

static int modbus_client_iface = -1;

static const struct modbus_iface_param modbus_client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = MODBUS_CLIENT_RX_TIMEOUT_US,
	.serial = {
		.baud = MODBUS_CLIENT_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};

static int modbus_rtu_client_init(void)
{
	const char iface_name[] = DEVICE_DT_NAME(MODBUS_NODE);

	modbus_client_iface = modbus_iface_get_by_name(iface_name);
	if (modbus_client_iface < 0) {
		LOG_ERR("Modbus interface %s not found", iface_name);
		return modbus_client_iface;
	}

	return modbus_init_client(modbus_client_iface, modbus_client_param);
}

static void modbus_rtu_client_thread(void)
{
	uint16_t regs[MODBUS_CLIENT_REGISTER_COUNT];
	int64_t next_poll_time;
	int err;

	err = modbus_rtu_client_init();
	if (err != 0) {
		LOG_ERR("Modbus RTU client init failed: %d", err);
		return;
	}

	LOG_INF("Modbus RTU client on USART6 PC6/PC7, unit=%u, addr=0x%04x, qty=%u, %u Hz",
		MODBUS_CLIENT_UNIT_ID, MODBUS_CLIENT_START_ADDR,
		MODBUS_CLIENT_REGISTER_COUNT, 1000 / MODBUS_CLIENT_POLL_PERIOD_MS);

	next_poll_time = k_uptime_get();

	while (1) {
		next_poll_time += MODBUS_CLIENT_POLL_PERIOD_MS;

		err = modbus_read_holding_regs(modbus_client_iface,
					       MODBUS_CLIENT_UNIT_ID,
					       MODBUS_CLIENT_START_ADDR,
					       regs,
					       ARRAY_SIZE(regs));
		if (err == 0) {
			LOG_INF("FC03 addr=0x%04x qty=%u value[0]=0x%04x value[1]=0x%04x",
				MODBUS_CLIENT_START_ADDR, ARRAY_SIZE(regs), regs[0], regs[1]);
		} else {
			LOG_WRN("FC03 addr=0x%04x qty=%u failed: %d",
				MODBUS_CLIENT_START_ADDR, ARRAY_SIZE(regs), err);
		}

		int64_t sleep_ms = next_poll_time - k_uptime_get();

		if (sleep_ms > 0) {
			k_sleep(K_MSEC(sleep_ms));
		} else {
			next_poll_time = k_uptime_get();
		}
	}
}

K_THREAD_DEFINE(modbus_rtu_client_tid, MODBUS_CLIENT_STACK_SIZE,
		modbus_rtu_client_thread, NULL, NULL, NULL,
		MODBUS_CLIENT_PRIORITY, 0, 0);
