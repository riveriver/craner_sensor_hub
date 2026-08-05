#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "modbus_register_service.h"
#include "system_health_app.h"

LOG_MODULE_REGISTER(modbus_rtu_client_app, CONFIG_LOG_DEFAULT_LEVEL);

#define MODBUS_ENCODER_STACK_SIZE 2048
#define MODBUS_ENCODER_PRIORITY 6

#define MODBUS_SLEWING_ENCODER_NODE DT_ALIAS(modbus_slewing_encoder)
#define MODBUS_LUFFING_ENCODER_NODE DT_ALIAS(modbus_luffing_encoder)
#define MODBUS_HOISTING_ENCODER_NODE DT_ALIAS(modbus_hook_encoder)
#define MODBUS_ANEMOMETER_NODE       DT_ALIAS(modbus_anemometer)

#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
BUILD_ASSERT(DT_NODE_HAS_STATUS(MODBUS_SLEWING_ENCODER_NODE, okay),
	     "Missing modbus-slewing-encoder alias");
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD)
BUILD_ASSERT(DT_NODE_HAS_STATUS(MODBUS_LUFFING_ENCODER_NODE, okay),
	     "Missing modbus-luffing-encoder alias");
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD)
BUILD_ASSERT(DT_NODE_HAS_STATUS(MODBUS_HOISTING_ENCODER_NODE, okay),
	     "Missing modbus-hook-encoder alias");
#endif

#define MODBUS_SLEWING_ENCODER_UNIT_ID 1
#define MODBUS_LUFFING_ENCODER_UNIT_ID  3
#define MODBUS_HOISTING_ENCODER_UNIT_ID 2
#define MODBUS_ENCODER_BAUDRATE 115200
#define MODBUS_ENCODER_RX_TIMEOUT_US 200000
#define MODBUS_ENCODER_POLL_PERIOD_MS 50
#define MODBUS_ENCODER_START_ADDR 0x0002
#define MODBUS_ENCODER_REGISTER_COUNT 2
#define MODBUS_ENCODER_MAX_REGISTER_COUNT 2
#define MODBUS_ANEMOMETER_POLL_PERIOD_MS 50
#define MODBUS_ANEMOMETER_UNIT_ID 4
#define MODBUS_ANEMOMETER_START_ADDR 0x0009
#define MODBUS_ANEMOMETER_REGISTER_COUNT 5
#define MODBUS_ANEMOMETER_MAX_REGISTER_COUNT 5

BUILD_ASSERT(MODBUS_ENCODER_REGISTER_COUNT <= MODBUS_ENCODER_MAX_REGISTER_COUNT,
	     "Increase MODBUS_ENCODER_MAX_REGISTER_COUNT");

struct modbus_encoder_stats {
	uint64_t success_interval_sum_ms;
	uint32_t success_interval_count;
	uint32_t max_success_interval_ms;
	uint32_t last_success_ms;
	uint32_t success_count;
	uint32_t failure_count;
	int last_error;
	int last_occur_error;
	bool has_last_success;
};

struct modbus_encoder_client {
	const char *name;
	const char *stats_name;
	const char *iface_name;
	uint8_t unit_id;
	uint16_t start_addr;
	uint16_t register_count;
	const char *timestamp_high_name;
	const char *timestamp_low_name;
	const char *error_code_name;
	const char *offline_status_name;
	const char *turn_count_name;
	const char *single_value_name;
	enum system_health_event health_event;
	uint32_t start_delay_ms;
	int iface;
	struct modbus_encoder_stats stats;
};

static K_MUTEX_DEFINE(modbus_encoder_stats_lock);

static const struct modbus_iface_param modbus_encoder_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = MODBUS_ENCODER_RX_TIMEOUT_US,
	.serial = {
		.baud = MODBUS_ENCODER_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};

#define MODBUS_ANEMOMETER_BAUDRATE 115200

static const struct modbus_iface_param modbus_anemometer_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = MODBUS_ENCODER_RX_TIMEOUT_US,
	.serial = {
		.baud = MODBUS_ANEMOMETER_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};

#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
static struct modbus_encoder_client slewing_encoder = {
	.name = "slewing encoder",
	.stats_name = "slewing",
	.iface_name = DEVICE_DT_NAME(MODBUS_SLEWING_ENCODER_NODE),
	.unit_id = MODBUS_SLEWING_ENCODER_UNIT_ID,
	.start_addr = MODBUS_ENCODER_START_ADDR,
	.register_count = MODBUS_ENCODER_REGISTER_COUNT,
	.timestamp_high_name = "REG_SLEWING_TIMESTAMP_H",
	.timestamp_low_name = "REG_SLEWING_TIMESTAMP_L",
	.error_code_name = "REG_SLEWING_ERROR_CODE",
	.offline_status_name = "REG_SLEWING_OFFLINE_STATUS",
	.turn_count_name = "REG_SLEWING_TRUN_CNT",
	.single_value_name = "REG_SLEWING_SINAGLE_VAL",
	.health_event = SYSTEM_HEALTH_READ_SLEWING_ENCODER,
	.start_delay_ms = 0,
	.iface = -1,
};
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD)
static struct modbus_encoder_client luffing_encoder = {
	.name = "luffing encoder",
	.stats_name = "luffing",
	.iface_name = DEVICE_DT_NAME(MODBUS_LUFFING_ENCODER_NODE),
	.unit_id = MODBUS_LUFFING_ENCODER_UNIT_ID,
	.start_addr = MODBUS_ENCODER_START_ADDR,
	.register_count = MODBUS_ENCODER_REGISTER_COUNT,
	.timestamp_high_name = "REG_LUFFING_TIMESTAMP_H",
	.timestamp_low_name = "REG_LUFFING_TIMESTAMP_L",
	.error_code_name = "REG_LUFFING_ERROR_CODE",
	.offline_status_name = "REG_LUFFING_OFFLINE_STATUS",
	.turn_count_name = "REG_LUFFING_TRUN_CNT",
	.single_value_name = "REG_LUFFING_SINAGLE_VAL",
	.health_event = SYSTEM_HEALTH_READ_LUFFING_ENCODER,
	.start_delay_ms = 7,
	.iface = -1,
};
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD)
static struct modbus_encoder_client hook_encoder = {
	.name = "hoisting encoder",
	.stats_name = "hoisting",
	.iface_name = DEVICE_DT_NAME(MODBUS_HOISTING_ENCODER_NODE),
	.unit_id = MODBUS_HOISTING_ENCODER_UNIT_ID,
	.start_addr = MODBUS_ENCODER_START_ADDR,
	.register_count = MODBUS_ENCODER_REGISTER_COUNT,
	.timestamp_high_name = "REG_HOISTING_TIMESTAMP_H",
	.timestamp_low_name = "REG_HOISTING_TIMESTAMP_L",
	.error_code_name = "REG_HOISTING_ERROR_CODE",
	.offline_status_name = "REG_HOISTING_OFFLINE_STATUS",
	.turn_count_name = "REG_HOISTING_TRUN_CNT",
	.single_value_name = "REG_HOISTING_SINAGLE_VAL",
	.health_event = SYSTEM_HEALTH_READ_HOISTING_ENCODER,
	.start_delay_ms = 14,
	.iface = -1,
};
#endif

struct modbus_anemometer_client {
	const char *name;
	const char *stats_name;
	const char *iface_name;
	uint8_t unit_id;
	uint16_t start_addr;
	uint16_t register_count;
	const char *timestamp_high_name;
	const char *timestamp_low_name;
	const char *error_code_name;
	const char *offline_status_name;
	const char *temperature_name;
	const char *humidity_name;
	const char *pressure_name;
	const char *wind_speed_name;
	const char *wind_direction_name;
	enum system_health_event health_event;
	uint32_t start_delay_ms;
	int iface;
	struct modbus_encoder_stats stats;
};

#if defined(CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD)
static struct modbus_anemometer_client anemometer = {
	.name = "anemometer",
	.stats_name = "anemometer",
	.iface_name = DEVICE_DT_NAME(MODBUS_ANEMOMETER_NODE),
	.unit_id = MODBUS_ANEMOMETER_UNIT_ID,
	.start_addr = MODBUS_ANEMOMETER_START_ADDR,
	.register_count = MODBUS_ANEMOMETER_REGISTER_COUNT,
	.timestamp_high_name = "REG_ANEMOMETER_TIMESTAMP_H",
	.timestamp_low_name = "REG_ANEMOMETER_TIMESTAMP_L",
	.error_code_name = "REG_ANEMOMETER_ERROR_CODE",
	.offline_status_name = "REG_ANEMOMETER_OFFLINE_STATUS",
	.temperature_name = "REG_ANEMOMETER_TEMPERATURE",
	.humidity_name = "REG_ANEMOMETER_HUMIDITY",
	.pressure_name = "REG_ANEMOMETER_PRESSURE",
	.wind_speed_name = "REG_ANEMOMETER_WIND_SPEED",
	.wind_direction_name = "REG_ANEMOMETER_WIND_DIRECTION",
	.health_event = SYSTEM_HEALTH_READ_ANEMOMETER,
	.start_delay_ms = 21,
	.iface = -1,
};
#endif


static int modbus_encoder_client_init(struct modbus_encoder_client *encoder)
{
	encoder->iface = modbus_iface_get_by_name(encoder->iface_name);
	if (encoder->iface < 0) {
		LOG_ERR("%s interface %s not found", encoder->name, encoder->iface_name);
		return encoder->iface;
	}

	return modbus_init_client(encoder->iface, modbus_encoder_param);
}

static int modbus_anemometer_client_init(struct modbus_anemometer_client *anem)
{
	anem->iface = modbus_iface_get_by_name(anem->iface_name);
	if (anem->iface < 0) {
		LOG_ERR("%s interface %s not found", anem->name, anem->iface_name);
		return anem->iface;
	}

	return modbus_init_client(anem->iface, modbus_anemometer_param);
}

static uint16_t modbus_encoder_error_code(int err);

static int modbus_anemometer_record_registers(struct modbus_anemometer_client *anem,
					      int comm_err, const uint16_t *regs)
{
	uint16_t offline_status = comm_err == 0 ? 0U : 1U;
	uint16_t error_code = modbus_encoder_error_code(comm_err);
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

static uint16_t modbus_encoder_error_code(int err)
{
	if (err < 0) {
		return (uint16_t)(-err);
	}

	return (uint16_t)err;
}

static int modbus_encoder_record_registers(struct modbus_encoder_client *encoder,
					   int comm_err, const uint16_t *regs)
{
	uint16_t offline_status = comm_err == 0 ? 0U : 1U;
	uint16_t error_code = modbus_encoder_error_code(comm_err);
	const uint16_t failure_values[] = {
		error_code,
		offline_status,
	};
	uint32_t timestamp_ms;

	if (comm_err != 0) {
		return modbus_register_service_write_inputs_by_name(
			encoder->error_code_name, failure_values,
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
		regs[0],
		regs[1],
	};

	return modbus_register_service_write_inputs_by_name(
		encoder->timestamp_high_name, success_values,
		ARRAY_SIZE(success_values));
}

static void modbus_encoder_record_attempt(struct modbus_encoder_client *encoder,
					  int err)
{
	struct modbus_encoder_stats *stats = &encoder->stats;
	uint32_t now_ms = k_uptime_get_32();
	uint32_t interval_ms;

	k_mutex_lock(&modbus_encoder_stats_lock, K_FOREVER);

	if (err == 0) {
		stats->success_count++;
	} else {
		stats->failure_count++;
		stats->last_error = err;
		stats->last_occur_error = err;
		k_mutex_unlock(&modbus_encoder_stats_lock);
		return;
	}

	stats->last_error = 0;

	if (!stats->has_last_success) {
		stats->last_success_ms = now_ms;
		stats->has_last_success = true;
		k_mutex_unlock(&modbus_encoder_stats_lock);
		return;
	}

	interval_ms = now_ms - stats->last_success_ms;
	stats->last_success_ms = now_ms;

	stats->success_interval_count++;
	stats->success_interval_sum_ms += interval_ms;
	if (interval_ms > stats->max_success_interval_ms) {
		stats->max_success_interval_ms = interval_ms;
	}

	LOG_DBG("%s RTU success interval=%u ms", encoder->name, interval_ms);

	k_mutex_unlock(&modbus_encoder_stats_lock);
}

static void modbus_anemometer_record_attempt(struct modbus_anemometer_client *anem,
					     int err)
{
	struct modbus_encoder_stats *stats = &anem->stats;
	uint32_t now_ms = k_uptime_get_32();
	uint32_t interval_ms;

	k_mutex_lock(&modbus_encoder_stats_lock, K_FOREVER);

	if (err == 0) {
		stats->success_count++;
	} else {
		stats->failure_count++;
		stats->last_error = err;
		stats->last_occur_error = err;
		k_mutex_unlock(&modbus_encoder_stats_lock);
		return;
	}

	stats->last_error = 0;

	if (!stats->has_last_success) {
		stats->last_success_ms = now_ms;
		stats->has_last_success = true;
		k_mutex_unlock(&modbus_encoder_stats_lock);
		return;
	}

	interval_ms = now_ms - stats->last_success_ms;
	stats->last_success_ms = now_ms;

	stats->success_interval_count++;
	stats->success_interval_sum_ms += interval_ms;
	if (interval_ms > stats->max_success_interval_ms) {
		stats->max_success_interval_ms = interval_ms;
	}

	LOG_DBG("%s RTU success interval=%u ms", anem->name, interval_ms);

	k_mutex_unlock(&modbus_encoder_stats_lock);
}

static void shell_print_encoder_stats(const struct shell *shell,
				      struct modbus_encoder_client *encoder)
{
	uint64_t success_interval_sum_ms;
	uint32_t success_interval_count;
	uint32_t max_success_interval_ms;
	uint32_t success_count;
	uint32_t failure_count;
	int last_occur_error;
	bool has_last_success;
	uint32_t avg_ms = 0;
	uint32_t total_count;
	uint32_t failure_rate_x100 = 0;

	k_mutex_lock(&modbus_encoder_stats_lock, K_FOREVER);
	success_interval_sum_ms = encoder->stats.success_interval_sum_ms;
	success_interval_count = encoder->stats.success_interval_count;
	max_success_interval_ms = encoder->stats.max_success_interval_ms;
	success_count = encoder->stats.success_count;
	failure_count = encoder->stats.failure_count;
	last_occur_error = encoder->stats.last_occur_error;
	has_last_success = encoder->stats.has_last_success;
	k_mutex_unlock(&modbus_encoder_stats_lock);

	total_count = success_count + failure_count;
	if (success_interval_count > 0) {
		avg_ms = (uint32_t)(success_interval_sum_ms /
				    success_interval_count);
	}

	if (total_count > 0) {
		failure_rate_x100 =
			(uint32_t)((((uint64_t)failure_count * 10000U) +
				    (total_count / 2U)) /
				   total_count);
	}

	shell_print(shell,
		    "%s: fail=%u.%02u%%(%u/%u) last_occur_error=%d avg=%ums max=%ums%s",
		    encoder->stats_name, failure_rate_x100 / 100U,
		    failure_rate_x100 % 100U, failure_count, total_count,
		    last_occur_error, avg_ms, max_success_interval_ms,
		    has_last_success ? "" : " waiting_first_success");
}

static void reset_encoder_stats(struct modbus_encoder_client *encoder)
{
	k_mutex_lock(&modbus_encoder_stats_lock, K_FOREVER);
	encoder->stats = (struct modbus_encoder_stats){ 0 };
	k_mutex_unlock(&modbus_encoder_stats_lock);
}

static void shell_print_anemometer_stats(const struct shell *shell,
					 struct modbus_anemometer_client *anem)
{
	uint64_t success_interval_sum_ms;
	uint32_t success_interval_count;
	uint32_t max_success_interval_ms;
	uint32_t success_count;
	uint32_t failure_count;
	int last_occur_error;
	bool has_last_success;
	uint32_t avg_ms = 0;
	uint32_t total_count;
	uint32_t failure_rate_x100 = 0;

	k_mutex_lock(&modbus_encoder_stats_lock, K_FOREVER);
	success_interval_sum_ms = anem->stats.success_interval_sum_ms;
	success_interval_count = anem->stats.success_interval_count;
	max_success_interval_ms = anem->stats.max_success_interval_ms;
	success_count = anem->stats.success_count;
	failure_count = anem->stats.failure_count;
	last_occur_error = anem->stats.last_occur_error;
	has_last_success = anem->stats.has_last_success;
	k_mutex_unlock(&modbus_encoder_stats_lock);

	total_count = success_count + failure_count;
	if (success_interval_count > 0) {
		avg_ms = (uint32_t)(success_interval_sum_ms /
				    success_interval_count);
	}

	if (total_count > 0) {
		failure_rate_x100 =
			(uint32_t)((((uint64_t)failure_count * 10000U) +
				    (total_count / 2U)) /
				   total_count);
	}

	shell_print(shell,
		    "%s: fail=%u.%02u%%(%u/%u) last_occur_error=%d avg=%ums max=%ums%s",
		    anem->stats_name, failure_rate_x100 / 100U,
		    failure_rate_x100 % 100U, failure_count, total_count,
		    last_occur_error, avg_ms, max_success_interval_ms,
		    has_last_success ? "" : " waiting_first_success");
}

static void reset_anemometer_stats(struct modbus_anemometer_client *anem)
{
	k_mutex_lock(&modbus_encoder_stats_lock, K_FOREVER);
	anem->stats = (struct modbus_encoder_stats){ 0 };
	k_mutex_unlock(&modbus_encoder_stats_lock);
}

static int cmd_show_encoder_stats(const struct shell *shell, size_t argc,
				  char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Modbus RTU stats:");
#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
	shell_print_encoder_stats(shell, &slewing_encoder);
#endif
#if defined(CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD)
	shell_print_encoder_stats(shell, &luffing_encoder);
#endif
#if defined(CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD)
	shell_print_encoder_stats(shell, &hook_encoder);
#endif
#if defined(CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD)
	shell_print_anemometer_stats(shell, &anemometer);
#endif

	return 0;
}

SHELL_CMD_REGISTER(show_encoder_stats, NULL,
		   "Show Modbus RTU encoder interval statistics.",
		   cmd_show_encoder_stats);

static int cmd_clear_encoder_stats(const struct shell *shell, size_t argc,
				   char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
	reset_encoder_stats(&slewing_encoder);
#endif
#if defined(CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD)
	reset_encoder_stats(&luffing_encoder);
#endif
#if defined(CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD)
	reset_encoder_stats(&hook_encoder);
#endif
#if defined(CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD)
	reset_anemometer_stats(&anemometer);
#endif

	shell_print(shell, "Modbus RTU encoder statistics cleared.");

	return 0;
}

SHELL_CMD_REGISTER(clear_encoder_stats, NULL,
		   "Clear Modbus RTU encoder statistics.",
		   cmd_clear_encoder_stats);

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
		anem->name, anem->iface_name, MODBUS_ENCODER_RX_TIMEOUT_US,
		anem->unit_id, anem->start_addr,
		(unsigned int)anem->register_count);

	next_poll_time = k_uptime_get();

	while (1) {
		next_poll_time += MODBUS_ANEMOMETER_POLL_PERIOD_MS;

		err = modbus_read_holding_regs(anem->iface,
					       anem->unit_id,
					       anem->start_addr,
					       regs,
					       anem->register_count);
		modbus_anemometer_record_attempt(anem, err);

		if (err == 0) {
			int write_err;

			system_health_update_event(anem->health_event);

			write_err = modbus_anemometer_record_registers(anem, err, regs);
			if (write_err != 0) {
				LOG_WRN_RATELIMIT("%s failed to update input registers: %d",
						   anem->name, write_err);
			}
		} else {
			int write_err;

			write_err = modbus_anemometer_record_registers(anem, err, NULL);
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

static void modbus_encoder_thread(void *p1, void *p2, void *p3)
{
	struct modbus_encoder_client *encoder = p1;
	uint16_t regs[MODBUS_ENCODER_MAX_REGISTER_COUNT];
	int64_t next_poll_time;
	int err;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (encoder->start_delay_ms) {
		k_sleep(K_MSEC(encoder->start_delay_ms));
	}

	err = modbus_encoder_client_init(encoder);
	if (err != 0) {
		LOG_ERR("%s Modbus RTU client init failed: %d", encoder->name, err);
		return;
	}

	LOG_INF("%s Modbus RTU client started, iface=%s, 115200 8N1, rx_timeout=%u us, unit=%u, addr=0x%04x, qty=%u",
		encoder->name, encoder->iface_name, MODBUS_ENCODER_RX_TIMEOUT_US,
		encoder->unit_id, encoder->start_addr,
		(unsigned int)encoder->register_count);

	next_poll_time = k_uptime_get();

	while (1) {
		next_poll_time += MODBUS_ENCODER_POLL_PERIOD_MS;

		err = modbus_read_holding_regs(encoder->iface,
					       encoder->unit_id,
					       encoder->start_addr,
					       regs,
					       encoder->register_count);
		modbus_encoder_record_attempt(encoder, err);

		if (err == 0) {
			int write_err;

			system_health_update_event(encoder->health_event);

			write_err = modbus_encoder_record_registers(encoder, err, regs);
			if (write_err != 0) {
				LOG_WRN_RATELIMIT("%s failed to update input registers: %d",
						   encoder->name, write_err);
			}
		} else {
			int write_err;

			write_err = modbus_encoder_record_registers(encoder, err, NULL);
			if (write_err != 0) {
				LOG_WRN_RATELIMIT("%s failed to update failure registers: %d",
						   encoder->name, write_err);
			}

			LOG_WRN_RATELIMIT("%s FC03 addr=0x%04x qty=%u failed: %d",
					  encoder->name, encoder->start_addr,
					  (unsigned int)encoder->register_count, err);
		}

		int64_t sleep_ms = next_poll_time - k_uptime_get();

		if (sleep_ms > 0) {
			k_sleep(K_MSEC(sleep_ms));
		} else {
			next_poll_time = k_uptime_get();
		}
	}
}

#if defined(CONFIG_CRANER_ENABLE_READ_SLEWING_ENCODER_THREAD)
K_THREAD_DEFINE(slewing_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &slewing_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_LUFFING_ENCODER_THREAD)
K_THREAD_DEFINE(luffing_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &luffing_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_HOISTING_ENCODER_THREAD)
K_THREAD_DEFINE(hook_encoder_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_encoder_thread, &hook_encoder, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);
#endif

#if defined(CONFIG_CRANER_ENABLE_READ_ANEMOMETER_THREAD)
K_THREAD_DEFINE(anemometer_tid, MODBUS_ENCODER_STACK_SIZE,
		modbus_anemometer_thread, &anemometer, NULL, NULL,
		MODBUS_ENCODER_PRIORITY, 0, 0);
#endif
