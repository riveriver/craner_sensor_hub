#ifdef CONFIG_ENABLE_COREDUMP_SERVICE
#include "coredump_service.h"
#endif
#ifdef CONFIG_MODBUS_REGISTER_STORE
#include "modbus_register_store.h"
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

#ifdef CONFIG_MODBUS_REGISTER_STORE
	rc = modbus_register_store_init();
	if (rc != 0) {
		printk("Modbus register store init failed: %d\n", rc);
	}
#endif

#ifdef CONFIG_ENABLE_COREDUMP_SERVICE
	rc = coredump_service_init();
	if (rc != 0) {
		printk("Coredump service init failed: %d\n", rc);
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
