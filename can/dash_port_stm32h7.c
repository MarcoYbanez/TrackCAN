/*******************************************************
 * @file dash_port_stm32h7.c
 * @brief STUB. TrackCAN port for the Dash Node's STM32H7. Not implemented yet.
 *
 * The Dash runs Zephyr (M4 core). Sending and receiving are not implemented yet (TCAN-6); the clock
 * is real, so missing data is detected: nothing ever arrives, every message goes stale or
 * never-received, and the ethanol alert fires 2 s after start.
 *
 * TODO when the toolchain is chosen:
 *  - Configure the FDCAN peripheral in CLASSIC CAN mode (no FD frames, no bit-rate
 *    switching): 500 kbit/s, sample point 87.5 %, 11-bit IDs.
 *  - send:    queue to the TX FIFO without blocking; return negative if the FIFO is full.
 *  - receive: pop one frame from RX FIFO 0 without blocking; return 1 or 0.
 *  - now_ms:  free-running millisecond counter (for example HAL_GetTick()).
 *  - Add an external CAN transceiver and use its datasheet for standby/slope pins.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#endif

#include "dash.h"

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static int stub_send(const tcan_frame_t *frame)
{
    (void)frame;
    return -1; /* not implemented: counted as dropped by the core */
}

static int stub_receive(tcan_frame_t *frame)
{
    (void)frame;
    return 0;
}

/* A real clock. With a constant here nothing is ever marked stale and the ethanol boot alert can
 * never fire, which would make a live build fail open. */
static uint32_t stub_now_ms(void)
{
#ifdef __ZEPHYR__
    return k_uptime_get_32();
#else
    return 0; /* host build only: the host tests never call this port */
#endif
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
const tcan_port_t *dash_port_stm32h7(void)
{
    static const tcan_port_t port = {stub_send, stub_receive, stub_now_ms};

    return &port;
}
