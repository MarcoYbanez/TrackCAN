/*******************************************************
 * @file bootloader_request.c
 * @brief Recognizes PJRC's "reboot into the programming bootloader" request. See bootloader_request.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "bootloader_request.h"

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void bootloader_request_init(bootloader_request_t *r)
{
    r->seen = false;
    r->fired = false;
    r->seen_ms = 0;
}

bool bootloader_request_poll(bootloader_request_t *r, uint32_t baud, uint32_t now_ms)
{
    if (r->fired) {
        return false; /* once only */
    }
    if (!r->seen) {
        if (baud != BOOTLOADER_REQUEST_BAUD) {
            return false;
        }
        r->seen = true;
        r->seen_ms = now_ms;
    }
    if ((uint32_t)(now_ms - r->seen_ms) >= BOOTLOADER_REQUEST_DELAY_MS) {
        r->fired = true;
        return true;
    }
    return false;
}
