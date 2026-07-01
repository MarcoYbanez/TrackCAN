
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>


/* GPIO */
// #define IN_PIN DT_ALIAS(in)
const struct gpio_dt_spec gp_pin = GPIO_DT_SPEC_GET(DT_PATH(user,out_pin), gpios);
const struct gpio_dt_spec in_pin = GPIO_DT_SPEC_GET(DT_PATH(user,in_pin), gpios);


/* RANDOM GENERATOR */
#define RAND_GEN DT_ALIAS(rand_gen)
const struct device *random_gen = DEVICE_DT_GET(RAND_GEN);

/* ENGINE MANAGEMENT ADC*/
#define ENG_SPI DT_ALIAS(eng_spi)
const struct spi_dt_spec eng_spi = SPI_DT_SPEC_GET(ENG_SPI, SPI_WORD_SET(8) | SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB, 0);
// const struct spi_dt_spec *engine_management_adc = SPI_DT_SPEC_GET(ENG_SPI);
//  const struct device *random_gen = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));

int main(void)
{

		struct spi_buf_set rx_bufs = {
				.buffers = (struct spi_buf[]){
						{.buf =  (void*)(char[]){
								0b00000000,
								0b00000000,
								0b00000000
						},
								.len = 3},
						{.buf =  (void*)(char[]){
								0b00000000,
								0b00000000,
								0b00000000
						}, .len = 3},
						{.buf =  (void*)(char[]){
								0b00000000,
								0b00000000,
								0b00000000
						}, .len = 3},
				},
				.count = 3
		};

		struct spi_buf_set tx_bufs = {
				.buffers = (struct spi_buf[]){
						{.buf =  (void*)(char[]){
								0b00000110, 
								0b01000000,
								0b00000000
						},
								.len = 3},
						{.buf =  (void*)(char[]){
								0b00000110,
								0b10000000,
								0b00000000
						}, .len = 3},
						{.buf =  (void*)(char[]){
								0b00000110,
								0b11000000,
								0b00000000
						}, .len = 3},
				},
				.count = 3
		};

		if(!gpio_is_ready_dt(&gp_pin))
		{
        printf("GPIO GP BAD");
		}
		else {
        printf("GPIO ENG GP GOOOD\n");
		}

		if(!gpio_is_ready_dt(&in_pin))
		{
        printf("GPIO IN BAD");
		}
		else {
        printf("GPIO ENG GP GOOOD\n");
		}

		if(!spi_is_ready_dt(&eng_spi ))
		{
        printf("GPIO ENG SPI BAD");
		}
		else {
        printf("GPIO ENG SPI GOOOD\n");
		}

		printf("GPIO READY\n");

		gpio_pin_configure_dt(&gp_pin, GPIO_OUTPUT_ACTIVE);
		gpio_pin_configure_dt(&gp_pin, GPIO_INPUT);

    while (1) {
        uint32_t x = 100;
        int ret = entropy_get_entropy(random_gen, (uint8_t *)&x, sizeof(x));
        // int ret = entropy_get_entropy(random_gen, (uint8_t *)&x, sizeof(x));

        printf("true random number: %u\n", (uint32_t)x);
        printf("PIN READ: %u\n", (uint32_t)gpio_pin_get_raw(in_pin.port, in_pin.pin));
        printf("PIN OUT: %u\n", (uint32_t)gpio_pin_get_dt(&gp_pin));

				spi_transceive_dt(&eng_spi, &tx_bufs, &rx_bufs);

				for(int i = 0 ; i  < 3; ++i)
				{
						char* res = (char *)tx_bufs.buffers[i].buf;
						
								uint32_t channel_value = 0;
								channel_value |= (res[1] << 8);
								channel_value |= res[2];
								printf("SPI_READ -- CH %d: %d\n", i, channel_value );
				}

        k_msleep(900);
    }

    return 0;
}
