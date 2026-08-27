#include "anemometer_sample_service_internal.h"

#include <errno.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>

static sys_slist_t service_list = SYS_SLIST_STATIC_INIT(&service_list);
static K_MUTEX_DEFINE(service_list_lock);

struct anemometer_sample_service_node {
	sys_snode_t node;
	struct anemometer_sample_service *service;
};

static struct anemometer_sample_service_node service_nodes[
	CONFIG_ANEMOMETER_SAMPLE_SERVICE_MAX_INSTANCES];

int anemometer_sample_service_register_shell_instance(
	struct anemometer_sample_service *service)
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

static void anemometer_sample_service_print_stats(
	const struct shell *shell,
	struct anemometer_sample_service *service)
{
	struct anemometer_sample_service_stats stats;
	uint32_t total_count;
	uint32_t fail_rate_x100 = 0U;

	anemometer_sample_service_get_stats(service, &stats);

	total_count = stats.success_count + stats.error_count;
	if (total_count > 0U) {
		fail_rate_x100 =
			(uint32_t)((((uint64_t)stats.error_count * 10000U) +
				    (total_count / 2U)) / total_count);
	}

	shell_print(shell, "%-12s %3u.%02u%% %7u/%-7u %8u %8u %d",
		    anemometer_sample_service_name(service),
		    fail_rate_x100 / 100U, fail_rate_x100 % 100U,
		    stats.error_count, total_count,
		    (stats.success_avg_time_us + 500U) / 1000U,
		    (stats.success_max_time_us + 500U) / 1000U,
		    stats.last_fault_error);
}

static int cmd_anemometer_stats(const struct shell *shell, size_t argc,
				char **argv)
{
	sys_snode_t *snode;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell,
		    "name         fail_rate   fail/total      avg_ms   max_ms last_fault");
	shell_print(shell,
		    "------------------------------------------------------------------");

	k_mutex_lock(&service_list_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_NODE(&service_list, snode) {
		struct anemometer_sample_service_node *node =
			CONTAINER_OF(snode,
				     struct anemometer_sample_service_node,
				     node);

		anemometer_sample_service_print_stats(shell, node->service);
	}
	k_mutex_unlock(&service_list_lock);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(anemometer_cmds,
	SHELL_CMD(stats, NULL, "Show anemometer sample statistics.",
		  cmd_anemometer_stats),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(anemometer, &anemometer_cmds,
		   "Anemometer sample service commands.", NULL);
