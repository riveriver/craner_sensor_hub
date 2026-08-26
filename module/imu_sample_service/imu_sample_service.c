#include "imu_sample_service.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "wit_imu_modbus.h"

LOG_MODULE_REGISTER(imu_sample_service, CONFIG_LOG_DEFAULT_LEVEL);

struct imu_sample_service_callback {
	imu_sample_service_callback_t cb;
	void *user_data;
};

static const struct wit_imu_modbus_config imu_modbus_config = {
	.iface_name = CONFIG_IMU_MODBUS_IFACE_NAME,
#if defined(CONFIG_IMU_MODEL_WIT_HIGH_PRECISION)
	.model = WIT_IMU_MODBUS_MODEL_HIGH_PRECISION,
#else
	.model = WIT_IMU_MODBUS_MODEL_STANDARD_PRECISION,
#endif
	.unit_id = CONFIG_IMU_MODBUS_UNIT_ID,
	.baud = CONFIG_IMU_MODBUS_BAUD,
	.rx_timeout_us = CONFIG_IMU_MODBUS_RX_TIMEOUT_US,
};

static struct wit_imu_modbus_client imu_modbus_client = {
	.iface = -1,
};

static struct imu_sample_service_sample latest_sample;
static struct imu_sample_service_stats latest_stats;
static struct k_spinlock latest_lock;

static K_MUTEX_DEFINE(callbacks_lock);
static struct imu_sample_service_callback callbacks[
	CONFIG_IMU_SAMPLE_SERVICE_MAX_CALLBACKS];

static void imu_sample_service_copy_raw_regs(
	struct imu_sample_service_sample *dst,
	const struct wit_imu_modbus_sample *src)
{
	dst->raw_reg_count = src->raw_reg_count;
	for (uint8_t i = 0U;
	     i < src->raw_reg_count && i < ARRAY_SIZE(dst->raw_regs);
	     i++) {
		dst->raw_regs[i] = src->raw_regs[i];
	}
}

static void imu_sample_service_notify(
	int err, const struct imu_sample_service_sample *sample)
{
	struct imu_sample_service_callback local_callbacks[
		CONFIG_IMU_SAMPLE_SERVICE_MAX_CALLBACKS];

	k_mutex_lock(&callbacks_lock, K_FOREVER);
	memcpy(local_callbacks, callbacks, sizeof(local_callbacks));
	k_mutex_unlock(&callbacks_lock);

	for (size_t i = 0U; i < ARRAY_SIZE(local_callbacks); i++) {
		if (local_callbacks[i].cb != NULL) {
			local_callbacks[i].cb(err, sample,
					      local_callbacks[i].user_data);
		}
	}
}

void imu_sample_service_get_latest(struct imu_sample_service_sample *sample)
{
	k_spinlock_key_t key;

	if (sample == NULL) {
		return;
	}

	key = k_spin_lock(&latest_lock);
	*sample = latest_sample;
	k_spin_unlock(&latest_lock, key);
}

void imu_sample_service_get_stats(struct imu_sample_service_stats *stats)
{
	k_spinlock_key_t key;

	if (stats == NULL) {
		return;
	}

	key = k_spin_lock(&latest_lock);
	*stats = latest_stats;
	k_spin_unlock(&latest_lock, key);
}

int imu_sample_service_register_callback(imu_sample_service_callback_t cb,
					 void *user_data)
{
	if (cb == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&callbacks_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(callbacks); i++) {
		if (callbacks[i].cb == NULL) {
			callbacks[i].cb = cb;
			callbacks[i].user_data = user_data;
			k_mutex_unlock(&callbacks_lock);
			return 0;
		}
	}
	k_mutex_unlock(&callbacks_lock);

	return -ENOMEM;
}

static void imu_sample_service_update_success(
	const struct wit_imu_modbus_sample *sample,
	uint32_t read_duration_us,
	struct imu_sample_service_sample *notify_sample)
{
	k_spinlock_key_t key;
	uint32_t success_count;

	key = k_spin_lock(&latest_lock);

	success_count = latest_stats.success_count + 1U;
	latest_stats.success_count = success_count;
	latest_stats.consecutive_error_count = 0;
	latest_stats.last_error = 0;
	latest_stats.last_success_uptime_ms = k_uptime_get();
	latest_stats.success_total_time_us += read_duration_us;
	latest_stats.success_avg_time_us =
		(uint32_t)(latest_stats.success_total_time_us / success_count);
	if (read_duration_us > latest_stats.success_max_time_us) {
		latest_stats.success_max_time_us = read_duration_us;
	}

	latest_sample.online = true;
	latest_sample.seq++;
	latest_sample.status = IMU_SAMPLE_SERVICE_STATUS_ONLINE;
	imu_sample_service_copy_raw_regs(&latest_sample, sample);
	latest_sample.roll_raw = sample->roll_raw;
	latest_sample.pitch_raw = sample->pitch_raw;
	latest_sample.yaw_raw = sample->yaw_raw;
	latest_sample.roll_mdeg = sample->roll_mdeg;
	latest_sample.pitch_mdeg = sample->pitch_mdeg;
	latest_sample.yaw_mdeg = sample->yaw_mdeg;
	latest_sample.sample_uptime_ms = latest_stats.last_success_uptime_ms;
	latest_sample.read_duration_us = read_duration_us;
	latest_sample.last_error = 0;
	*notify_sample = latest_sample;

	k_spin_unlock(&latest_lock, key);
}

static void imu_sample_service_update_error(int err)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&latest_lock);
	latest_sample.online = false;
	latest_sample.status &= ~IMU_SAMPLE_SERVICE_STATUS_ONLINE;
	latest_sample.last_error = err;
	latest_stats.error_count++;
	latest_stats.consecutive_error_count++;
	latest_stats.last_error = err;
	latest_stats.last_fault_error = err;
	latest_stats.last_error_uptime_ms = k_uptime_get();
	if (latest_stats.consecutive_error_count >
	    latest_stats.max_consecutive_error_count) {
		latest_stats.max_consecutive_error_count =
			latest_stats.consecutive_error_count;
	}
	k_spin_unlock(&latest_lock, key);
}

static void imu_sample_service_thread(void *arg1, void *arg2, void *arg3)
{
	int err;
	bool client_ready = false;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_thread_name_set(k_current_get(), "imu_sample_service");

	while (true) {
		struct wit_imu_modbus_sample sample;
		struct imu_sample_service_sample notify_sample;
		uint32_t start_cycles;
		uint32_t elapsed_cycles;
		uint32_t read_duration_us;

		if (!client_ready) {
			err = wit_imu_modbus_init(&imu_modbus_client,
						  &imu_modbus_config);
			if (err != 0) {
				imu_sample_service_update_error(err);
				imu_sample_service_notify(err, NULL);
				LOG_WRN_RATELIMIT("IMU Modbus init failed: %d",
						   err);
				k_sleep(K_MSEC(CONFIG_IMU_SAMPLE_PERIOD_MS));
				continue;
			}

			client_ready = true;
			LOG_INF("IMU sampling started: model=%s period=%u ms",
				wit_imu_modbus_model_name(imu_modbus_config.model),
				CONFIG_IMU_SAMPLE_PERIOD_MS);
		}

		start_cycles = k_cycle_get_32();
		err = wit_imu_modbus_fetch(&imu_modbus_client, &sample);
		elapsed_cycles = k_cycle_get_32() - start_cycles;
		read_duration_us = (uint32_t)k_cyc_to_us_floor64(elapsed_cycles);

		if (err == 0) {
			imu_sample_service_update_success(&sample, read_duration_us,
							  &notify_sample);
			imu_sample_service_notify(0, &notify_sample);
		} else {
			imu_sample_service_update_error(err);
			imu_sample_service_notify(err, NULL);
			LOG_WRN_RATELIMIT("IMU read failed: %d", err);
			if (err == -ENODEV) {
				imu_modbus_client.ready = false;
				imu_modbus_client.iface = -1;
				client_ready = false;
			}
		}

		k_sleep(K_MSEC(CONFIG_IMU_SAMPLE_PERIOD_MS));
	}
}

static void shell_print_angle_row(const struct shell *shell,
				  const char *axis, int32_t raw,
				  int32_t mdeg)
{
	int64_t deg_value = mdeg;
	const char *sign = "";

	if (deg_value < 0) {
		sign = "-";
		deg_value = -deg_value;
	}

	shell_print(shell, "%-6s %12d %12d %s%lld.%03lld", axis, raw, mdeg,
		    sign, (long long)(deg_value / 1000LL),
		    (long long)(deg_value % 1000LL));
}

static int cmd_imu_status(const struct shell *shell, size_t argc, char **argv)
{
	struct imu_sample_service_sample sample;
	struct imu_sample_service_stats stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	imu_sample_service_get_latest(&sample);
	imu_sample_service_get_stats(&stats);

	shell_print(shell, "model: %s",
		    wit_imu_modbus_model_name(imu_modbus_config.model));
	shell_print(shell, "iface: %s", imu_modbus_config.iface_name);
	shell_print(shell, "unit: 0x%02x", imu_modbus_config.unit_id);
	shell_print(shell, "serial: %u 8N1", imu_modbus_config.baud);
	shell_print(shell, "timeout_us: %u", imu_modbus_config.rx_timeout_us);
	shell_print(shell, "period_ms: %u", CONFIG_IMU_SAMPLE_PERIOD_MS);
	shell_print(shell, "online: %s", sample.online ? "true" : "false");
	shell_print(shell, "seq: %u", sample.seq);
	shell_print(shell, "last_error: %d", sample.last_error);
	shell_print(shell, "consecutive_error_count: %u",
		    stats.consecutive_error_count);

	return 0;
}

static int cmd_imu_sample(const struct shell *shell, size_t argc, char **argv)
{
	struct imu_sample_service_sample sample;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	imu_sample_service_get_latest(&sample);

	shell_print(shell,
		    "online=%s seq=%u uptime_ms=%lld read_us=%u last_error=%d",
		    sample.online ? "yes" : "no", sample.seq,
		    (long long)sample.sample_uptime_ms,
		    sample.read_duration_us, sample.last_error);

	shell_print(shell, "");
	shell_print(shell, "axis          raw32         mdeg          deg");
	shell_print(shell, "---------------------------------------------");
	shell_print_angle_row(shell, "roll", sample.roll_raw, sample.roll_mdeg);
	shell_print_angle_row(shell, "pitch", sample.pitch_raw, sample.pitch_mdeg);
	shell_print_angle_row(shell, "yaw", sample.yaw_raw, sample.yaw_mdeg);

	if (sample.raw_reg_count > 0U) {
		shell_print(shell, "");
		shell_print(shell,
			    "raw_regs count=%u r0=0x%04x r1=0x%04x r2=0x%04x r3=0x%04x r4=0x%04x r5=0x%04x",
			    sample.raw_reg_count,
			    sample.raw_regs[0], sample.raw_regs[1],
			    sample.raw_regs[2], sample.raw_regs[3],
			    sample.raw_regs[4], sample.raw_regs[5]);
	}

	return 0;
}

static int cmd_imu_stats(const struct shell *shell, size_t argc, char **argv)
{
	struct imu_sample_service_stats stats;
	uint32_t total_count;
	uint32_t fail_rate_x100 = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	imu_sample_service_get_stats(&stats);

	total_count = stats.success_count + stats.error_count;
	if (total_count > 0U) {
		fail_rate_x100 =
			(uint32_t)((((uint64_t)stats.error_count * 10000U) +
				    (total_count / 2U)) / total_count);
	}

	shell_print(shell, "name       fail_rate   fail/total      avg_ms   max_ms");
	shell_print(shell, "-------------------------------------------------------");
	shell_print(shell, "%-10s %3u.%02u%% %7u/%-7u %8u %8u",
		    "imu", fail_rate_x100 / 100U, fail_rate_x100 % 100U,
		    stats.error_count, total_count,
		    (stats.success_avg_time_us + 500U) / 1000U,
		    (stats.success_max_time_us + 500U) / 1000U);

	return 0;
}

static int cmd_imu_fault(const struct shell *shell, size_t argc, char **argv)
{
	struct imu_sample_service_stats stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	imu_sample_service_get_stats(&stats);

	shell_print(shell,
		    "last_error_ms   last_fault_error  consecutive_fail  max_consecutive_fail");
	shell_print(shell,
		    "--------------------------------------------------------------------");
	shell_print(shell, "%13lld %16d %17u %21u",
		    (long long)stats.last_error_uptime_ms,
		    stats.last_fault_error,
		    stats.consecutive_error_count,
		    stats.max_consecutive_error_count);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(imu_cmds,
	SHELL_CMD(fault, NULL, "Show IMU fault statistics.", cmd_imu_fault),
	SHELL_CMD(sample, NULL, "Show latest IMU sample.", cmd_imu_sample),
	SHELL_CMD(stats, NULL, "Show IMU sample statistics.", cmd_imu_stats),
	SHELL_CMD(status, NULL, "Show IMU sampling status.", cmd_imu_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(imu, &imu_cmds, "IMU sampling commands.", NULL);

K_THREAD_DEFINE(imu_sample_service_tid,
		CONFIG_IMU_SAMPLE_THREAD_STACK_SIZE,
		imu_sample_service_thread,
		NULL, NULL, NULL,
		CONFIG_IMU_SAMPLE_THREAD_PRIORITY,
		0,
		0);
