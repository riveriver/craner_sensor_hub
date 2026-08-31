#if defined(CONFIG_MODBUS_DATA_MODEL_STORE) && \
	defined(CONFIG_MODBUS_DATA_MODEL_STORE_FLASH)
#include <zephyr/storage/flash_map.h>

#include "modbus_data_model_store.h"
#include "modbus_data_model_store_flash.h"
#endif
#ifdef CONFIG_ENABLE_STORAGE_SERVICE
#include "storage_service.h"
#endif
#include "shell_app.h"
#ifdef CONFIG_SYS_HEALTH_SERVICE
#include "system_health_app.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_MODBUS_DATA_MODEL_STORE) && \
	defined(CONFIG_MODBUS_DATA_MODEL_STORE_FLASH)
#define MODBUS_DATA_MODEL_STORE_AREA_ID \
	DT_FIXED_PARTITION_ID(DT_NODELABEL(modbus_store_partition))

static struct modbus_data_model_store_backend modbus_data_model_store_backend;

static int app_modbus_data_model_store_init(void)
{
	const struct modbus_data_model_flash_store_config flash_config = {
		.flash_area_id = MODBUS_DATA_MODEL_STORE_AREA_ID,
	};
	const struct modbus_data_model_store_config store_config = {
		.backend = &modbus_data_model_store_backend,
		.save_delay_ms =
			CONFIG_MODBUS_DATA_MODEL_STORE_DEFAULT_SAVE_DELAY_MS,
	};
	int rc;

	rc = modbus_data_model_flash_store_backend_init(
		&flash_config, &modbus_data_model_store_backend);
	if (rc != 0) {
		return rc;
	}

	return modbus_data_model_store_init(&store_config);
}
#endif

int main(void)
{
	int rc;

#ifdef CONFIG_ENABLE_STORAGE_SERVICE
	rc = storage_service_init();
	if (rc != 0) {
		printk("Storage service init failed: %d\n", rc);
	}
#endif

	rc = shell_app_init();
	if (rc != 0) {
		printk("Shell app init failed: %d\n", rc);
	}

#if defined(CONFIG_MODBUS_DATA_MODEL_STORE) && \
	defined(CONFIG_MODBUS_DATA_MODEL_STORE_FLASH)
	rc = app_modbus_data_model_store_init();
	if (rc != 0) {
		printk("Modbus data model store init failed: %d\n", rc);
	}
#endif

#ifdef CONFIG_SYS_HEALTH_SERVICE
	static const struct sys_health_time_provider health_time_provider = {
		.get_unix_time_s = system_health_app_get_unix_time_s,
	};

	rc = sys_health_init(system_health_app_event_table,
					system_health_app_event_table_size,
					&health_time_provider);
	if (rc != 0) {
		printk("System health service init failed: %d\n", rc);
	}
#endif

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
