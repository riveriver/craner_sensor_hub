#include "imu_sample_service_internal.h"

#include <errno.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>

static sys_slist_t service_list = SYS_SLIST_STATIC_INIT(&service_list);
static K_MUTEX_DEFINE(service_list_lock);

struct imu_sample_service_node {
	sys_snode_t node;
	struct imu_sample_service *service;
};

static struct imu_sample_service_node service_nodes[
	CONFIG_IMU_SAMPLE_SERVICE_MAX_INSTANCES];

int imu_sample_service_register_shell_instance(
	struct imu_sample_service *service)
{
	k_mutex_lock(&service_list_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(service_nodes); i++) {
		if (service_nodes[i].service == NULL) {
			service_nodes[i].service = service;
			sys_slist_append(&service_list, &service_nodes[i].node);
			k_mutex_unlock(&service_list_lock);
			return 0;
		}
	}
	k_mutex_unlock(&service_list_lock);

	return -ENOMEM;
}

static void shell_print_angle_row(const struct shell *shell, const char *axis,
				  int32_t raw, int32_t mdeg)
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

static void imu_sample_service_print_stats(
	const struct shell *shell, struct imu_sample_service *service)
{
	struct imu_sample_service_stats stats;
	uint32_t total_count;
	uint32_t fail_rate_x100 = 0U;

	imu_sample_service_get_stats(service, &stats);

	total_count = stats.success_count + stats.error_count;
	if (total_count > 0U) {
		fail_rate_x100 =
			(uint32_t)((((uint64_t)stats.error_count * 10000U) +
				    (total_count / 2U)) / total_count);
	}

	shell_print(shell, "%-14s %3u.%02u%% %7u/%-7u %8u %8u %d",
		    imu_sample_service_name(service),
		    fail_rate_x100 / 100U, fail_rate_x100 % 100U,
		    stats.error_count, total_count,
		    (stats.success_avg_time_us + 500U) / 1000U,
		    (stats.success_max_time_us + 500U) / 1000U,
		    stats.last_fault_error);
}

static int cmd_imu_stats(const struct shell *shell, size_t argc, char **argv)
{
	sys_snode_t *snode;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    "name           fail_rate   fail/total      avg_ms   max_ms last_fault");
	shell_print(shell,
		    "--------------------------------------------------------------------");

	k_mutex_lock(&service_list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_NODE(&service_list, snode) {
		struct imu_sample_service_node *node =
			CONTAINER_OF(snode, struct imu_sample_service_node,
				     node);

		imu_sample_service_print_stats(shell, node->service);
	}
	k_mutex_unlock(&service_list_lock);

	return 0;
}

static int cmd_imu_sample(const struct shell *shell, size_t argc, char **argv)
{
	sys_snode_t *snode;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&service_list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_NODE(&service_list, snode) {
		struct imu_sample_service_node *node =
			CONTAINER_OF(snode, struct imu_sample_service_node,
				     node);
		struct imu_sample_service_sample sample;

		imu_sample_service_get_latest(node->service, &sample);
		shell_print(shell,
			    "%s online=%s seq=%u uptime_ms=%lld read_us=%u last_error=%d",
			    imu_sample_service_name(node->service),
			    sample.online ? "yes" : "no", sample.seq,
			    (long long)sample.sample_uptime_ms,
			    sample.read_duration_us, sample.last_error);

		shell_print(shell, "axis          raw32         mdeg          deg");
		shell_print(shell, "---------------------------------------------");
		shell_print_angle_row(shell, "roll", sample.roll_raw,
				      sample.roll_mdeg);
		shell_print_angle_row(shell, "pitch", sample.pitch_raw,
				      sample.pitch_mdeg);
		shell_print_angle_row(shell, "yaw", sample.yaw_raw,
				      sample.yaw_mdeg);
	}
	k_mutex_unlock(&service_list_lock);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(imu_cmds,
	SHELL_CMD(sample, NULL, "Show latest IMU samples.", cmd_imu_sample),
	SHELL_CMD(stats, NULL, "Show IMU sample statistics.", cmd_imu_stats),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(imu, &imu_cmds, "IMU sampling commands.", NULL);
