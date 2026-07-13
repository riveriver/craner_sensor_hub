#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>

int main(void)
{
	printf("craner_encoder_hub started on %s\n", CONFIG_BOARD);
	printk("printk remains routed to the board console UART\n");
	printk("Shell backend is Telnet on port 23 after Ethernet is up\n");
	printk("Zephyr LOG backend is UART and syslog UDP 192.168.18.4:5514\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
