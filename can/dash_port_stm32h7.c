/*******************************************************
 * @file dash_port_stm32h7.c
 * @brief TrackCAN port for the Dash Node's STM32H7 (M4 core, Zephyr CAN API on the FDCAN
 * controller).
 *
 * Classic CAN only: 500 kbit/s, sample point 87.5 %, 11-bit IDs, no FD frames. The bit rate and
 * sample point come from the devicetree (m4/app.overlay), the controller is `zephyr,canbus`.
 * Sending never blocks and receiving polls a message queue, the same design as the DataLogger's
 * port. Not built on the host: the host tests use a fake port.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash.h"

#ifdef __ZEPHYR__
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

/*******************************************************
 * Macros
 *******************************************************/
#define CAN_RX_QUEUE_LEN 32

/*******************************************************
 * Static Variables
 *******************************************************/
static const struct device *const l_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

CAN_MSGQ_DEFINE(l_rx_msgq, CAN_RX_QUEUE_LEN);

static bool l_started;
static bool l_filter_added;
static bool l_failed;
static uint8_t l_init_step;
static int16_t l_init_err;
static uint32_t l_core_clock_hz;
static uint16_t l_prescaler;
static uint16_t l_tq_per_bit;
static uint16_t l_sample_point;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void tx_done(const struct device *dev, int error, void *user_data)
{
    (void)dev;
    (void)user_data;
    (void)error; /* Fire and forget: a lost frame is simply sent again next cycle. */
}

static int port_send(const tcan_frame_t *frame)
{
    struct can_frame zf;

    if (!l_started) {
        return -1;
    }
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

/*******************************************************
 * Public Function Definitions
 *******************************************************/
const tcan_port_t *dash_port_stm32h7(void)
{
    static const tcan_port_t port = {port_send, port_receive, port_now_ms};

    return &port;
}

bool dash_port_stm32h7_start(bool loopback)
{
    /* Accept every standard frame; the core ignores IDs it does not know. */
    static const struct can_filter accept_all = {.id = 0, .mask = 0, .flags = 0};
    int ret;

    if (l_started) {
        return true;
    }
    l_failed = true;
    l_init_step = DASH_CAN_STEP_DEVICE_NOT_READY;
    l_init_err = 0;
    if (!device_is_ready(l_can_dev)) {
        /* Zephyr keeps the result of the driver's own init (a positive errno); 0 with initialized
         * == false means the init never ran. This is the only way to learn why a controller failed
         * to come up on a core with no console. */
        l_init_err = (int16_t)(l_can_dev->state != NULL ? -(int)l_can_dev->state->init_res : 0);
        return false;
    }
    l_init_step = DASH_CAN_STEP_SET_MODE;
    ret = can_set_mode(l_can_dev, loopback ? CAN_MODE_LOOPBACK : CAN_MODE_NORMAL);
    if (ret < 0) {
        l_init_err = (int16_t)ret;
        return false;
    }
    /* Add the filter once: adding it again on a retry would queue every frame more than once. */
    if (!l_filter_added) {
        l_init_step = DASH_CAN_STEP_ADD_FILTER;
        ret = can_add_rx_filter_msgq(l_can_dev, &l_rx_msgq, &accept_all);
        if (ret < 0) {
            l_init_err = (int16_t)ret;
            return false;
        }
        l_filter_added = true;
    }
    l_init_step = DASH_CAN_STEP_START;
    ret = can_start(l_can_dev);
    if (ret < 0) {
        l_init_err = (int16_t)ret;
        return false;
    }
    l_started = true;
    l_failed = false;
    l_init_step = DASH_CAN_STEP_NONE;

    /* What the driver believes: the kernel clock it computes with and the timing it derives from
     * it. Only a real second node on the bus proves the bit rate; this shows whether the clock the
     * driver assumes is the one that was intended. */
    {
        struct can_timing t;
        uint32_t rate = 0;
        int sp_error = can_calc_timing(l_can_dev, &t, 500000, 875);

        if (can_get_core_clock(l_can_dev, &rate) == 0) {
            l_core_clock_hz = rate;
        }
        if (sp_error >= 0) {
            l_prescaler = (uint16_t)t.prescaler;
            l_tq_per_bit = (uint16_t)(1u + t.prop_seg + t.phase_seg1 + t.phase_seg2);
            /* can_calc_timing() returns the sample point ERROR in per mille, not the sample point:
             * work it out from the segments (the sample is taken after sync + propagation + phase
             * segment 1). */
            l_sample_point = (uint16_t)(((1u + t.prop_seg + t.phase_seg1) * 1000u) / l_tq_per_bit);
        }
    }
    return true;
}

void dash_port_stm32h7_status(struct dash_can_hw_status *out)
{
    struct can_bus_err_cnt cnt = {0};
    enum can_state st;

    memset(out, 0, sizeof(*out));
    if (l_failed) {
        out->state = DASH_CAN_HW_INIT_FAILED;
        out->init_step = l_init_step;
        out->init_err = l_init_err;
        return;
    }
    if (!l_started || can_get_state(l_can_dev, &st, &cnt) < 0) {
        out->state = DASH_CAN_HW_UNKNOWN;
        return;
    }
    out->tx_err = cnt.tx_err_cnt;
    out->rx_err = cnt.rx_err_cnt;
    out->core_clock_hz = l_core_clock_hz;
    out->prescaler = l_prescaler;
    out->tq_per_bit = l_tq_per_bit;
    out->sample_point_permille = l_sample_point;
    switch (st) {
    case CAN_STATE_ERROR_ACTIVE:
        out->state = DASH_CAN_HW_ACTIVE;
        break;
    case CAN_STATE_ERROR_WARNING:
        out->state = DASH_CAN_HW_WARNING;
        break;
    case CAN_STATE_ERROR_PASSIVE:
        out->state = DASH_CAN_HW_PASSIVE;
        break;
    case CAN_STATE_BUS_OFF:
        out->state = DASH_CAN_HW_BUS_OFF;
        break;
    default:
        out->state = DASH_CAN_HW_STOPPED;
        break;
    }
}

#else /* host build: the host tests use their own fake port, so this one is never called */

const tcan_port_t *dash_port_stm32h7(void)
{
    return NULL;
}

bool dash_port_stm32h7_start(bool loopback)
{
    (void)loopback;
    return false;
}

void dash_port_stm32h7_status(struct dash_can_hw_status *out)
{
    out->state = DASH_CAN_HW_INIT_FAILED;
    out->tx_err = 0;
    out->rx_err = 0;
    out->init_step = DASH_CAN_STEP_NONE;
    out->init_err = 0;
    out->core_clock_hz = 0;
    out->prescaler = 0;
    out->tq_per_bit = 0;
    out->sample_point_permille = 0;
}

#endif /* __ZEPHYR__ */
