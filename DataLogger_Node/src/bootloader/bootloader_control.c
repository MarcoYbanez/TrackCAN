/*******************************************************
 * @file bootloader_control.c
 * @brief Reboot into PJRC's programming bootloader on the USB request. See bootloader_control.h.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <fsl_device_registers.h>

#include "bootloader_control.h"
#include "bootloader_request.h"
#ifndef DATALOGGER_NO_SD
#include "sd_logger.h"
#endif

LOG_MODULE_REGISTER(bootloader_control, LOG_LEVEL_INF);

/*******************************************************
 * Macros
 *******************************************************/
#define POLL_MS 10
#define LOGGER_CLOSE_TIMEOUT_MS 500 /* the most to wait for the SD log to be closed; then reboot anyway */
#define USB_SERIAL DT_NODELABEL(usb_serial)
/* PJRC's own check (usb.c:243): bit 1 of OCOTP CFG5 set means a secure-locked chip, where PJRC uses a different path. This
 * firmware does not implement that path, so it refuses instead of guessing. */
#define OCOTP_CFG5_SECURE 0x02u

/*******************************************************
 * Static Variables
 *******************************************************/
static const struct device *const l_uart = DEVICE_DT_GET(USB_SERIAL);
static bootloader_request_t l_req;
static struct k_work_delayable l_work;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void enter_bootloader(void)
{
    if (OCOTP->CFG5 & OCOTP_CFG5_SECURE) {
        LOG_ERR("secure-locked chip: bootloader entry not implemented, use the PROGRAM button");
        return;
    }
#ifndef DATALOGGER_NO_SD
    /* Close the log cleanly first: a reboot mid-write would leave the file system dirty. Bounded, so a hung card cannot block recovery. */
    if (!sd_logger_prepare_reboot(LOGGER_CLOSE_TIMEOUT_MS)) {
        LOG_WRN("SD log not closed in time, rebooting anyway");
    }
#endif
    LOG_INF("rebooting into the programming bootloader");
    LOG_PANIC(); /* flush pending log output */
    (void)irq_lock(); /* the same as PJRC: nothing may run between here and the trap */
    __asm__ volatile("bkpt #251"); /* PJRC usb.c:244 */
    for (;;) {
    }
}

static void poll(struct k_work *work)
{
    uint32_t baud = 0;

    (void)uart_line_ctrl_get(l_uart, UART_LINE_CTRL_BAUD_RATE, &baud);
    if (bootloader_request_poll(&l_req, baud, k_uptime_get_32())) {
        enter_bootloader();
    }
    k_work_reschedule(&l_work, K_MSEC(POLL_MS));
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int bootloader_control_start(void)
{
    if (!device_is_ready(l_uart)) {
        return -ENODEV;
    }
    bootloader_request_init(&l_req);
    k_work_init_delayable(&l_work, poll);
    k_work_schedule(&l_work, K_MSEC(POLL_MS));
    return 0;
}
