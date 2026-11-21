/*******************************************************
 * @file main.c
 * @brief Ethanol Node: flex-fuel sensor PWM in, 0-5 V ethanol-content signal to the ECU out.
 *
 * All decisions live in safety/ (hardware independent, host tested). This file only connects
 * them to the hardware: edge capture, the DAC, the optional ADC readback, the watchdog, the
 * scratch-register record and the status LED.
 *
 * Boot order (main): reset cause and scratch record first, DAC with bounded I2C writes, the first
 * output written BEFORE the scheduler starts, then the watchdog, then the tasks.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "FreeRTOS.h"
#include "ethanol.h"
#include "ethanol_boot.h"
#include "ethanol_config.h"
#include "ethanol_dac_math.h"
#include "ethanol_health.h"
#include "ethanol_safety.h"
#include "ethanol_scratch.h"
#include "ethanol_signal.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "task.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*******************************************************
 * Macros
 *******************************************************/
#define PWM_INPUT_PIN 26
#define LED_PIN 13 /* Feather red LED (Zephyr board file, GPIO13) */

#define E_DAC_ADDR 0x5F
#define E_I2C_PORT i2c1
#define E_SCL 3
#define E_SDA 2
#define E_RANGE_REG 0x01
#define E_RANGE_5V 0x00 /* DFRobot GP8403 library: 0x00 = 0-5 V, 0x11 = 0-10 V */
#define E_CH0_REG 0x02

#define E_TASK_STACK_WORDS 512
#define E_PRIO_CAN 1
#define E_PRIO_SIGNAL 2
#define E_PRIO_MONITOR 3

#define EDGE_HISTORY 7u /* the oldest entry is 6 steps (300 ms) back: the edge-count window */

/*******************************************************
 * Static Variables
 *******************************************************/
/* ---- Edge capture (written by the GPIO interrupt) ---- */
static volatile uint64_t l_last_rise_us; /* 64-bit: the age cannot wrap into a false "alive" */
static volatile bool l_have_rise;
static volatile uint32_t l_periods_us[ETH_SIG_PERIODS];
static volatile uint8_t l_period_head;
static volatile uint8_t l_period_count;
static volatile uint32_t l_rises;
static volatile uint32_t l_pulse_us;
static volatile bool l_pulse_valid;

/* ---- Shared between tasks, always accessed inside a critical section ---- */
static eth_safety_out_t l_out;
static struct {
    bool i2c_ok;
    bool readback_valid;
    uint16_t err_x10;
    bool overvolt;
} l_dac_status = {.i2c_ok = true};

static eth_health_t l_health;
static eth_safety_t l_safety;

/* ---- Reset information and the last good value for the fatal-error hooks ---- */
static uint8_t l_reset_count;
static volatile bool l_last_good_valid;
static volatile uint16_t l_last_good_x10;

/*******************************************************
 * Static Function Definitions: scratch record and fatal errors
 *******************************************************/
static void scratch_store(uint16_t pct_x10, uint8_t reason)
{
    eth_scratch_record_t rec = {pct_x10, reason, l_reset_count};
    uint32_t w[ETH_SCRATCH_WORDS];

    eth_scratch_pack(&rec, w);
    for (unsigned i = 0; i < ETH_SCRATCH_WORDS; i++) {
        watchdog_hw->scratch[i] = w[i]; /* registers 0-3 only: 4-7 belong to the SDK/bootrom */
    }
}

static bool scratch_load(eth_scratch_record_t *rec)
{
    uint32_t w[ETH_SCRATCH_WORDS];

    for (unsigned i = 0; i < ETH_SCRATCH_WORDS; i++) {
        w[i] = watchdog_hw->scratch[i];
    }
    return eth_scratch_unpack(w, rec);
}

/* Record why, then stop and let the enabled watchdog time out (at most ETH_WATCHDOG_MS). Not
 * watchdog_reboot(): that clears the SDK marker, and the next boot could not tell a real fault from
 * a flash. The DAC keeps its last value meanwhile, and the new boot restores the last good value,
 * so no step to a default is ever sent to the ECU. */
static void fatal_reboot(uint8_t reason)
{
    taskDISABLE_INTERRUPTS();
    if (l_last_good_valid) {
        scratch_store(l_last_good_x10, reason);
    }
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    fatal_reboot(ETH_REASON_STACK_OVERFLOW);
}

void vApplicationMallocFailedHook(void)
{
    fatal_reboot(ETH_REASON_MALLOC_FAILED);
}

void vApplicationTickHook(void)
{ /* Leave empty. */
}

/*******************************************************
 * Static Function Definitions: sensor edge capture
 *******************************************************/
static uint32_t saturate_u32(uint64_t v)
{
    return (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)v;
}

static void edge_irq(uint gpio, uint32_t events)
{
    uint64_t now = time_us_64();

    (void)gpio;
    if (events & GPIO_IRQ_EDGE_RISE) {
        if (l_have_rise) {
            uint32_t period = saturate_u32(now - l_last_rise_us);

            if (period > ETH_LOSS_US) {
                l_period_count = 0; /* a gap: start the history again */
                l_period_head = 0;
            } else {
                l_periods_us[l_period_head] = period;
                l_period_head = (uint8_t)((l_period_head + 1u) % ETH_SIG_PERIODS);
                if (l_period_count < ETH_SIG_PERIODS) {
                    l_period_count++;
                }
                l_rises++;
            }
        }
        l_last_rise_us = now;
        l_have_rise = true;
    }
    if ((events & GPIO_IRQ_EDGE_FALL) && l_have_rise) {
        l_pulse_us = saturate_u32(now - l_last_rise_us);
        l_pulse_valid = true;
    }
}

typedef struct {
    uint32_t periods_us[ETH_SIG_PERIODS];
    uint8_t n;
    uint32_t age_us;
    uint32_t rises;
    uint32_t now_us;
    uint32_t pulse_us;
    bool pulse_valid;
} edge_snapshot_t;

static void snapshot_edges(edge_snapshot_t *s)
{
    taskENTER_CRITICAL();
    uint64_t now64 = time_us_64();

    s->now_us = (uint32_t)now64;
    s->n = l_period_count;
    for (unsigned i = 0; i < ETH_SIG_PERIODS; i++) {
        s->periods_us[i] = l_periods_us[i];
    }
    s->age_us = l_have_rise ? saturate_u32(now64 - l_last_rise_us) : 0xFFFFFFFFu;
    s->rises = l_rises;
    s->pulse_us = l_pulse_us;
    s->pulse_valid = l_pulse_valid;
    taskEXIT_CRITICAL();
}

/*******************************************************
 * Static Function Definitions: DAC and readback
 *******************************************************/
static bool dac_write(const uint8_t *buf, size_t len)
{
    return i2c_write_timeout_us(E_I2C_PORT, E_DAC_ADDR, buf, len, false, ETH_I2C_TIMEOUT_US) ==
           (int)len;
}

static bool dac_write_range(void)
{
    const uint8_t buf[2] = {E_RANGE_REG, E_RANGE_5V};

    return dac_write(buf, sizeof(buf));
}

/* GP8403: 12-bit value left-justified in 16 bits, low byte first. */
static bool dac_write_code(uint16_t code)
{
    const uint8_t buf[3] = {E_CH0_REG, (uint8_t)((code & 0x0Fu) << 4),
                            (uint8_t)((code >> 4) & 0xFFu)};

    return dac_write(buf, sizeof(buf));
}

static void dac_hw_init(void)
{
    i2c_init(E_I2C_PORT, 100 * 1000);
    gpio_set_function(E_SCL, GPIO_FUNC_I2C);
    gpio_set_function(E_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(E_SDA);
    gpio_pull_up(E_SCL);
}

/*******************************************************
 * Static Function Definitions: tasks
 *******************************************************/
static void led_update(const eth_safety_out_t *o, uint32_t tick)
{
    bool on;

    switch (o->state) {
    case ETH_STATE_OK:
        on = true; /* solid: all good */
        break;
    case ETH_STATE_CRITICAL:
        on = (tick & 1u) != 0u; /* 10 Hz strobe */
        break;
    case ETH_STATE_HOLDING:
        on = ((tick / 5u) & 1u) != 0u; /* 2 Hz */
        break;
    default:
        on = ((tick / 10u) & 1u) != 0u; /* 1 Hz while booting or qualifying */
        break;
    }
    gpio_put(LED_PIN, on);
}

static void publish_status(const eth_safety_out_t *o, int16_t temp_x10)
{
    ethanol_can_status_t st;

    st.content_pct = (uint8_t)((o->output_pct_x10 + 5u) / 10u);
    st.fuel_temp_x10 = temp_x10;
    st.state = (uint8_t)o->state;
    st.critical = o->critical;
    st.output_is_default = o->output_is_default;
    st.signal_valid = o->signal_valid;
    st.dac_warning = o->dac_warning;
    st.reset_recent = o->reset_recent;
    st.fault_code = (uint8_t)o->fault;
    ethanol_can_set_status(&st);
}

/* Reads the sensor every 50 ms, runs the safety logic, publishes the result. */
static void signal_task(void *params)
{
    uint32_t hist_rises[EDGE_HISTORY];
    uint32_t hist_us[EDGE_HISTORY];
    unsigned hist_n = 0;
    unsigned hist_i = 0;
    uint32_t tick = 0;
    uint16_t stored_good = 0xFFFFu;
    TickType_t last_wake = xTaskGetTickCount();

    (void)params;

    for (;;) {
        edge_snapshot_t snap;
        eth_sig_input_t in;
        eth_sig_result_t res;
        eth_safety_in_t sin;
        eth_safety_out_t out;
        int16_t temp_x10 = (int16_t)ETH_TEMP_INVALID_X10;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint16_t good;

        snapshot_edges(&snap);

        /* Edge count over the last EDGE_HISTORY steps (about 300 ms). */
        if (hist_n < EDGE_HISTORY) {
            hist_rises[hist_n] = snap.rises;
            hist_us[hist_n] = snap.now_us;
            hist_n++;
        } else {
            hist_rises[hist_i] = snap.rises;
            hist_us[hist_i] = snap.now_us;
            hist_i = (hist_i + 1u) % EDGE_HISTORY;
        }

        memset(&in, 0, sizeof(in));
        memcpy(in.periods_us, snap.periods_us, sizeof(in.periods_us));
        in.n = snap.n;
        in.age_us = snap.age_us;
        if (hist_n == EDGE_HISTORY) {
            unsigned oldest = hist_i; /* the next slot to overwrite holds the oldest entry */
            in.gate_us = (uint32_t)(snap.now_us - hist_us[oldest]);
            in.rises_in_gate = (uint32_t)(snap.rises - hist_rises[oldest]);
        }
        res = ethanol_signal_evaluate(&in);

        /* Fuel temperature: only while the signal is alive and the pulse width is plausible. */
        if (res.status != ETH_SIG_LOST && snap.pulse_valid) {
            int16_t t;

            if (ethanol_temp_from_pulse_us(snap.pulse_us, &t) == 0) {
                temp_x10 = t;
            }
        }

        memset(&sin, 0, sizeof(sin));
        sin.now_ms = now_ms;
        sin.sig_ok = (res.status == ETH_SIG_OK);
        sin.sig_pct_x10 = res.pct_x10;
        taskENTER_CRITICAL();
        sin.dac_i2c_ok = l_dac_status.i2c_ok;
        sin.readback_valid = l_dac_status.readback_valid;
        sin.readback_err_x10 = l_dac_status.err_x10;
        sin.overvoltage = l_dac_status.overvolt;
        taskEXIT_CRITICAL();

        eth_safety_step(&l_safety, &sin, &out);

        taskENTER_CRITICAL();
        l_out = out;
        taskEXIT_CRITICAL();
        publish_status(&out, temp_x10);

        /* Keep the last good value where a watchdog reset can find it. */
        if (eth_safety_last_good(&l_safety, &good)) {
            l_last_good_x10 = good;
            l_last_good_valid = true;
            if (good != stored_good) {
                scratch_store(good, ETH_REASON_NONE);
                stored_good = good;
            }
        }

        led_update(&out, tick++);
        eth_health_checkin(&l_health, ETH_ENTITY_READER);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ETH_READER_PERIOD_MS));
    }
}

/* Writes the output to the DAC every 100 ms and (when wired) reads it back. */
static void dac_task(void *params)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_range_ms = to_ms_since_boot(get_absolute_time());
    unsigned fails = 0;
    bool need_range = false;

    (void)params;

    for (;;) {
        eth_safety_out_t out;
        uint16_t code;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        bool ok = true;
        bool rb_valid = false;
        uint16_t err_x10 = 0;
        bool overvolt = false;

        taskENTER_CRITICAL();
        out = l_out;
        taskEXIT_CRITICAL();

        code = eth_dac_code_from_pct_x10(out.output_pct_x10);

        if (need_range || (uint32_t)(now_ms - last_range_ms) >= ETH_DAC_RANGE_REFRESH_MS) {
            ok = dac_write_range();
            last_range_ms = now_ms;
            need_range = !ok;
        }
        ok = dac_write_code(code) && ok;

        if (ok) {
            fails = 0;
        } else {
            need_range = true; /* re-assert the range after any failure */
            if (++fails >= 3u) {
                i2c_deinit(E_I2C_PORT); /* try to free a stuck bus; the next writes retry */
                dac_hw_init();
                fails = 0;
            }
        }

#if ETH_DAC_READBACK_ENABLED
        if (ok) {
            uint32_t mv;
            uint32_t cmd_mv = eth_dac_code_to_mv(code);

            vTaskDelay(1); /* let the output settle after the write */
            mv = eth_readback_mv(adc_read());
            err_x10 = eth_mismatch_x10(mv, cmd_mv);
            overvolt = mv > ETH_DAC_OVERVOLT_MV;
            rb_valid = true;
        }
#endif

        taskENTER_CRITICAL();
        l_dac_status.i2c_ok = ok;
        l_dac_status.readback_valid = rb_valid;
        l_dac_status.err_x10 = err_x10;
        l_dac_status.overvolt = overvolt;
        taskEXIT_CRITICAL();

        eth_health_checkin(&l_health, ETH_ENTITY_DAC);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ETH_DAC_PERIOD_MS));
    }
}

/* Alive supervision. Feeds the hardware watchdog only while both critical tasks are healthy. Per
 * AUTOSAR WdgM 7.3.3, on failure the feed simply stops and the hardware timeout resets the chip. */
static void monitor_task(void *params)
{
    TickType_t last_wake = xTaskGetTickCount();
    bool reason_stored = false;

    (void)params;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ETH_HEALTH_PERIOD_MS));
        (void)eth_health_evaluate(&l_health);

        if (eth_health_should_feed(&l_health)) {
            watchdog_update();
        } else if (!reason_stored) {
            reason_stored = true;
            if (l_last_good_valid) {
                scratch_store(l_last_good_x10, ETH_REASON_HEALTH_STOPPED);
            }
        }
    }
}

/* Optional status line over USB. Low priority and isolated: it must never matter. */
static void debug_task(void *params)
{
    (void)params;

    for (;;) {
        eth_safety_out_t out;

        taskENTER_CRITICAL();
        out = l_out;
        taskEXIT_CRITICAL();
        printf("eth state=%d out=%u.%u%% default=%d critical=%d fault=%d\n", (int)out.state,
               out.output_pct_x10 / 10u, out.output_pct_x10 % 10u, (int)out.output_is_default,
               (int)out.critical, (int)out.fault);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    bool wd_reset;
    bool restored = false;
    uint16_t boot_pct = ETH_DEFAULT_PCT_X10;
    uint16_t boot_code;
    unsigned tries;
    bool dac_ok = false;

    /* 1. Why did we start? Read it before anything else touches the registers. Only a timeout of
     * OUR watchdog is a fault: watchdog_caused_reboot() is also true after a UF2 flash. */
    {
        eth_scratch_record_t rec;
        bool rec_ok = scratch_load(&rec);
        eth_boot_decision_t d = eth_boot_decide(watchdog_enable_caused_reboot(), rec_ok, &rec);

        wd_reset = d.watchdog_fault;
        restored = d.restored;
        boot_pct = d.restored ? d.pct_x10 : ETH_DEFAULT_PCT_X10;
        l_reset_count = d.reset_count;
        if (d.clear_scratch) {
            for (unsigned i = 0; i < ETH_SCRATCH_WORDS; i++) {
                watchdog_hw->scratch[i] = 0; /* a stale record must never be restored */
            }
        }
    }

    /* 2. DAC first, with bounded I2C writes. The output is written before the scheduler runs. */
    dac_hw_init();
    boot_code = eth_dac_code_from_pct_x10(boot_pct);
    for (tries = 0; tries < 3u && !dac_ok; tries++) {
        dac_ok = dac_write_range() && dac_write_code(boot_code);
    }

#if ETH_DAC_READBACK_ENABLED
    adc_init();
    adc_gpio_init(ETH_READBACK_ADC_GPIO);
    adc_select_input(ETH_READBACK_ADC_GPIO - 26u);
#endif

    stdio_init_all();

    /* 3. Safety logic, health monitor, indicator, sensor input. */
    eth_safety_init(&l_safety, to_ms_since_boot(get_absolute_time()), restored, boot_pct, wd_reset);
    eth_health_init(&l_health);
    memset(&l_out, 0, sizeof(l_out));
    l_out.state = ETH_STATE_BOOT;
    l_out.output_pct_x10 = boot_pct;
    l_out.output_is_default = !restored;
    l_out.reset_recent = wd_reset;
    l_dac_status.i2c_ok = dac_ok;
    if (restored) {
        l_last_good_x10 = boot_pct;
        l_last_good_valid = true;
    }
    if (restored) {
        scratch_store(boot_pct,
                      ETH_REASON_NONE); /* keep the record, with the reset count updated */
    }

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(PWM_INPUT_PIN);
    gpio_set_dir(PWM_INPUT_PIN, GPIO_IN);
    gpio_disable_pulls(PWM_INPUT_PIN); /* the pull-up to 3.3 V is external, see docs/user-todo B2 */
    gpio_set_irq_enabled_with_callback(PWM_INPUT_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true,
                                       &edge_irq);

    /* 4. Watchdog, then the tasks. The ECU signal tasks and the monitor outrank CAN (priority 1)
     * so CAN can never delay them. */
    watchdog_enable(ETH_WATCHDOG_MS, true);

    xTaskCreate(signal_task, "eth_signal", E_TASK_STACK_WORDS, NULL, E_PRIO_SIGNAL, NULL);
    xTaskCreate(dac_task, "eth_dac", E_TASK_STACK_WORDS, NULL, E_PRIO_SIGNAL, NULL);
    xTaskCreate(monitor_task, "eth_monitor", E_TASK_STACK_WORDS, NULL, E_PRIO_MONITOR, NULL);
    xTaskCreate(debug_task, "eth_debug", E_TASK_STACK_WORDS, NULL, E_PRIO_CAN, NULL);
    (void)ethanol_can_start();

    vTaskStartScheduler();

    for (;;) {
    }
}
