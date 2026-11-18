/*******************************************************
 * @file dash_port_stm32h7.c
 * @brief STUB. TrackCAN port for the Dash Node's STM32H7. Not implemented yet.
 *
 * The Dash toolchain (STM32Cube, Zephyr, FreeRTOS, ...) is not decided, so this only marks
 * what the port must do. Until it is filled in the Dash neither sends nor receives.
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

static uint32_t stub_now_ms(void)
{
    return 0; /* TODO: a real clock. With this stub nothing is ever marked stale. */
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
const tcan_port_t *dash_port_stm32h7(void)
{
    static const tcan_port_t port = {stub_send, stub_receive, stub_now_ms};

    return &port;
}
