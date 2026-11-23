/*******************************************************
 * @file datalogger.c
 * @brief DataLogger Node (Teensy 4.1, Zephyr): TrackCAN port and cyclic transmit/receive.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "datalogger.h"
#include "logger_segments.h"

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
#define CAN_STATUS_MS 10000

/*******************************************************
 * Static Variables
 *******************************************************/
static const struct device *const l_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

CAN_MSGQ_DEFINE(l_rx_msgq, CAN_RX_QUEUE_LEN);

static tcan_t l_can;
static struct k_spinlock l_lock;
static uint8_t l_heartbeat_counter;
static datalogger_can_tap_t l_tap;

/* The logging button: a level in Logger_Control frames, turned into one callback per accepted press (see logger_segments.h for the rules). */
#define BUTTON_DEBOUNCE_MS 300u
#define BUTTON_STALE_MS 400u /* the frame comes every 100 ms */
static seg_button_t l_btn;
static uint32_t l_btn_last_rx_ms;
static datalogger_can_button_cb_t l_btn_cb;
static unsigned l_logger_mode;
static unsigned l_logger_card;
static volatile bool l_can_up; /* the controller has started */

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
    int ret = can_send(l_can_dev, &zf, K_NO_WAIT, tx_done, NULL);

    if (ret == 0 && l_tap != NULL) {
        l_tap(true, frame);
    }
    return ret;
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
    if (l_tap != NULL) {
        l_tap(false, frame);
    }
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
#ifdef DATALOGGER_CAN_LOOPBACK
    /* Bench only: the controller talks to itself, so no other node is needed to acknowledge frames. Allowed while stopped. */
    ret = can_set_mode(l_can_dev, CAN_MODE_LOOPBACK);
    if (ret < 0) {
        return ret;
    }
#endif
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
            .oil_temp = s.oil_temp_degc_x10,
            .coolant_temp = s.coolant_temp_degc_x10,
            .fuel_level = s.fuel_level_pct_x10,
        };

        if (trackcan_data_logger_analog_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_DATALOGGER_ANALOG, buf);
        }
    }

    /* Oil pressure has its own fast message (50 Hz), so the slow signals above do not ride at that rate (docs/logging-rates.md). */
    if (tcan_tx_due(&l_can, TCAN_MSG_DATALOGGER_OILPRESSURE)) {
        struct trackcan_data_logger_oil_pressure_t m = {.oil_pressure = s.oil_pressure_bar_x100};

        if (trackcan_data_logger_oil_pressure_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_DATALOGGER_OILPRESSURE, buf);
        }
    }

    if (tcan_tx_due(&l_can, TCAN_MSG_DATALOGGER_LIGHTS)) {
        buf[0] = (uint8_t)(s.lights & 0xFFu);
        buf[1] = (uint8_t)(s.lights >> 8);
        (void)tcan_tx(&l_can, TCAN_MSG_DATALOGGER_LIGHTS, buf);
    }

    if (tcan_tx_due(&l_can, TCAN_MSG_DATALOGGER_STATUS)) {
        struct trackcan_data_logger_status_t st;
        k_spinlock_key_t skey = k_spin_lock(&l_lock);

        st.log_mode = (uint8_t)l_logger_mode;
        st.card_state = (uint8_t)l_logger_card;
        k_spin_unlock(&l_lock, skey);
        if (trackcan_data_logger_status_pack(buf, &st, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_DATALOGGER_STATUS, buf);
        }
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
    seg_button_init(&l_btn, BUTTON_DEBOUNCE_MS, BUTTON_STALE_MS);

    /* A missing transceiver or bus must never stop the rest of the firmware: keep retrying. */
    while (can_bring_up() != 0) {
        LOG_WRN("CAN not ready, retrying");
        k_msleep(CAN_INIT_RETRY_MS);
    }
    LOG_INF("TrackCAN up on %s", l_can_dev->name);
    l_can_up = true;

    uint32_t last_status_ms = k_uptime_get_32();

    while (1) {
        k_spinlock_key_t key = k_spin_lock(&l_lock);

        tcan_poll(&l_can);
        k_spin_unlock(&l_lock, key);

        /* The button: act only on a NEW Logger_Control frame (a changed receive time), never on the cached last one, so frames that stop are noticed. */
        {
            uint32_t now = k_uptime_get_32();

            if (l_can.rx_seen[TCAN_MSG_LOGGER_CONTROL] && l_can.rx_ms[TCAN_MSG_LOGGER_CONTROL] != l_btn_last_rx_ms) {
                l_btn_last_rx_ms = l_can.rx_ms[TCAN_MSG_LOGGER_CONTROL];
                if (seg_button_frame(&l_btn, (l_can.rx_data[TCAN_MSG_LOGGER_CONTROL][0] & 1u) != 0u, now) && l_btn_cb != NULL) {
                    l_btn_cb();
                }
            }
            seg_button_tick(&l_btn, now);
        }

        publish();
        if ((uint32_t)(k_uptime_get_32() - last_status_ms) >= CAN_STATUS_MS) {
            int state = -1;
            unsigned tec = 0, rec = 0;

            last_status_ms = k_uptime_get_32();
            (void)datalogger_can_status(&state, &tec, &rec);
            LOG_INF("CAN state %d (0 active, 1 warning, 2 passive, 3 bus-off) tec %u rec %u", state, tec, rec);
        }
        k_msleep(CAN_THREAD_PERIOD_MS);
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int datalogger_can_status(int *state, unsigned *tx_err, unsigned *rx_err)
{
    enum can_state st;
    struct can_bus_err_cnt cnt;
    int ret;

    if (!device_is_ready(l_can_dev)) {
        return -ENODEV;
    }
    ret = can_get_state(l_can_dev, &st, &cnt);
    if (ret == 0) {
        *state = (int)st;
        *tx_err = cnt.tx_err_cnt;
        *rx_err = cnt.rx_err_cnt;
    }
    return ret;
}

void datalogger_can_set_button_cb(datalogger_can_button_cb_t cb)
{
    l_btn_cb = cb;
}

void datalogger_can_set_logger_status(unsigned mode, unsigned card_state)
{
    k_spinlock_key_t key = k_spin_lock(&l_lock);

    l_logger_mode = mode & 0x3u;
    l_logger_card = card_state & 0x3u;
    k_spin_unlock(&l_lock, key);
}

void datalogger_can_set_tap(datalogger_can_tap_t tap)
{
    l_tap = tap;
}

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

#ifdef DATALOGGER_CAN_BENCH_BUTTON
/*
 * BENCH ONLY (needs DATALOGGER_CAN_LOOPBACK): plays a button on the bus, so the whole path (frame, edge logic, mode switch, file switch) can be tested
 * with no button hardware. Logger_Control level frames every 100 ms; the button is down for 500 ms at 20 s, 50 s and 80 s after start.
 */
static const uint32_t k_press_at_ms[] = {20000u, 50000u, 80000u};

static void bench_button_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (!l_can_up) {
        k_msleep(100);
    }
    for (;;) {
        uint32_t t = k_uptime_get_32();
        bool down = false;
        struct can_frame f = {.id = TRACKCAN_LOGGER_CONTROL_FRAME_ID, .dlc = 1};

        for (unsigned i = 0; i < ARRAY_SIZE(k_press_at_ms); i++) {
            if (t >= k_press_at_ms[i] && t < k_press_at_ms[i] + 500u) {
                down = true;
            }
        }
        f.data[0] = down ? 1u : 0u;
        (void)can_send(l_can_dev, &f, K_NO_WAIT, NULL, NULL);
        k_msleep(100);
    }
}

K_THREAD_DEFINE(bench_button, 1024, bench_button_thread, NULL, NULL, NULL, 8, 0, 0);
#endif
