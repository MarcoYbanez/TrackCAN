/*******************************************************
 * @file ethanol_boot.h
 * @brief Decides, at start-up, whether the previous run ended in a real watchdog timeout.
 *
 * Only a timeout of the watchdog this firmware enabled counts. A power-up, a UF2 flash, a
 * reset button or a bootrom reboot all look like "a watchdog register is set" on the RP2040, so
 * they must NOT be treated as a fault and must NOT restore an old value
 * (pico-sdk `watchdog_enable_caused_reboot()` versus `watchdog_caused_reboot()`).
 *******************************************************/
#ifndef ETHANOL_BOOT_H
#define ETHANOL_BOOT_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "ethanol_scratch.h"

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    bool watchdog_fault; /* the firmware's own watchdog timed out: Critical, latched */
    bool restored;       /* a valid last-good value survived and may be used */
    uint16_t pct_x10;    /* the value to restore, when restored */
    uint8_t reset_count; /* watchdog timeouts since power-up, saturating */
    bool clear_scratch;  /* not a real timeout: wipe any stale record */
} eth_boot_decision_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/

/*
 * wd_enable_caused: result of watchdog_enable_caused_reboot(), read BEFORE watchdog_enable().
 * record_valid / rec: the scratch record and whether it passed its checks.
 */
eth_boot_decision_t eth_boot_decide(bool wd_enable_caused, bool record_valid,
                                    const eth_scratch_record_t *rec);

#endif /* ETHANOL_BOOT_H */
