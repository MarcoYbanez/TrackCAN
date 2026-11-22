/*******************************************************
 * @file dash_xcore.h
 * @brief Cross-core plumbing shared by the M4 and M7 apps: SRAM4 clock, hardware-semaphore lock.
 *
 * The lock is a plain STM32 hardware semaphore (HSEM) taken in one step. Zephyr reserves IDs 0 to
 * 11 for itself (soc/st/stm32/common/stm32_hsem.h), so the Dash uses 12.
 *******************************************************/
#ifndef DASH_XCORE_H
#define DASH_XCORE_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

#include <stm32h7xx_ll_bus.h>
#include <stm32h7xx_ll_hsem.h>

#include "dash_shared.h"

/*******************************************************
 * Macros
 *******************************************************/
#define DASH_HSEM_ID 12U
#define DASH_LOCK_TRIES 2000u /* bounded: a stuck other core must never hang this one */

/*******************************************************
 * Static Inline Functions
 *******************************************************/
/* The shared block lives in SRAM4 (D3). Its bus clock must be on before the first access. */
static inline void dash_xcore_clock_on(void)
{
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_SRAM4);
}

static inline dash_shared_t *dash_xcore_shared(void)
{
    return (dash_shared_t *)DASH_SHARED_ADDR;
}

/* Takes the semaphore in one step. Returns false if the other core still holds it. */
static inline bool dash_xcore_lock(void)
{
    for (uint32_t i = 0; i < DASH_LOCK_TRIES; i++) {
        if (LL_HSEM_1StepLock(HSEM, DASH_HSEM_ID) == 0u) {
            return true;
        }
    }
    return false;
}

static inline void dash_xcore_unlock(void)
{
    LL_HSEM_ReleaseLock(HSEM, DASH_HSEM_ID, 0);
}

#endif /* DASH_XCORE_H */
