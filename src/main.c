#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	printk("craner_encoder_hub started on mini_stm32h743\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
