#ifndef STORAGE_SERVICE_H_
#define STORAGE_SERVICE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct storage_partition_status {
	const char *name;
	const char *device_name;
	uint8_t area_id;
	uint32_t offset;
	size_t size;
	bool available;
	bool device_ready;
	int last_error;
};

struct storage_service_status {
	bool initialized;
	bool internal_flash_ready;
	int last_error;
	struct storage_partition_status coredump;
	struct storage_partition_status app_storage;
};

int storage_service_init(void);
void storage_service_get_status(struct storage_service_status *status);

#endif /* STORAGE_SERVICE_H_ */
