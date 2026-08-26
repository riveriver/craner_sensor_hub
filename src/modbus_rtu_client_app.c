#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "modbus_register_service.h"
#include "system_health_app.h"

LOG_MODULE_REGISTER(modbus_rtu_client_app, CONFIG_LOG_DEFAULT_LEVEL);

#define ANEMOMETER_RS485_NODE DT_ALIAS(rs485_usart6)

#if defined(CONFIG_ENABLE_READ_ANEMOMETER_THREAD)
BUILD_ASSERT(DT_NODE_HAS_STATUS(ANEMOMETER_RS485_NODE, okay),
	     "Missing rs485-usart6 alias");
#endif

#define MODBUS_ANEMOMETER_STACK_SIZE 2048
#define MODBUS_ANEMOMETER_PRIORITY 6
#define MODBUS_ANEMOMETER_BAUDRATE 115200
#define MODBUS_ANEMOMETER_RX_TIMEOUT_US 100000
#define MODBUS_ANEMOMETER_POLL_PERIOD_MS 1000
#define MODBUS_ANEMOMETER_UNIT_ID 4
#define MODBUS_ANEMOMETER_START_ADDR 0x0009
#define MODBUS_ANEMOMETER_REGISTER_COUNT 5
#define MODBUS_ANEMOMETER_MAX_REGISTER_COUNT 5

BUILD_ASSERT(MODBUS_ANEMOMETER_REGISTER_COUNT <=
	     MODBUS_ANEMOMETER_MAX_REGISTER_COUNT,
	     "Increase MODBUS_ANEMOMETER_MAX_REGISTER_COUNT");

struct modbus_rtu_client_stats {
	uint64_t success_latency_sum_ms;
	uint32_t success_latency_count;
	uint32_t max_success_latency_ms;
	uint32_t success_count;
	uint32_t failure_count;
	int last_error;
	int last_occur_error;
};

struct modbus_anemometer_client {
	const char *name;
	const char *stats_name;
	const char *iface_name;
	uint8_t unit_id;
	uint16_t start_addr;
	uint16_t register_count;
	const char *timestamp_high_name;
	const char *error_code_name;
	enum system_health_event health_event;
	uint32_t start_delay_ms;
	int iface;
	struct modbus_rtu_client_stats stats;
};

static K_MUTEX_DEFINE(modbus_rtu_stats_lock);

static const struct modbus_iface_param modbus_anemometer_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = MODBUS_ANEMOMETER_RX_TIMEOUT_US,
	.serial = {
		.baud = MODBUS_ANEMOMETER_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};

#if defined(CONFIG_ENABLE_READ_ANEMOMETER_THREAD)
static struct modbus_anemometer_client anemometer = {
	.name = "anemometer",
	.stats_name = "anemometer",
	.iface_name = DEVICE_DT_NAME(ANEMOMETER_RS485_NODE),
	.unit_id = MODBUS_ANEMOMETER_UNIT_ID,
	.start_addr = MODBUS_ANEMOMETER_START_ADDR,
	.register_count = MODBUS_ANEMOMETER_REGISTER_COUNT,
	.timestamp_high_name = "REG_ANEMOMETER_TIMESTAMP_H",
	.error_code_name = "REG_ANEMOMETER_ERROR_CODE",
	.health_event = SYSTEM_HEALTH_READ_ANEMOMETER,
	.start_delay_ms = 35,
	.iface = -1,
};
#endif

static uint16_t modbus_rtu_error_code(int err)
{
	if (err < 0) {
		return (uint16_t)(-err);
	}

	return (uint16_t)err;
}

static int modbus_anemometer_client_init(struct modbus_anemometer_client *anem)
{
	anem->iface = modbus_iface_get_by_name(anem->iface_name);
	if (anem->iface < 0) {
		LOG_ERR("%s interface %s not found", anem->name,
			anem->iface_name);
		return anem->iface;
	}

	return modbus_init_client(anem->iface, modbus_anemometer_param);
}

static int modbus_anemometer_record_registers(
	struct modbus_anemometer_client *anem, int comm_err,
	const uint16_t *regs)
{
	uint16_t offline_status = comm_err == 0 ? 0U : 1U;
	uint16_t error_code = modbus_rtu_error_code(comm_err);
	const uint16_t failure_values[] = {
		error_code,
		offline_status,
	};
	uint32_t timestamp_ms;

	if (comm_err != 0) {
		return modbus_register_service_write_inputs_by_name(
			anem->error_code_name, failure_values,
			ARRAY_SIZE(failure_values));
	}

	if (regs == NULL) {
		return -EINVAL;
	}

	timestamp_ms = k_uptime_get_32();

	const uint16_t success_values[] = {
		(uint16_t)(timestamp_ms >> 16),
		(uint16_t)timestamp_ms,
		error_code,
		offline_status,
		(uint16_t)(regs[0] - 4000),
		regs[1],
		regs[2],
		regs[3],
		regs[4],
	};

	return modbus_register_service_write_inputs_by_name(
		anem->timestamp_high_name, success_values,
		ARRAY_SIZE(success_values));
}

static void modbus_anemometer_record_attempt(
	struct modbus_anemometer_client *anem, int err, uint32_t elapsed_ms)
{
	struct modbus_rtu_client_stats *stats = &anem->stats;

	k_mutex_lock(&modbus_rtu_stats_lock, K_FOREVER);

	if (err == 0) {
		stats->success_count++;
		stats->success_latency_count++;
		stats->success_latency_sum_ms += elapsed_ms;
		if (elapsed_ms > stats->max_success_latency_ms) {
			stats->max_success_latency_ms = elapsed_ms;
		}

		LOG_DBG("%s RTU success latency=%u ms", anem->name, elapsed_ms);
	} else {
		stats->failure_count++;
		stats->last_error = err;
		stats->last_occur_error = err;
		k_mutex_unlock(&modbus_rtu_stats_lock);
		return;
	}

	stats->last_error = 0;

	k_mutex_unlock(&modbus_rtu_stats_lock);
}

static void shell_print_anemometer_stats(
	const struct shell *shell, struct modbus_anemometer_client *anem)
{
	uint64_t success_latency_sum_ms;
	uint32_t success_latency_count;
	uint32_t max_success_latency_ms;
	uint32_t success_count;
	uint32_t failure_count;
	int last_occur_error;
	uint32_t avg_ms = 0;
	uint32_t total_count;
	uint32_t failure_rate_x100 = 0;

	k_mutex_lock(&modbus_rtu_stats_lock, K_FOREVER);
	success_latency_sum_ms = anem->stats.success_latency_sum_ms;
	success_latency_count = anem->stats.success_latency_count;
	max_success_latency_ms = anem->stats.max_success_latency_ms;
	success_count = anem->stats.success_count;
	failure_count = anem->stats.failure_count;
	last_occur_error = anem->stats.last_occur_error;
	k_mutex_unlock(&modbus_rtu_stats_lock);

	total_count = success_count + failure_count;
	if (success_latency_count > 0) {
		avg_ms = (uint32_t)(success_latency_sum_ms /
				    success_latency_count);
	}

	if (total_count > 0) {
		failure_rate_x100 =
			(uint32_t)((((uint64_t)failure_count * 10000U) +
				    (total_count / 2U)) / total_count);
	}

	shell_print(shell,
		    "%s: fail=%u.%02u%%(%u/%u) last_occur_error=%d avg=%ums max=%ums%s",
		    anem->stats_name, failure_rate_x100 / 100U,
		    failure_rate_x100 % 100U, failure_count, total_count,
		    last_occur_error, avg_ms, max_success_latency_ms,
		    success_count > 0U ? "" : " waiting_first_success");
}

static void reset_anemometer_stats(struct modbus_anemometer_client *anem)
{
	k_mutex_lock(&modbus_rtu_stats_lock, K_FOREVER);
	anem->stats = (struct modbus_rtu_client_stats){ 0 };
	k_mutex_unlock(&modbus_rtu_stats_lock);
}

static int cmd_show_anemometer_stats(const struct shell *shell, size_t argc,
				     char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Modbus RTU anemometer stats:");
#if defined(CONFIG_ENABLE_READ_ANEMOMETER_THREAD)
	shell_print_anemometer_stats(shell, &anemometer);
#endif

	return 0;
}

SHELL_CMD_REGISTER(show_anemometer_stats, NULL,
		   "Show Modbus RTU anemometer interval statistics.",
		   cmd_show_anemometer_stats);

static int cmd_clear_anemometer_stats(const struct shell *shell, size_t argc,
				      char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

#if defined(CONFIG_ENABLE_READ_ANEMOMETER_THREAD)
	reset_anemometer_stats(&anemometer);
#endif

	shell_print(shell, "Modbus RTU anemometer statistics cleared.");

	return 0;
}

SHELL_CMD_REGISTER(clear_anemometer_stats, NULL,
		   "Clear Modbus RTU anemometer statistics.",
		   cmd_clear_anemometer_stats);

static void modbus_anemometer_thread(void *p1, void *p2, void *p3)
{
	struct modbus_anemometer_client *anem = p1;
	uint16_t regs[MODBUS_ANEMOMETER_MAX_REGISTER_COUNT];
	int64_t next_poll_time;
	int err;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (anem->start_delay_ms) {
		k_sleep(K_MSEC(anem->start_delay_ms));
	}

	err = modbus_anemometer_client_init(anem);
	if (err != 0) {
		LOG_ERR("%s Modbus RTU client init failed: %d", anem->name, err);
		return;
	}

	LOG_INF("%s Modbus RTU client started, iface=%s, 115200 8N1, rx_timeout=%u us, unit=%u, addr=0x%04x, qty=%u",
		anem->name, anem->iface_name, MODBUS_ANEMOMETER_RX_TIMEOUT_US,
		anem->unit_id, anem->start_addr,
		(unsigned int)anem->register_count);

	next_poll_time = k_uptime_get();

	while (1) {
		uint32_t request_start_ms;
		uint32_t elapsed_ms;

		next_poll_time += MODBUS_ANEMOMETER_POLL_PERIOD_MS;

		request_start_ms = k_uptime_get_32();
		err = modbus_read_holding_regs(anem->iface, anem->unit_id,
					       anem->start_addr, regs,
					       anem->register_count);
		elapsed_ms = k_uptime_get_32() - request_start_ms;
		modbus_anemometer_record_attempt(anem, err, elapsed_ms);

		if (err == 0) {
			int write_err;

			system_health_update_event(anem->health_event);

			write_err = modbus_anemometer_record_registers(
				anem, err, regs);
			if (write_err != 0) {
				LOG_WRN_RATELIMIT("%s failed to update input registers: %d",
						   anem->name, write_err);
			}
		} else {
			int write_err;

			write_err = modbus_anemometer_record_registers(
				anem, err, NULL);
			if (write_err != 0) {
				LOG_WRN_RATELIMIT("%s failed to update failure registers: %d",
						   anem->name, write_err);
			}

			LOG_WRN_RATELIMIT("%s FC03 addr=0x%04x qty=%u failed: %d",
					  anem->name, anem->start_addr,
					  (unsigned int)anem->register_count, err);
		}

		int64_t sleep_ms = next_poll_time - k_uptime_get();

		if (sleep_ms > 0) {
			k_sleep(K_MSEC(sleep_ms));
		} else {
			next_poll_time = k_uptime_get();
		}
	}
}

#if defined(CONFIG_ENABLE_READ_ANEMOMETER_THREAD)
K_THREAD_DEFINE(anemometer_tid, MODBUS_ANEMOMETER_STACK_SIZE,
		modbus_anemometer_thread, &anemometer, NULL, NULL,
		MODBUS_ANEMOMETER_PRIORITY, 0, 0);
#endif
