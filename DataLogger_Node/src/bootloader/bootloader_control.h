/*******************************************************
 * @file bootloader_control.h
 * @brief Reboot the Teensy 4.1 into PJRC's programming bootloader when the host asks over USB.
 *
 * The request is PJRC's own: the host sets the USB serial port to 134 baud (see bootloader_request.h). Nothing else triggers it.
 * The reboot step is PJRC's _reboot_Teensyduino_ (cores/teensy4/usb.c:241): "bkpt #251". HARDWARE-UNVERIFIED under Zephyr: see
 * docs/journal/2026-11-22-tcan18-teensy-usb-bootloader.md.
 *******************************************************/
#ifndef BOOTLOADER_CONTROL_H
#define BOOTLOADER_CONTROL_H

/*
 * Starts a low-priority poll (every 10 ms, on the system work queue) of the USB serial port's baud rate. Returns 0, or a negative
 * errno if the USB serial port does not exist. Does nothing to the CAN logger, the SPI sensors or the UART console.
 */
int bootloader_control_start(void);

#endif /* BOOTLOADER_CONTROL_H */
