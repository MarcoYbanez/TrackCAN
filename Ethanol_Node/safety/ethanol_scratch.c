/*******************************************************
 * @file ethanol_scratch.c
 * @brief The record kept in RP2040 watchdog scratch registers 0-3 across a watchdog reset.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol_scratch.h"

#include "ethanol_config.h"

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static uint32_t checksum(const uint32_t w0, const uint32_t w1, const uint32_t w2)
{
    return w0 ^ w1 ^ w2 ^ 0xA5A5A5A5u;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
void eth_scratch_pack(const eth_scratch_record_t *rec, uint32_t words[ETH_SCRATCH_WORDS])
{
    words[0] = ETH_SCRATCH_MAGIC;
    /* Value in the low half, its complement in the high half. */
    words[1] =
        (uint32_t)rec->last_good_pct_x10 | ((uint32_t)(uint16_t)~rec->last_good_pct_x10 << 16);
    words[2] = (uint32_t)rec->reason | ((uint32_t)rec->reset_count << 8);
    words[3] = checksum(words[0], words[1], words[2]);
}

bool eth_scratch_unpack(const uint32_t words[ETH_SCRATCH_WORDS], eth_scratch_record_t *rec)
{
    uint16_t value;
    uint16_t complement;

    if (words[0] != ETH_SCRATCH_MAGIC || words[3] != checksum(words[0], words[1], words[2])) {
        return false;
    }
    value = (uint16_t)(words[1] & 0xFFFFu);
    complement = (uint16_t)(words[1] >> 16);
    if ((uint16_t)~value != complement || value > ETH_PCT_MAX_X10) {
        return false;
    }
    if ((words[2] >> 16) != 0u) {
        return false;
    }

    rec->last_good_pct_x10 = value;
    rec->reason = (uint8_t)(words[2] & 0xFFu);
    rec->reset_count = (uint8_t)((words[2] >> 8) & 0xFFu);
    return true;
}
