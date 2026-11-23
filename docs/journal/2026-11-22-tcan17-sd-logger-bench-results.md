# TCAN-17: SD logger bench results (2026-11-22)

Bench only: the CAN controller ran in loopback (no other node), the card is a SanDisk Extreme 128 GB, FAT32. Every log below says
`mode=loopback bench data` in a NOTE line. Files were read with `scripts/check_datalogger_log.py` (tests: `scripts/test_check_datalogger_log.py`, 15 cases).

## Verified (read from the files)  [verified: observed]
- **The logger works**: card mounts, `LOGnnnn.CSV` created, header and records well formed, nothing overwritten. All 11 files pass the checker.
- **Throughput**: the flood file (LOG0010) holds 315,000 sequence-numbered frames across six rate steps (250 to 8,000 frames/s, 20 s each), **0 missing**,
  so the logger sustained at least 8,000 frames/s with no loss. Worst-case classic CAN at 500 kbit/s is about 4,000 frames/s.
- **Clean close before a USB reboot** (`sd_logger_prepare_reboot`): LOG0005 to LOG0009 (five USB reflashes in a row) each end on a complete record, the
  card remounted at about 1 s every time, and the next file number was correct.
- **Hard power cuts**: LOG0010 and LOG0011 end on a complete record, and a read-only `fsck_msdos -n` of the card afterwards was clean (exit 0).
- **Startup loss is counted, not silent**: LOG0003 lost 1,066 frames and LOG0004 lost 4 before the file opened; both say `dropped=N` in a NOTE and
  the heartbeat counter gap matches. The queue went from 64 to 256 records afterwards.
- **CAN loopback**: state error-active, TEC 0, REC 0, steady about 62 lines/s.

## Not verified / open
- **How much a power cut loses.** The files end cleanly and the logger syncs every 1 s (`SYNC_MS`), but the moment of each cut is unknown, so the loss
  window is not measured. It needs a cut at a known time (for example a relay, or a marker printed on the console just before).
- **Why the card got stuck.** One reflash from an image WITHOUT the clean-close step left the card failing with `Card never left busy state` until a power
  cycle (then it took about 17 s to come up: LOG0003). The Teensy's SD socket has no power control (Zephyr warns about it). My explanation (a soft reset
  mid-write leaves the card busy) fits the data but is one occurrence. The five clean-close reflashes did not reproduce it. A reboot that is NOT preceded
  by the handshake (a crash, the watchdog, the PROGRAM button) could still leave the card stuck; the logger retries every 2 s and recovers after a power cycle.
- **The "bus-off with no other node" explanation** I gave for the 1.6 s log (LOG0002) is NOT confirmed. ISO 11898-1 error-passive transmitters do not
  go bus-off from ACK errors alone, so the controller may have sat error-passive with its mailboxes full. The status line now prints the CAN state and
  TEC/REC; run the non-loopback image with the card in to see which it is. This matters for TCAN-11 (real bus).
- **1 to 2 ms timestamp jitter** appears when two threads queue frames (only the bench flood does); the checker tolerates 2 ms. In production only the CAN thread queues.
- The real bus, real traffic and CAN errors are untested; no other node was connected.
