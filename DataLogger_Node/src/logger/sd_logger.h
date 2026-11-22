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

#endif /* SD_LOGGER_H */
