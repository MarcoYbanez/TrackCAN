
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>

#define RAND_GEN DT_ALIAS(randdd)
#if !DT_NODE_HAS_STATUS(RAND_GEN, okay)

#error "randdd alias does not point to an okay device"

#endif

const struct device *random_gen = DEVICE_DT_GET(RAND_GEN);
// const struct device *random_gen = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
int main(void)
{
    while (1) {
        uint32_t x = 100;
        int ret = entropy_get_entropy(random_gen, (uint8_t *)&x, sizeof(x));
        // int ret = entropy_get_entropy(random_gen, (uint8_t *)&x, sizeof(x));

        printf("true random number: %u\n", (uint32_t)x);
        k_msleep(500);
    }

    return 0;
}
