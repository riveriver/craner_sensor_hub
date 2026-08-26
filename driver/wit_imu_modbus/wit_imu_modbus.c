#include "wit_imu_modbus.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(wit_imu_modbus, LOG_LEVEL_INF);

#define WIT_ANGLE_REG_START 0x003d
#define WIT_STANDARD_PRECISION_ANGLE_REG_COUNT 2
#define WIT_HIGH_PRECISION_ANGLE_REG_COUNT 6
#define WIT_STANDARD_PRECISION_DEG_SCALE_MDEG 180000LL
#define WIT_STANDARD_PRECISION_RAW_FULL_SCALE 32768LL

enum wit_high_precision_angle_reg {
	WIT_HIGH_PRECISION_LROLL,
	WIT_HIGH_PRECISION_HROLL,
	WIT_HIGH_PRECISION_LPITCH,
	WIT_HIGH_PRECISION_HPITCH,
	WIT_HIGH_PRECISION_LYAW,
	WIT_HIGH_PRECISION_HYAW,
};

static int32_t wit_standard_precision_raw_to_mdeg(int16_t raw)
{
	return (int32_t)(((int64_t)raw * WIT_STANDARD_PRECISION_DEG_SCALE_MDEG) /
			 WIT_STANDARD_PRECISION_RAW_FULL_SCALE);
}

static int32_t wit_high_precision_make_mdeg(uint16_t low, uint16_t high)
{
	return (int32_t)(((uint32_t)high << 16) | low);
}

static void wit_imu_copy_raw_regs(struct wit_imu_modbus_sample *sample,
				  const uint16_t *regs, uint8_t count)
{
	sample->raw_reg_count = count;
	for (uint8_t i = 0U; i < count && i < ARRAY_SIZE(sample->raw_regs); i++) {
		sample->raw_regs[i] = regs[i];
	}
}

const char *wit_imu_modbus_model_name(enum wit_imu_modbus_model model)
{
	switch (model) {
	case WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION:
		return "wit_standard_precision";
	case WIT_IMU_MODBUS_MODEL_HIGH_PRECISION:
		return "wit_high_precision";
	default:
		return "unknown";
	}
}

int wit_imu_modbus_init(struct wit_imu_modbus_client *client,
			const struct wit_imu_modbus_config *config)
{
	const struct modbus_iface_param param = {
		.mode = MODBUS_MODE_RTU,
		.rx_timeout = config != NULL ? config->rx_timeout_us : 0U,
		.serial = {
			.baud = config != NULL ? config->baud : 0U,
			.parity = UART_CFG_PARITY_NONE,
			.stop_bits = UART_CFG_STOP_BITS_1,
		},
	};
	int ret;

	if (client == NULL || config == NULL || config->iface_name == NULL) {
		return -EINVAL;
	}

	if (client->ready) {
		return 0;
	}

	client->iface = modbus_iface_get_by_name(config->iface_name);
	if (client->iface < 0) {
		LOG_ERR("WIT IMU Modbus interface not found: %s",
			config->iface_name);
		return client->iface;
	}

	ret = modbus_init_client(client->iface, param);
	if (ret != 0) {
		LOG_ERR("WIT IMU Modbus init failed: iface=%s model=%s err=%d",
			config->iface_name,
			wit_imu_modbus_model_name(config->model), ret);
		return ret;
	}

	client->config = config;
	client->ready = true;

	LOG_INF("WIT IMU Modbus ready: iface=%s model=%s unit=0x%02x baud=%u 8N1 timeout=%u us",
		config->iface_name,
		wit_imu_modbus_model_name(config->model),
		config->unit_id,
		config->baud,
		config->rx_timeout_us);

	return 0;
}

static int wit_imu_modbus_fetch_standard_precision(
	struct wit_imu_modbus_client *client,
	struct wit_imu_modbus_sample *sample)
{
	uint16_t regs[WIT_STANDARD_PRECISION_ANGLE_REG_COUNT];
	int ret;

	ret = modbus_read_holding_regs(client->iface, client->config->unit_id,
				       WIT_ANGLE_REG_START, regs,
				       ARRAY_SIZE(regs));
	if (ret != 0) {
		return ret;
	}

	wit_imu_copy_raw_regs(sample, regs, ARRAY_SIZE(regs));
	sample->roll_raw = (int16_t)regs[0];
	sample->pitch_raw = (int16_t)regs[1];
	sample->yaw_raw = 0;
	sample->roll_mdeg =
		wit_standard_precision_raw_to_mdeg(sample->roll_raw);
	sample->pitch_mdeg =
		wit_standard_precision_raw_to_mdeg(sample->pitch_raw);
	sample->yaw_mdeg = 0;

	return 0;
}

static int wit_imu_modbus_fetch_high_precision(
	struct wit_imu_modbus_client *client,
	struct wit_imu_modbus_sample *sample)
{
	uint16_t regs[WIT_HIGH_PRECISION_ANGLE_REG_COUNT];
	int ret;

	ret = modbus_read_holding_regs(client->iface, client->config->unit_id,
				       WIT_ANGLE_REG_START, regs,
				       ARRAY_SIZE(regs));
	if (ret != 0) {
		return ret;
	}

	wit_imu_copy_raw_regs(sample, regs, ARRAY_SIZE(regs));
	sample->roll_raw = wit_high_precision_make_mdeg(
		regs[WIT_HIGH_PRECISION_LROLL], regs[WIT_HIGH_PRECISION_HROLL]);
	sample->pitch_raw = wit_high_precision_make_mdeg(
		regs[WIT_HIGH_PRECISION_LPITCH],
		regs[WIT_HIGH_PRECISION_HPITCH]);
	sample->yaw_raw = wit_high_precision_make_mdeg(
		regs[WIT_HIGH_PRECISION_LYAW], regs[WIT_HIGH_PRECISION_HYAW]);
	sample->roll_mdeg = sample->roll_raw;
	sample->pitch_mdeg = sample->pitch_raw;
	sample->yaw_mdeg = sample->yaw_raw;

	return 0;
}

int wit_imu_modbus_fetch(struct wit_imu_modbus_client *client,
			 struct wit_imu_modbus_sample *sample)
{
	if (client == NULL || sample == NULL) {
		return -EINVAL;
	}

	if (!client->ready || client->config == NULL || client->iface < 0) {
		return -ENODEV;
	}

	switch (client->config->model) {
	case WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION:
		return wit_imu_modbus_fetch_standard_precision(client, sample);
	case WIT_IMU_MODBUS_MODEL_HIGH_PRECISION:
		return wit_imu_modbus_fetch_high_precision(client, sample);
	default:
		return -EINVAL;
	}
}
