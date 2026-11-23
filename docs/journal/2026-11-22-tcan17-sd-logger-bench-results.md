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

## Power cuts, measured  [verified: observed, 2 measurable cuts]
Bench-only console time mark every 100 ms (`DATALOGGER_SD_BENCH_CUTTEST`) against the last record in the file; three cuts by unplugging USB while logging.
- LOG0014: last record t=70,926 ms, last console mark 71,123 ms: the cut lost about **0.2 s** at most.
- LOG0015: last record t=36,522 ms, last console mark 36,425 ms: the file was not behind the console (the console UART lags): about **0 s** lost.
- LOG0016: not measurable (the console recording had ended); the file still ends on a complete line.
- All three files end on a complete record, pass the checker, and a read-only `fsck_msdos -n` of the card after all three cuts was clean (exit 0).
- The design bound is still `SYNC_MS` = 1 s; two cuts do not prove it. Treat 1 s as the limit and about 0.2 s as what was seen.

## Not verified / open
- **The "stuck card" was a card that was not fully seated  [verified: observed, 3 occurrences].** Three times the SD driver logged `Card does not
  support CMD8, assuming legacy card` after 1 s and `Card never left busy state` every 3.4 s; a 5 s and a multi-minute power cycle did not clear it,
  and it never recovered by itself in 3.5 minutes. Each time the card had just been pulled and put back by hand (first after reading it in the Mac).
  On the third occurrence the owner found the card was not pushed in; once seated it mounted at once, with no power cycle or reboot, and the logger
  noticed the card itself. My earlier explanations (a soft reset mid-write; the Mac reader's signalling mode; power-off time; the missing power
  control on the SD socket) were guesses and are **not supported**. The earlier first-time recovery after about 17 s is most likely a re-seat during that unplug (not proven).
  The clean-close handshake before a USB reboot is still correct practice but was never shown to fix anything. The log now says "not inserted or not fully seated?".
- **CAN with no other node  [verified: observed]:** the non-loopback image sat at **error-active, TEC 0, REC 0** for 75 s, so it was NOT bus-off and NOT
  error-passive (my earlier bus-off explanation was wrong). With no errors counted and the log stopping after about 1.6 s (50 lines), the controller
  most likely stops transmitting without trying (for example it never sees an idle bus with no transceiver or other node) [unverified]. This is TCAN-11's
  to settle: it needs a transceiver and a second node.
- **1 to 2 ms timestamp jitter** appears when two threads queue frames (only the bench flood does); the checker tolerates 2 ms. In production only the CAN thread queues.
- The real bus, real traffic and CAN errors are untested; no other node was connected.
- **FlexCAN driver warning in loopback:** `can_mcux_flexcan: Unhandled status 0x000014c1 (result = 0x...ff)` about every 100 ms from about 6 s after boot
  (1,468 times in 15 minutes). No frame was lost or mispaired (all loopback files pass), so it looks harmless to the data, but the cause is not understood
  [unverified], and I did not check whether it also happens with the controller on a real bus. It floods the console; worth understanding before TCAN-11.
