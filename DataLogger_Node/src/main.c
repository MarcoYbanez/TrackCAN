
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/spi.h>

#define RAND_GEN DT_ALIAS(rand_gen)
#define RAND_GEN DT_ALIAS(rand_gen)
#define ENG_SPI DT_ALIAS(eng_spi)

const struct device *random_gen = DEVICE_DT_GET(RAND_GEN);
// const struct spi_dt_spec *engine_management_adc = SPI_DT_SPEC_GET(ENG_SPI);
//  const struct device *random_gen = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
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
