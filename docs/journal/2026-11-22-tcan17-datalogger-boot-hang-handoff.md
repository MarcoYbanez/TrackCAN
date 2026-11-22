# TCAN-17 PAUSED: the DataLogger firmware is silent after flashing (handoff to resume)

Paused 2026-11-22 by the owner to do a different task (a USB "reboot into program mode" feature). Resume from here.

## State in one paragraph

The SD card logger (TCAN-17) is written, host-tested (73 checks, 14 mutations caught) and builds, but it has **never run**. Flashing it to the
Teensy 4.1 worked (`teensy_loader_cli` reported Success, the Teensy left its bootloader) and then the firmware is **completely silent**: no
console output at all (not even the Zephyr boot banner) and the board LED stays off. A second build **without any SD code** is silent too, so the
SD logger is cleared. The suspect is the **TrackCAN CAN logic from TCAN-1 (FlexCAN3 init at boot)**, which has never run on this Teensy:
the old firmware that printed (before commit `0a3eac5`, TCAN-1) compiled only `main.c`. The missing banner means the hang is during driver
startup, before any thread runs, so the FlexCAN3 driver init is the prime suspect.

## What is on the Teensy right now

The **`nosd` build** (CAN logic, no SD): silent. The Teensy is NOT in its bootloader. A control build (`scaffold`) has been built but **not flashed**.

## The next experiment (needs the owner to press the Teensy's program button once)

1. `scripts/datalogger_build_flash.sh build scaffold` (already built at `build/datalogger_scaffold`, 82,580 B: the original scaffold, no CAN, no SD).
2. Owner presses the Teensy's program button. Confirm with `ioreg -p IOUSB -l -w0 | grep -c '"idVendor" = 5824'` (1 = in the bootloader,
   PJRC `16C0:0478`). Then `scripts/datalogger_build_flash.sh flash scaffold`.
3. Listen to the console (below) for 10 s.
   - **It prints** (`true random number: ...`, `SPI_READ -- CH 0: ...`): the CAN init is the culprit. Then read the FlexCAN3 driver init
     (`/opt/toolchains/zephyr_support/zephyr/drivers/can/can_mcux_flexcan.c`), the pins and clocks, and fix it. This is also a real bug
     that would block TCAN-11 (the real-bus test).
   - **Still silent**: not the CAN code. Suspect how this toolchain's images boot on this Teensy, or the console wiring. Check the probe wiring
     and try the previous firmware again.
4. After the culprit is fixed, flash `full` and run the bench test in `docs/user-todo/hardware-todo.md` (LED: slow blink = logging,
   fast = no card, steady = error; file `LOG0001.CSV` on a FAT32 card).

## How to read the Teensy (the Pico Debug Probe's UART is the console)

- The Pico Debug Probe is on USB as `Debug Probe (CMSIS-DAP)`, vendor `2E8A:000C`. Its UART bridge is `/dev/cu.usbmodem2143402` (the number can change
  when it is replugged: look for the port under the probe), **115200 8N1**. It is wired to the Teensy's console pins (pins 0 and 1, LPUART6).
- Read it with `/Users/openinvite/zephyrproject/.venv/bin/python -I` and `pyserial` (reading is harmless). The old firmware printed
  `true random number: ...`, `PIN READ`, `PIN OUT`, `SPI_READ -- CH n: v` about every 900 ms.
- The Teensy 4.1 has no debug header for the probe: the Teensy is flashed by its USB bootloader only (`teensy_loader_cli`, button press each
  time) and nothing can read its memory. The LED, the UART and the SD file are the only evidence.
- The Teensy's own USB shows as `16C0:0478` (HalfKay) only while in the bootloader; the firmware has no USB of its own.

## Branch and files

- Branch `TCAN-17-DataLogger-SD-card-up-and-running`, pushed, **no PR yet**. TCAN-17 is **In Progress** in Jira.
- `DataLogger_Node/src/logger/logger_core.[ch]` (tested core) and `sd_logger.[ch]` (Zephyr glue), the CAN tap in `can/datalogger.[ch]`,
  `DataLogger_Node/CMakeLists.txt` (options `DATALOGGER_SD` and `DATALOGGER_CAN`), `scripts/datalogger_build_flash.sh` (variants
  `full`, `nosd`, `scaffold`), journal `2026-11-22-tcan17-datalogger-sd.md`, bench steps in `docs/user-todo/hardware-todo.md`.
- Host tests: `cmake -S DataLogger_Node/tests -B build/datalogger_tests && cmake --build build/datalogger_tests && ./build/datalogger_tests/test_logger_core`.
- `build/` is not in git: rebuild with the script.

## Other state

- The Dash board (ST-LINK) was unplugged. Dash work is all merged to `main` (PRs 6 to 10); nothing pending there.
- Open decision for the owner (not blocking): which other data deserves the full-screen error (only oil pressure so far).
