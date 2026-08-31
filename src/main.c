#include <zephyr/kernel.h>

int main(void)
{
	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
