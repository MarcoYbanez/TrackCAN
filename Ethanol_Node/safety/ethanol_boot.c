/*******************************************************
 * @file ethanol_boot.c
 * @brief Decides, at start-up, whether the previous run ended in a real watchdog timeout.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol_boot.h"

#include <stddef.h>

/*******************************************************
 * Public Function Definitions
 *******************************************************/
eth_boot_decision_t eth_boot_decide(bool wd_enable_caused, bool record_valid,
                                    const eth_scratch_record_t *rec)
{
    eth_boot_decision_t d = {false, false, 0, 0, false};

    if (!wd_enable_caused) {
        /* Power-up, UF2 flash, reset button, bootrom reboot: no fault, nothing to restore. */
        d.clear_scratch = true;
        return d;
    }

    d.watchdog_fault = true;
    if (record_valid && rec != NULL) {
        d.restored = true;
        d.pct_x10 = rec->last_good_pct_x10;
        d.reset_count = (rec->reset_count < 255u) ? (uint8_t)(rec->reset_count + 1u) : 255u;
    } else {
        d.reset_count = 1;
    }
    return d;
}
