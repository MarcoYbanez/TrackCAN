/*******************************************************
 * @file ethanol_scratch.h
 * @brief The record kept in RP2040 watchdog scratch registers 0-3 across a watchdog reset.
 *
 * Scratch registers survive soft resets and are cleared by a power cycle (RP2040 datasheet
 * 4.7.4). Registers 4-7 belong to the SDK and the bootrom, so only 0-3 are used. The record is
 * protected by a magic value, a complemented copy and a checksum: a corrupt record is ignored.
 *******************************************************/
#ifndef ETHANOL_SCRATCH_H
#define ETHANOL_SCRATCH_H

/*******************************************************
 * Includes
 *******************************************************/
#include <stdbool.h>
#include <stdint.h>

/*******************************************************
 * Macros
 *******************************************************/
#define ETH_SCRATCH_WORDS 4u
#define ETH_SCRATCH_MAGIC 0x45544831u /* "ETH1" */

/* Reason codes stored in the record, for diagnostics. */
#define ETH_REASON_NONE 0u
#define ETH_REASON_STACK_OVERFLOW 1u
#define ETH_REASON_MALLOC_FAILED 2u
#define ETH_REASON_HEALTH_STOPPED 3u

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef struct {
    uint16_t last_good_pct_x10;
    uint8_t reason;
    uint8_t reset_count; /* watchdog resets since power-up, saturating */
} eth_scratch_record_t;

/*******************************************************
 * Public Function Prototypes
 *******************************************************/
void eth_scratch_pack(const eth_scratch_record_t *rec, uint32_t words[ETH_SCRATCH_WORDS]);

/* Returns true and fills rec only if the magic, the complement and the checksum all hold, and
 * the value is within 0..1000. */
bool eth_scratch_unpack(const uint32_t words[ETH_SCRATCH_WORDS], eth_scratch_record_t *rec);

#endif /* ETHANOL_SCRATCH_H */
