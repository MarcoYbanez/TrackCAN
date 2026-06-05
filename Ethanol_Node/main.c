#include "FreeRTOS.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "task.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#define PWM_INPUT_PIN 26
#define E_DAC_ADDR 0x5F
#define E_I2C_PORT i2c1
#define E_SCL 3
#define E_SDA 2
#define E_DAC_MAX 4095
#define E_FUEL_CONTENT 0x02
#define E_FUEL_TEMP 0x04
#define E_DAC_OP_RANGE 3275 // ADC = 12b | .5V = 410, 4.5V = 3685 ==> 3685 - 410 = 3275
#define E_DAC_START_OFFSET 410

static volatile uint32_t l_start = {0};
static volatile uint32_t l_stop = {0};
static volatile uint32_t l_period = {0};

static volatile float l_pulse_width = {0};
static atomic_uint l_ethanol_content = ATOMIC_VAR_INIT(0);

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationTickHook(void)
{ /* Leave empty for now. */
}

void MeasurePwmSignal(uint gpio, uint32_t event_mask)
{
    l_start = time_us_32();
    if (event_mask & GPIO_IRQ_EDGE_RISE) {
        l_period = l_start - l_stop;
        l_stop = l_start;
    }
    if (event_mask & GPIO_IRQ_EDGE_FALL) {
        l_pulse_width = ((41.25f * ((l_start - l_stop) / 1000.0f)) - 81.25);
    }
}

static void ethanolContentReader(void *params)
{
    (void)params;

    gpio_init(PWM_INPUT_PIN);
    gpio_set_dir(PWM_INPUT_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(PWM_INPUT_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true,
                                       &MeasurePwmSignal);
    // gpio_pull_down(PWM_INPUT_PIN);
    gpio_disable_pulls(PWM_INPUT_PIN);

    while (1) {
        /* INFO: write l_ethanol_content*/
        int val_t = round((1000000.0f / l_period) - 50);
        val_t = val_t >= 0 && val_t <= 150 ? val_t : 0;
        atomic_store(&l_ethanol_content, val_t);

        printf("temp: %f C*\n", l_pulse_width);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void ethanolContentDAC(void *params)
{
    (void)params;
    i2c_init(E_I2C_PORT, 100 * 1000);
    gpio_set_function(E_SCL, GPIO_FUNC_I2C);
    gpio_set_function(E_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(E_SDA);
    gpio_pull_up(E_SCL);

    uint8_t enable_5V[2] = {0x01, 0x00};
    uint8_t content_buf[3] = {0};
    content_buf[0] = E_FUEL_CONTENT;
    // Set 5V
    int ret = i2c_write_blocking(E_I2C_PORT, E_DAC_ADDR, enable_5V, sizeof(enable_5V), false);

    // Set
    while (1) {
        uint32_t e_content = atomic_load(&l_ethanol_content);

        uint16_t dac_val =
            (uint16_t)round(e_content / 100.0f * E_DAC_OP_RANGE + E_DAC_START_OFFSET);
        if (dac_val < E_DAC_START_OFFSET) {
            dac_val = E_DAC_START_OFFSET;
        }

        if (dac_val > E_DAC_OP_RANGE + E_DAC_START_OFFSET) {
            dac_val = E_DAC_OP_RANGE + E_DAC_START_OFFSET;
        }

        content_buf[1] = (uint16_t)(dac_val & 0x0F) << 4;
        content_buf[2] = (uint16_t)(dac_val >> 4) & 0xFF;

        ret = i2c_write_blocking(E_I2C_PORT, E_DAC_ADDR, content_buf, sizeof(content_buf), false);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*
 * TODO: set default values to set 0v out and 5v max... set memory?
 */

int main(void)
{
    stdio_init_all();

    xTaskCreate(ethanolContentReader, "ethanol_reader", 256, NULL, 1, NULL);
    xTaskCreate(ethanolContentDAC, "ethanol_DAC", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {
    }
}
