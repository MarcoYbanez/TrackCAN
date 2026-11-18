/*******************************************************
 * @file datalogger.c
 * @brief DataLogger Node (Teensy 4.1, Zephyr): TrackCAN port and cyclic transmit/receive.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "datalogger.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(datalogger_can, LOG_LEVEL_INF);

/*******************************************************
 * Macros
 *******************************************************/
#define CAN_THREAD_STACK_SIZE 2048
#define CAN_THREAD_PRIORITY 7
#define CAN_THREAD_PERIOD_MS 5
#define CAN_RX_QUEUE_LEN 32
#define CAN_INIT_RETRY_MS 1000

/*******************************************************
 * Static Variables
 *******************************************************/
static const struct device *const l_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

CAN_MSGQ_DEFINE(l_rx_msgq, CAN_RX_QUEUE_LEN);

static tcan_t l_can;
static struct k_spinlock l_lock;
static uint8_t l_heartbeat_counter;

/*
 * PLACEHOLDER VALUES. The Defi sensor transfer curves are not known yet (see
 * docs/journal/2026-11-17-can-backbone-decisions.md), so until the application calls
 * datalogger_can_set_sample() this Node sends zeros.
 */
static struct datalogger_sample l_sample;

/*******************************************************
 * Static Function Prototypes
 *******************************************************/
static int port_send(const tcan_frame_t *frame);
static int port_receive(tcan_frame_t *frame);
static uint32_t port_now_ms(void);
static void can_thread(void *p1, void *p2, void *p3);

static const tcan_port_t l_port = {
    .send = port_send,
    .receive = port_receive,
    .now_ms = port_now_ms,
};

K_THREAD_DEFINE(l_can_thread, CAN_THREAD_STACK_SIZE, can_thread, NULL, NULL, NULL,
                CAN_THREAD_PRIORITY, 0, 0);

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void tx_done(const struct device *dev, int error, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    ARG_UNUSED(error); /* Fire and forget: a lost frame is simply sent again next cycle. */
}

static int port_send(const tcan_frame_t *frame)
{
    struct can_frame zf;

    memset(&zf, 0, sizeof(zf));
    zf.id = frame->id;
    zf.dlc = frame->dlc;
    memcpy(zf.data, frame->data, frame->dlc);

    /* K_NO_WAIT plus a callback keeps this non-blocking. */
    return can_send(l_can_dev, &zf, K_NO_WAIT, tx_done, NULL);
}

static int port_receive(tcan_frame_t *frame)
{
    struct can_frame zf;

    if (k_msgq_get(&l_rx_msgq, &zf, K_NO_WAIT) != 0) {
        return 0;
    }
    if ((zf.flags & (CAN_FRAME_IDE | CAN_FRAME_RTR | CAN_FRAME_FDF)) != 0 ||
        zf.dlc > TCAN_MAX_DLC) {
        return 0; /* TrackCAN only uses classic 11-bit data frames. */
    }

    frame->id = zf.id;
    frame->dlc = zf.dlc;
    memset(frame->data, 0, sizeof(frame->data));
    memcpy(frame->data, zf.data, zf.dlc);
    return 1;
}

static uint32_t port_now_ms(void)
{
    return k_uptime_get_32();
}

static int can_bring_up(void)
{
    /* Accept every standard (11-bit) frame; the core ignores IDs it does not know. */
    static const struct can_filter accept_all = {.id = 0, .mask = 0, .flags = 0};
    static bool filter_added;
    int ret;

    if (!device_is_ready(l_can_dev)) {
        return -ENODEV;
    }
    /* Add the filter once. Retrying it would queue every received frame more than once. */
    if (!filter_added) {
        ret = can_add_rx_filter_msgq(l_can_dev, &l_rx_msgq, &accept_all);
        if (ret < 0) {
            return ret;
        }
        filter_added = true;
    }
    return can_start(l_can_dev);
}

static void publish(void)
{
    struct datalogger_sample s;
    uint8_t buf[TCAN_MAX_DLC];
    k_spinlock_key_t key = k_spin_lock(&l_lock);

    s = l_sample;
    k_spin_unlock(&l_lock, key);

    if (tcan_tx_due(&l_can, TCAN_MSG_DATALOGGER_ANALOG)) {
        struct trackcan_data_logger_analog_t m = {
            .oil_pressure = s.oil_pressure_bar_x100,
            .oil_temp = s.oil_temp_degc_x10,
            .coolant_temp = s.coolant_temp_degc_x10,
            .fuel_level = s.fuel_level_pct_x10,
        };

        if (trackcan_data_logger_analog_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_DATALOGGER_ANALOG, buf);
        }
    }

    if (tcan_tx_due(&l_can, TCAN_MSG_DATALOGGER_LIGHTS)) {
        buf[0] = s.lights;
        (void)tcan_tx(&l_can, TCAN_MSG_DATALOGGER_LIGHTS, buf);
    }

    if (tcan_tx_due(&l_can, TCAN_MSG_HEARTBEAT_DATALOGGER)) {
        struct trackcan_heartbeat_data_logger_t hb = {.status = 0,
                                                      .counter = l_heartbeat_counter++};

        if (trackcan_heartbeat_data_logger_pack(buf, &hb, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_HEARTBEAT_DATALOGGER, buf);
        }
    }
}

static void can_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    (void)tcan_init(&l_can, &l_port, TCAN_NODE_DATALOGGER);

    /* A missing transceiver or bus must never stop the rest of the firmware: keep retrying. */
    while (can_bring_up() != 0) {
        LOG_WRN("CAN not ready, retrying");
        k_msleep(CAN_INIT_RETRY_MS);
    }
    LOG_INF("TrackCAN up on %s", l_can_dev->name);

    while (1) {
        k_spinlock_key_t key = k_spin_lock(&l_lock);

        tcan_poll(&l_can);
        k_spin_unlock(&l_lock, key);

        publish();
        k_msleep(CAN_THREAD_PERIOD_MS);
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void datalogger_can_set_sample(const struct datalogger_sample *sample)
{
    k_spinlock_key_t key = k_spin_lock(&l_lock);

    l_sample = *sample;
    k_spin_unlock(&l_lock, key);
}

tcan_rx_state_t datalogger_can_get(tcan_msg_t msg, uint8_t data[TCAN_MAX_DLC])
{
    k_spinlock_key_t key = k_spin_lock(&l_lock);
    tcan_rx_state_t state = tcan_rx_get(&l_can, msg, data);

    k_spin_unlock(&l_lock, key);
    return state;
}
