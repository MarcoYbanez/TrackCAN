/*******************************************************
 * @file usb_transfer.h
 * @brief Copies the SD logs to the Mac over the USB serial port (TCAN-21). Protocol: logger_xfer.h. Read-only, only closed
 *        LOGnnnn.CSV files, and it runs in its own low-priority thread, so logging is never held up by it.
 *******************************************************/
#ifndef USB_TRANSFER_H
#define USB_TRANSFER_H

/* Starts the transfer thread on the USB serial port. Returns 0, or a negative errno if the port does not exist. Call once, after the SD
 * logger has started. */
int usb_transfer_start(void);

#endif /* USB_TRANSFER_H */
