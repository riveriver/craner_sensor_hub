#include "device_identity_service.h"
#ifdef CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE
#include "device_param_store.h"
#endif
#ifdef CONFIG_CRANER_ENABLE_COREDUMP_SERVICE
#include "coredump_service.h"
#endif
#include "network_service.h"
#ifdef CONFIG_CRANER_ENABLE_STORAGE_SERVICE
#include "storage_service.h"
#endif
#include "time_service.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>

int main(void)
{
	int rc;

	printf("craner_encoder_hub started on %s\n", CONFIG_BOARD);
	printk("printk remains routed to the board console UART\n");
	printk("Shell backend is Telnet on port 23 after Ethernet is up\n");
	printk("Zephyr LOG backend is UART and MQTT\n");

	rc = device_identity_service_init();
	if (rc != 0) {
		printk("Device identity init failed: %d\n", rc);
	} else {
		printk("Device hostname: %s\n", device_identity_hostname_get());
		printk("Device mDNS name: %s\n", device_identity_mdns_name_get());
		printk("MQTT client id: %s\n", device_identity_mqtt_client_id_get());
	}

#ifdef CONFIG_CRANER_ENABLE_STORAGE_SERVICE
	rc = storage_service_init();
	if (rc != 0) {
		printk("Storage service init failed: %d\n", rc);
	}
#endif

#ifdef CONFIG_CRANER_ENABLE_DEVICE_PARAM_STORE
	rc = device_param_store_init();
	if (rc != 0) {
		printk("Device parameter store init failed: %d\n", rc);
	}
#endif

#ifdef CONFIG_CRANER_ENABLE_COREDUMP_SERVICE
	rc = coredump_service_init();
	if (rc != 0) {
		printk("Coredump service init failed: %d\n", rc);
	}
#endif

	rc = network_service_init();
	if (rc != 0) {
		printk("Network service init failed: %d\n", rc);
	}

	rc = time_service_init();
	if (rc != 0) {
		printk("Time service init failed: %d\n", rc);
	}

	rc = network_service_start();
	if (rc != 0) {
		printk("Network startup did not get DHCP yet: %d\n", rc);
	}

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
