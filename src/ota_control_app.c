/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(ota_control_app, CONFIG_LOG_DEFAULT_LEVEL);

#define PRIMARY_AREA_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition))
#define SECONDARY_AREA_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition))

static struct k_work_delayable unconfirmed_reboot_work;

static const char *swap_type_name(int swap_type)
{
	switch (swap_type) {
	case BOOT_SWAP_TYPE_NONE:
		return "none";
	case BOOT_SWAP_TYPE_TEST:
		return "test";
	case BOOT_SWAP_TYPE_PERM:
		return "permanent";
	case BOOT_SWAP_TYPE_REVERT:
		return "revert";
	case BOOT_SWAP_TYPE_FAIL:
		return "fail";
	default:
		return "unknown";
	}
}

static void unconfirmed_reboot_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!boot_is_img_confirmed()) {
		LOG_WRN("OTA test image was not confirmed in %u second(s), rebooting for rollback",
			CONFIG_CRANER_OTA_TEST_TIMEOUT_S);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

static int print_bank_info(const struct shell *shell, const char *name, uint8_t area_id)
{
	struct mcuboot_img_header header;
	int rc;

	rc = boot_read_bank_header(area_id, &header, sizeof(header));
	if (rc != 0) {
		shell_print(shell, "%s: area=%u header unavailable: %d", name, area_id, rc);
		return rc;
	}

	shell_print(shell, "%s: area=%u version=%u.%u.%u+%u image_size=%u",
		    name, area_id,
		    header.h.v1.sem_ver.major,
		    header.h.v1.sem_ver.minor,
		    header.h.v1.sem_ver.revision,
		    header.h.v1.sem_ver.build_num,
		    header.h.v1.image_size);

	return 0;
}

static int cmd_ota_show(const struct shell *shell, size_t argc, char **argv)
{
	int swap_type;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	swap_type = mcuboot_swap_type();
	shell_print(shell, "active_area=%u", boot_fetch_active_slot());
	shell_print(shell, "confirmed=%s", boot_is_img_confirmed() ? "yes" : "no");
	shell_print(shell, "next_boot_swap=%s (%d)", swap_type_name(swap_type), swap_type);
	shell_print(shell, "test_timeout=%u s", CONFIG_CRANER_OTA_TEST_TIMEOUT_S);

	print_bank_info(shell, "primary", PRIMARY_AREA_ID);
	print_bank_info(shell, "secondary", SECONDARY_AREA_ID);

	return 0;
}

static int cmd_ota_test(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
	if (rc != 0) {
		shell_error(shell, "failed to request test upgrade: %d", rc);
		return rc;
	}

	shell_print(shell, "secondary image marked as test upgrade");
	shell_print(shell, "run: ota reboot");

	return 0;
}

static int cmd_ota_permanent(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
	if (rc != 0) {
		shell_error(shell, "failed to request permanent upgrade: %d", rc);
		return rc;
	}

	shell_print(shell, "secondary image marked as permanent upgrade");
	shell_print(shell, "run: ota reboot");

	return 0;
}

static int cmd_ota_confirm(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = boot_write_img_confirmed();
	if (rc != 0) {
		shell_error(shell, "failed to confirm current image: %d", rc);
		return rc;
	}

	shell_print(shell, "current image confirmed");

	return 0;
}

static int cmd_ota_erase_secondary(const struct shell *shell, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = boot_erase_img_bank(SECONDARY_AREA_ID);
	if (rc != 0) {
		shell_error(shell, "failed to erase secondary image: %d", rc);
		return rc;
	}

	shell_print(shell, "secondary image erased");
	return 0;
}

static int cmd_ota_reboot(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sys_reboot(SYS_REBOOT_COLD);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	ota_cmds,
	SHELL_CMD(show, NULL, "Show MCUboot OTA swap state.", cmd_ota_show),
	SHELL_CMD(test, NULL, "Mark secondary image as test upgrade.", cmd_ota_test),
	SHELL_CMD(permanent, NULL, "Mark secondary image as permanent upgrade.", cmd_ota_permanent),
	SHELL_CMD(confirm, NULL, "Confirm current image as stable.", cmd_ota_confirm),
	SHELL_CMD(erase-secondary, NULL, "Erase secondary image slot.", cmd_ota_erase_secondary),
	SHELL_CMD(reboot, NULL, "Reboot the MCU.", cmd_ota_reboot),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ota, &ota_cmds, "MCUboot swap OTA commands", NULL);

static int ota_control_init(void)
{
	k_work_init_delayable(&unconfirmed_reboot_work, unconfirmed_reboot_handler);

	if (!boot_is_img_confirmed()) {
		LOG_WRN("Running unconfirmed OTA test image, confirm within %u second(s)",
			CONFIG_CRANER_OTA_TEST_TIMEOUT_S);
		k_work_schedule(&unconfirmed_reboot_work,
				K_SECONDS(CONFIG_CRANER_OTA_TEST_TIMEOUT_S));
	}

	return 0;
}

SYS_INIT(ota_control_init, APPLICATION, 95);
