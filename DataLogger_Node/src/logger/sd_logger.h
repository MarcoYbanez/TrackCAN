/*******************************************************
 * @file sd_logger.h
 * @brief The SD card logger (Zephyr): mounts the card, makes a new numbered file on every start and writes the CAN frames the Node
 *        sends and receives to it as CSV (format in logger_core.h).
 *
 * The CAN thread only queues frames (never blocks); this module's own thread does all the file work. A missing, unformatted or
 * failing card never stops the rest of the firmware: the logger keeps retrying and recovers when a card is back. The logger never
 * formats a card and never overwrites a file. The status LED (led0, pin 13) shows what it is doing: slow blink = logging, fast
 * blink = no card, steady on = the card or file system failed.
 *******************************************************/
#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define SD_LOGGER_MOUNT_POINT "/SD:"

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum
{
	SD_LOGGER_STARTING = 0,
	SD_LOGGER_NO_CARD,	/* the card did not answer */
	SD_LOGGER_FS_ERROR, /* a card answered but the file system could not be mounted, or a write failed */
	SD_LOGGER_LOGGING,
} sd_logger_state_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/* Starts the logger thread and hooks the CAN tap. Call once. */
void sd_logger_start(void);

sd_logger_state_t sd_logger_state(void);

/* The name of the file being written now ("D0000007.CSV" or "M0000007.CSV"), or false when none is open. Other readers (the USB copy, TCAN-21)
 * must not serve it. */
bool sd_logger_open_file(char *name, size_t size);

/* The USB copy holds this while it reads a file, so the ring's deletion never runs under a reader (it skips and tries at the next rollover). */
void sd_logger_serving_lock(void);
void sd_logger_serving_unlock(void);


/*
 * Before a deliberate reboot (TCAN-18): drains the queue, writes the buffer, syncs and closes the file, and stops the logger for good.
 * Waits at most timeout_ms. Returns true if the file was closed cleanly (or there was nothing open), false on timeout.
 */
bool sd_logger_prepare_reboot(uint32_t timeout_ms);

#endif /* SD_LOGGER_H */
