#include <errno.h>
#include <stdint.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "modbus_data_model.h"
#include "system_health_app.h"
#include "sensor_manage_app.h"

LOG_MODULE_REGISTER(anemometer_load_app, CONFIG_LOG_DEFAULT_LEVEL);

#define ANEMOMETER_IFACE CONFIG_ANEMOMETER_IFACE_NAME
#define ANEMOMETER_UNIT CONFIG_ANEMOMETER_MODBUS_UNIT_ID
#define ANEMOMETER_ADDR CONFIG_ANEMOMETER_MODBUS_START_ADDR
#define ANEMOMETER_COUNT 5U
#define ANEMOMETER_TEMP_OFFSET 4000U
#define LOAD_ADC_UNIT 2U
#define LOAD_ADC_ADDR 0x0000U
#define LOAD_ADC_COUNT 1U
#define SAMPLE_PERIOD_MS CONFIG_ANEMOMETER_SAMPLE_PERIOD_MS

static const struct modbus_iface_param modbus_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = CONFIG_ANEMOMETER_MODBUS_RX_TIMEOUT_MS * 1000U,
	.serial = {
		.baud = CONFIG_ANEMOMETER_MODBUS_BAUD,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};

K_THREAD_STACK_DEFINE(sensor_stack, CONFIG_ANEMOMETER_THREAD_STACK_SIZE);
static struct k_thread sensor_thread;
static K_MUTEX_DEFINE(load_lock);
static struct {
	uint16_t value;
	uint32_t timestamp_ms;
	uint32_t success_count;
	uint32_t error_count;
	int last_error;
	bool online;
} load_state;

static uint16_t error_code_to_reg(int err)
{
	return err < 0 ? (uint16_t)(-err) : (uint16_t)err;
}

static void write_anemometer_error(int err)
{
	const uint16_t values[] = { error_code_to_reg(err), 1U };

	(void)modbus_data_model_write_inputs_by_name(
		"REG_ANEMOMETER_ERROR_CODE", values, ARRAY_SIZE(values));
}

static void write_anemometer(const uint16_t *regs)
{
	const uint32_t timestamp = k_uptime_get_32();
	const uint16_t values[] = {
		(uint16_t)(timestamp >> 16), (uint16_t)timestamp, 0U, 0U,
		(uint16_t)(regs[0] - ANEMOMETER_TEMP_OFFSET), regs[1], regs[2],
		regs[3], regs[4],
	};

	(void)modbus_data_model_write_inputs_by_name(
		"REG_ANEMOMETER_TIMESTAMP_H", values, ARRAY_SIZE(values));
	system_health_update_event(SYSTEM_HEALTH_READ_ANEMOMETER);
}

static void write_load_adc_error(int err)
{
	const uint16_t values[] = { error_code_to_reg(err), 1U };

	(void)modbus_data_model_write_inputs_by_name(
		"REG_LOAD_ADC_ERROR_CODE", values, ARRAY_SIZE(values));
	k_mutex_lock(&load_lock, K_FOREVER);
	load_state.error_count++;
	load_state.last_error = err;
	load_state.online = false;
	k_mutex_unlock(&load_lock);
	system_health_update_event(SYSTEM_HEALTH_READ_LOAD_ADC);
}

static void write_load_adc(uint16_t value)
{
	const uint32_t timestamp = k_uptime_get_32();
	const uint16_t values[] = {
		(uint16_t)(timestamp >> 16), (uint16_t)timestamp, 0U, 0U, value,
	};

	(void)modbus_data_model_write_inputs_by_name(
		"REG_LOAD_ADC_TIMESTAMP_H", values, ARRAY_SIZE(values));
	k_mutex_lock(&load_lock, K_FOREVER);
	load_state.value = value;
	load_state.timestamp_ms = timestamp;
	load_state.success_count++;
	load_state.last_error = 0;
	load_state.online = true;
	k_mutex_unlock(&load_lock);
}

#if defined(CONFIG_LOAD_ADC_SHELL)
static int cmd_load_sample(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	k_mutex_lock(&load_lock, K_FOREVER);
	shell_print(shell, "online=%s value=%u timestamp_ms=%u last_error=%d",
			load_state.online ? "yes" : "no", load_state.value,
			load_state.timestamp_ms, load_state.last_error);
	k_mutex_unlock(&load_lock);
	return 0;
}

static int cmd_load_stats(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	k_mutex_lock(&load_lock, K_FOREVER);
	shell_print(shell, "success=%u error=%u last_error=%d",
			load_state.success_count, load_state.error_count,
			load_state.last_error);
	k_mutex_unlock(&load_lock);
	return 0;
}

static int cmd_load_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	k_mutex_lock(&load_lock, K_FOREVER);
	shell_print(shell, "enabled=%s online=%s value=%u",
			IS_ENABLED(CONFIG_ENABLE_READ_LOAD_SENSOR) ? "yes" : "no",
			load_state.online ? "yes" : "no", load_state.value);
	k_mutex_unlock(&load_lock);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(load_cmds,
	SHELL_CMD(sample, NULL, "Show latest load ADC sample.", cmd_load_sample),
	SHELL_CMD(stats, NULL, "Show load ADC statistics.", cmd_load_stats),
	SHELL_CMD(status, NULL, "Show load ADC status.", cmd_load_status),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(load, &load_cmds, "Load ADC commands.", NULL);
#endif

static void sensor_thread_entry(void *p1, void *p2, void *p3)
{
	uint16_t anemometer_regs[ANEMOMETER_COUNT];
	uint16_t load_adc_regs[LOAD_ADC_COUNT];
	int64_t next_poll;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int iface = modbus_iface_get_by_name(ANEMOMETER_IFACE);
	if (iface < 0 || modbus_init_client(iface, modbus_param) != 0) {
		LOG_ERR("Failed to initialize USART6 Modbus RTU interface");
		return;
	}

	LOG_INF("Anemometer/load ADC Modbus started: iface=%s 9600 8N1, units=%u/%u",
		ANEMOMETER_IFACE, ANEMOMETER_UNIT, LOAD_ADC_UNIT);
	next_poll = k_uptime_get();

	while (true) {
		int err;

		#if defined(CONFIG_ENABLE_ANEMOMETER_SENSOR)
		if (sensor_manage_is_enabled(SENSOR_MANAGE_ANEMOMETER)) {
		err = modbus_read_input_regs(iface, ANEMOMETER_UNIT,
					      ANEMOMETER_ADDR, anemometer_regs,
					      ANEMOMETER_COUNT);
		if (err == 0) {
			write_anemometer(anemometer_regs);
		} else {
			write_anemometer_error(err);
		}
		}
		#endif

		#if defined(CONFIG_ENABLE_READ_LOAD_SENSOR)
		if (sensor_manage_is_enabled(SENSOR_MANAGE_LOAD_ADC)) {
		err = modbus_read_input_regs(iface, LOAD_ADC_UNIT, LOAD_ADC_ADDR,
					      load_adc_regs, LOAD_ADC_COUNT);
		if (err == 0 && load_adc_regs[0] <= 4095U) {
			write_load_adc(load_adc_regs[0]);
		} else {
			write_load_adc_error(err != 0 ? err : -ERANGE);
		}
		}
		#endif

		next_poll += k_ms_to_ticks_ceil32(SAMPLE_PERIOD_MS);
		k_sleep(K_TIMEOUT_ABS_TICKS(next_poll));
	}
}

static int anemometer_load_app_init(void)
{
	#if !defined(CONFIG_ENABLE_ANEMOMETER_SENSOR) && \
	    !defined(CONFIG_ENABLE_READ_LOAD_SENSOR)
	return 0;
	#else
	if (!sensor_manage_is_enabled(SENSOR_MANAGE_ANEMOMETER) &&
	    !sensor_manage_is_enabled(SENSOR_MANAGE_LOAD_ADC)) {
		return 0;
	}
	k_thread_create(&sensor_thread, sensor_stack,
			K_THREAD_STACK_SIZEOF(sensor_stack), sensor_thread_entry,
			NULL, NULL, NULL, K_PRIO_PREEMPT(CONFIG_ANEMOMETER_THREAD_PRIORITY),
			0, K_NO_WAIT);
	k_thread_name_set(&sensor_thread, "anem_load_modbus");
	return 0;
	#endif
}

SYS_INIT(anemometer_load_app_init, APPLICATION, 95);
