# TCAN-19: automatic file rotation, bench results (2026-11-22)

Bench only (CAN loopback + the flood generator), SanDisk Extreme 128 GB FAT32. Rule: a new `LOGnnnn.CSV` every 10 minutes or 30 MB, whichever comes
first (bench builds shorten it: `rotate` = 20 s, `rotatesize` = 2000 KB). Each new file starts with `continues=LOGnnnn.CSV` and every file of a bench
build says it is bench data. Checker: `scripts/check_datalogger_log.py` takes several files and checks them as one stream.

## Verified from the files  [verified: observed]
- **Age rule:** 11 files, 10 rollovers, 328,710 lines as one stream: PASS. 315,000 sequence-numbered flood frames, 0 missing, all six rate steps up to
  8,000 frames/s. The time between the last record of a file and the first of the next: 0 to 46 ms.
- **Size rule:** 4 rollovers, gap at most 5 ms. Frames were dropped in that run (see below), all counted in `dropped=` NOTEs; the heartbeat gap and the
  flood gaps were explained by them. Its label failure (LOG0031 to LOG0034) was my stale binary (flashed without rebuilding), not a firmware bug.

## Frame drops under the 8,000 frames/s flood
- Runs so far: 14,035 dropped (queue 256, 2 KB buffer); 0 (same config); 13,128 (queue 2048, 16 KB buffer, 8 KB writes); then 5 runs with timing
  instrumentation: 0 each.
- **Measured: single 8 KB card writes take 100 to 200 ms, in clusters** (slowest seen 200 ms; sync up to 14 ms; a rollover under 100 ms). At 8,000 frames/s
  a 200 ms write queues about 1,600 frames; at 4,000 frames/s (the worst-case classic CAN rate) 800. The old queue (256) overflows on any write over about
  64 ms at 4,000 frames/s, so the bigger queue (2048) is justified by this measurement, not by an A/B pass.
- **Not explained  [unverified]:** the two 13,000-frame episodes would need a stall of about 1.6 s or more. It did not reproduce in the 5 instrumented runs.
  Every drop is counted and written to the log as a NOTE with the queue peak and the slowest write and sync, so it is never silent.
- Options if long stalls matter on the real bus: a deeper queue in the unused OCRAM2 (512 KB), or the Recent ring of TCAN-20.

## Not yet verified
- The files written by the bigger-write configuration (LOG0041 onward) have not been read by the checker; they will be pulled over USB (TCAN-21) and checked.
- Real traffic (TCAN-11); the real 600 s / 30 MB values were never exercised (only the 20 s and 2000 KB bench values).
