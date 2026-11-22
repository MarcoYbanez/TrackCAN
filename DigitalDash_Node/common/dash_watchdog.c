/*******************************************************
 * @file dash_watchdog.c
 * @brief Hardware watchdog glue shared by the M4 and M7 apps (Zephyr). See dash_watchdog.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_watchdog.h"

#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

/*******************************************************
 * Macros
 *******************************************************/
#define MONITOR_STACK_SIZE 1024

/*******************************************************
 * Static Variables
 *******************************************************/
static dash_health_t l_health;
static const struct device *l_wdt;
static int l_channel;
static uint32_t l_period_ms;
static struct k_thread l_thread;
static K_THREAD_STACK_DEFINE(l_stack, MONITOR_STACK_SIZE);

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void monitor(void *a, void *b, void *c)
{
    (void)a;
    (void)b;
    (void)c;

    for (;;) {
        k_msleep(l_period_ms);
        (void)dash_health_evaluate(&l_health);
        if (dash_health_should_feed(&l_health)) {
            (void)wdt_feed(l_wdt, l_channel);
        }
        /* After a failure there is no feed, ever: the hardware timeout resets the chip. */
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int dash_watchdog_start(const struct device *wdt, const dash_health_cfg_t *cfg, uint32_t period_ms,
                        uint32_t timeout_ms, uint8_t options)
{
    struct wdt_timeout_cfg tc = {
        .window = {.min = 0, .max = timeout_ms},
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };
    int ret;

    if (wdt == NULL || !device_is_ready(wdt)) {
        return -ENODEV;
    }
    if (period_ms == 0 || timeout_ms <= period_ms) {
        return -EINVAL; /* the monitor must feed well inside the timeout */
    }
    dash_health_init(&l_health, cfg);
    if (!dash_health_should_feed(&l_health)) {
        return -EINVAL; /* a bad supervision configuration */
    }

    l_wdt = wdt;
    l_period_ms = period_ms;
    l_channel = wdt_install_timeout(wdt, &tc);
    if (l_channel < 0) {
        return l_channel;
    }
    ret = wdt_setup(wdt, options);
    if (ret < 0) {
        return ret;
    }
    (void)wdt_feed(wdt, l_channel);

    k_thread_create(&l_thread, l_stack, K_THREAD_STACK_SIZEOF(l_stack), monitor, NULL, NULL, NULL,
                    K_HIGHEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&l_thread, "dash_wdt");
    return 0;
}

void dash_watchdog_checkin(uint8_t entity)
{
    dash_health_checkin(&l_health, entity);
}

bool dash_watchdog_stopped(void)
{
    return !dash_health_should_feed(&l_health);
}
