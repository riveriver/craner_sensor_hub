#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>

int main(void)
{
	printf("craner_encoder_hub started on craner_general_board_v110\n");
	printk("printk is also routed to UART5 PB6/PB5\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
