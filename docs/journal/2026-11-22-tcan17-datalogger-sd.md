# TCAN-17: the DataLogger's SD card logger

Date: 2026-11-22. Tags: **[read]** read in the Zephyr tree this session, **[host]** tested on the host, **[not run]** written and built but never run on the Teensy.

## Where it started

The DataLogger's README says its main purpose is recording to an SD card, but there was no SD code: `main.c` was a bring-up scaffold
(GPIO, SPI and random-number tests, three ADC channels printed) and `src/logger/telemetry_recorder.c` was an empty file.

## What the board support already gave

- [read] `boards/pjrc/teensy4/teensy41.dts`: `&usdhc1` is enabled with its pin groups and a child `mmc` node,
  `compatible = "zephyr,sdmmc-disk"`, `disk-name = "SD"`; `sdhc0` is an alias; `teensy41.yaml` lists `sdhc` as supported. So the
  controller, pins and disk were already described; the missing parts were the file system config, the mount and the logging code.
- [read] `teensy40.dts`: `led0` is the board LED on pin 13 (`gpio2 3`), used for status.
- [read] Zephyr's `fs_sample` shows the FAT-on-SD pattern: `CONFIG_DISK_ACCESS`, `CONFIG_FILE_SYSTEM`, `CONFIG_FAT_FILESYSTEM_ELM`, mount
  point `/SD:`, `fs_mount` with `FS_FATFS`.

## Decisions

- **Never format a card.** `CONFIG_FS_FATFS_MOUNT_MKFS` is turned **off**: Zephyr's FAT option creates a file system when the mount
  fails, which would erase an exFAT or unrecognised card. Checked in the final `.config`: not set. exFAT support is also off, so the
  card must be FAT32.
- **Never overwrite.** A new `LOGnnnn.CSV` on every start, numbered one above the highest on the card; the logger refuses to write
  at 9999 rather than reuse a name. 8.3 names, so no long-file-name support is needed.
- **The CAN thread never waits for the card.** A tap in `can/datalogger.c` (`datalogger_can_set_tap`) is called for every frame the
  Node sends (only when the controller accepted it) and receives; it only queues (`K_NO_WAIT`) and counts a drop if the queue is
  full. All file work is in the logger's own thread at a lower priority than the CAN thread.
- **Power cuts:** write every 250 ms or 1 KB, flush the file to the card once a second. At most about a second is lost, and a
  half-written last line is possible (a reader drops it). The loss is bounded by design; it has not been measured.
- **Format:** CSV, `t_ms,dir,id,dlc,data` (time since power-up in ms: there is no RTC yet), raw frames in hex, plus NOTE lines from the
  logger itself (start, frames dropped). Raw frames, not decoded values, so nothing is lost and the DBC decodes it afterwards.
- **Feedback without a console:** the console is LPUART6 and needs a USB-serial adapter, so the board LED shows the state: slow blink
  logging, fast blink no card, steady on a card or file system error.

## Tested on the host (the logic that can be wrong without hardware)

`src/logger/logger_core.c`: the CSV line format (checked against bytes worked out by hand from the DBC examples), clamping, buffer
limits, notes, the log file names, picking the next number from a directory listing one entry at a time (near misses, any case, a full
card), and the flush and sync policy including the millisecond counter wrapping. 73 checks; 14 deliberate bugs were caught, and
the first time-wrap test was too weak (a naive wrap bug passed because both sides wrapped together), so the wrap cases were rebuilt
so that only one side wraps.

## Not run on the Teensy

Everything in `sd_logger.c` (the mount, the directory scan, the writes, the sync, the LED, recovery when the card comes and goes) and
the tap in `can/datalogger.c` are written and built (132 KB of flash) but **not run**: no Teensy was attached and no card tested. The
bench steps are in `docs/user-todo/hardware-todo.md`. Unknowns until then: whether the mount and the first write work on a real card,
how long a mount and a sync take, the write speed, and whether the logger thread's 4 KB stack and the SD stack's memory use are enough.

## Absences

- The Teensy has no debug probe: it is flashed by its USB bootloader (`teensy_loader_cli`), and nothing here can read its memory like
  the Dash. The LED, an adapter on the console pins, and the file itself are the only evidence.
- No real-time clock: log times are milliseconds since power-up. Joining a log to wall-clock time needs a GPS or RTC later.
