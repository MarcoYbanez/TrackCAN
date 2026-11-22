/*******************************************************
 * @file bootloader_request.h
 * @brief Recognizes PJRC's "reboot into the programming bootloader" request. Pure logic: no Zephyr, no hardware.
 *
 * The request is the one PJRC's own Teensy 4 core uses (cores/teensy4/usb.c, lines 843 to 847): the host sets the USB serial
 * (CDC) port to exactly 134 baud. The firmware then waits a moment so that USB request completes (PJRC waits 80 start-of-frame
 * interrupts, about 10 ms) and reboots. It is a deliberately odd baud rate: nothing opens a port at 134 baud by accident.
 *
 * Like PJRC's firmware, once the request has been seen it is NOT cancelled by a later line-coding change: a host that sets 134 baud
 * and immediately closes the port (as PJRC's own loaders do) must still get its reboot.
 *******************************************************/
#ifndef BOOTLOADER_REQUEST_H
#define BOOTLOADER_REQUEST_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define BOOTLOADER_REQUEST_BAUD 134u /* PJRC's value, cores/teensy4/usb.c:843 */
#define BOOTLOADER_REQUEST_DELAY_MS 50u /* more than PJRC's ~10 ms: the host's request is certainly complete */

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    bool seen;   /* the request has been seen */
    bool fired;  /* poll has returned true */
    uint32_t seen_ms;
} bootloader_request_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

void bootloader_request_init(bootloader_request_t *r);

/*
 * Call with the baud rate the host last set, at least every few milliseconds. Returns true exactly ONCE: when the baud rate has been
 * 134 and BOOTLOADER_REQUEST_DELAY_MS have passed since it was first seen. Any other baud rate (133, 135, 1340, 115200, ...) never
 * triggers it. Wrap-safe in time.
 */
bool bootloader_request_poll(bootloader_request_t *r, uint32_t baud, uint32_t now_ms);

#endif /* BOOTLOADER_REQUEST_H */
