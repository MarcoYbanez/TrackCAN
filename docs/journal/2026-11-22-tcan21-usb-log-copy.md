# TCAN-21: copy the SD logs to the Mac over USB (2026-11-22)

`scripts/datalogger_pull.py list|pull [--check]` talks to the Node's USB serial port (protocol: `DataLogger_Node/src/transfer/logger_xfer.h`). Read-only,
closed `LOGnnnn.CSV` files only, never the one being written, no delete command. The card stays in the Teensy and logging keeps running.

## Verified on the hardware  [verified: observed]
- **Correctness:** all 67 files (88.5 MB) downloaded with 0 failures, each checked against the Node's CRC-32. 32 of them were then compared byte for byte
  with copies I had read directly off the card in the Mac: all identical (this is independent of the Node's CRC). A second pull skipped all files.
- **Logging is not disturbed:** a full 68-file download ran WHILE the flood logged at up to 8,000 frames/s. The flood's 315,000 sequence-numbered frames
  all arrived (0 missing), rollover gaps at most 2 ms, console dropped counter 0. FatFS re-entrancy (`FS_FATFS_REENTRANT=y`) is what makes two threads
  on the card safe; it was OFF before this ticket.
- **A rotated run is checked as a chain:** `--check` splits the downloaded files into runs at each file that does not say `continues=`. 8 runs written by
  the bigger-write configuration passed (one of them had counted drops, no silent loss).
- **A cut transfer:** the tool was killed mid-download. The Node stayed on USB, the next LIST worked and a full 12 MB GET afterwards passed its CRC.
- **The bootloader request and the console:** the USB reboot-and-flash (TCAN-18) still works with the transfer thread owning the port's interrupt
  handler (every flash in this work went through it), and opening the port at 115200 baud (the tool does) never reboots the Node.

## Speed  [verified: measured]
- About **1 MB/s** end to end (0.90 to 1.04 MB/s on files over 1 MB; small files are slower per byte). A 2 MB file takes about 2 s, a 30 MB rotated file
  about 30 s, all 88 MB about 2 minutes.
- Where the time goes (device-side timing, 12 MB file, 11.8 s): reading the SD card 8.1 s (about 1.5 MB/s), CRC and USB sending 3.0 s (about 4 MB/s).
- Tried and **did not help** (reverted): 512-byte USB fills instead of 64 (0.99 vs 0.96 MB/s), 32 KB card reads instead of 4 KB (7.5 s vs 8.1 s of reading),
  silencing the SD driver's per-command warnings (7.9 s). The card runs at 4-bit, 50 MHz (so the link could do much more), and DMA is on, so the limit is
  somewhere in Zephyr's SD read path per block; the cause is **not found**. Overlapping the card read with the USB send would gain about 30 %; not done.

## Not verified / open
- The PROGRAM button with this firmware (needs the owner); the transfer on the real car (power, cable, hub); a download of a file larger than a few MB
  over a long cable; what happens if two programs open the port at once (not supported).
