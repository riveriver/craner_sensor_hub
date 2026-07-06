#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>

int main(void)
{
	printf("craner_encoder_hub started on %s\n", CONFIG_BOARD);
	printk("printk is routed to the board console UART\n");
	printk("Type 'fw_time' in shell to show firmware build time\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
