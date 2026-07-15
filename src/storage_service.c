#include "storage_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(storage_service, CONFIG_LOG_DEFAULT_LEVEL);

#define COREDUMP_PARTITION_NODE DT_NODELABEL(coredump_partition)
#define APP_STORAGE_PARTITION_NODE DT_NODELABEL(app_storage_partition)

BUILD_ASSERT(DT_NODE_EXISTS(COREDUMP_PARTITION_NODE),
	     "Missing coredump_partition fixed partition");
BUILD_ASSERT(DT_NODE_EXISTS(APP_STORAGE_PARTITION_NODE),
	     "Missing app_storage_partition fixed partition");

#define COREDUMP_AREA_ID DT_FIXED_PARTITION_ID(COREDUMP_PARTITION_NODE)
#define APP_STORAGE_AREA_ID DT_FIXED_PARTITION_ID(APP_STORAGE_PARTITION_NODE)

static struct storage_service_status service_status = {
	.coredump = {
		.name = "coredump-partition",
		.area_id = COREDUMP_AREA_ID,
	},
	.app_storage = {
		.name = "app-storage",
		.area_id = APP_STORAGE_AREA_ID,
	},
};

static int probe_partition(uint8_t area_id, const char *name,
			   struct storage_partition_status *status)
{
	const struct flash_area *area;
	const struct device *dev;
	int rc;

	status->name = name;
	status->area_id = area_id;
	status->available = false;
	status->device_ready = false;
	status->device_name = "";
	status->offset = 0U;
	status->size = 0U;

	rc = flash_area_open(area_id, &area);
	if (rc != 0) {
		status->last_error = rc;
		return rc;
	}

	dev = flash_area_get_device(area);
	status->device_name = dev != NULL ? dev->name : "";
	status->device_ready = dev != NULL && device_is_ready(dev);
	status->offset = (uint32_t)area->fa_off;
	status->size = area->fa_size;
	status->available = status->device_ready;
	status->last_error = status->device_ready ? 0 : -ENODEV;

	flash_area_close(area);

	return status->last_error;
}

int storage_service_init(void)
{
	int coredump_rc;
	int app_storage_rc;

	memset(&service_status, 0, sizeof(service_status));

	coredump_rc = probe_partition(COREDUMP_AREA_ID, "coredump-partition",
				      &service_status.coredump);
	app_storage_rc = probe_partition(APP_STORAGE_AREA_ID, "app-storage",
					 &service_status.app_storage);

	service_status.initialized = true;
	service_status.internal_flash_ready =
		(coredump_rc == 0) && (app_storage_rc == 0);
	service_status.last_error = coredump_rc != 0 ? coredump_rc :
				    app_storage_rc;

	if (service_status.internal_flash_ready) {
		LOG_INF("Storage service ready: coredump=0x%08x/%u, app-storage=0x%08x/%u",
			service_status.coredump.offset,
			(uint32_t)service_status.coredump.size,
			service_status.app_storage.offset,
			(uint32_t)service_status.app_storage.size);
	} else {
		LOG_ERR("Storage service internal flash probe failed: %d",
			service_status.last_error);
	}

	return service_status.last_error;
}

void storage_service_get_status(struct storage_service_status *status)
{
	if (status == NULL) {
		return;
	}

	*status = service_status;
}
