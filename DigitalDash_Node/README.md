# TrackCAN Dash Node (STM32H745I-DISCO, Zephyr)

Two Zephyr images, one per core, sharing a snapshot through SRAM4:

| Core | Folder | Job |
|------|--------|-----|
| **M7** | `m7/` | Owns the clocks and the display. Reads the snapshot and draws it with LVGL on the board's built-in 4.3" 480x272 LCD. |
| **M4** | `m4/` | Owns CAN (FDCAN). Runs the shared TrackCAN Dash logic (`can/dash.c`) and writes the snapshot. |

`shared/dash_shared.*` is the hardware-independent exchange (sequence counter, heartbeat, link
supervision) and is host tested. `common/dash_xcore.h` has the target-specific parts: the SRAM4 bus
clock and a hardware-semaphore (HSEM) lock. The M4 signals the M7 through the Zephyr HSEM mailbox
(`ipm_send`) after each write; the M7 treats that only as a hint and always checks the heartbeat.

**Fail closed:** if the M4 is missing at start-up (2 s grace) or its heartbeat stops (250 ms), the M7
shows a full-screen "NO DATA / CAN CORE LOST" alert. It never keeps showing old numbers as live.

## Status

M1 (this folder): both images build, the exchange is host tested. The M4 publishes **test values**,
not CAN data yet. **Nothing here has run on hardware.**

## Build

Needs Zephyr (`ZEPHYR_BASE`), the Zephyr virtualenv and the ARM GNU toolchain, as used for the
DataLogger. From the repository root:

```
export PATH=$HOME/zephyrproject/.venv/bin:$PATH ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=/opt/homebrew
cmake -S DigitalDash_Node/m4 -B build/dash_m4 -GNinja -DBOARD=stm32h745i_disco/stm32h745xx/m4 -DTRACKCAN_PATH=$PWD
cmake -S DigitalDash_Node/m7 -B build/dash_m7 -GNinja -DBOARD=stm32h745i_disco/stm32h745xx/m7 -DTRACKCAN_PATH=$PWD
ninja -C build/dash_m4 && ninja -C build/dash_m7
```

Host tests for the exchange:

```
cmake -S DigitalDash_Node/tests -B build/dash_tests && cmake --build build/dash_tests && ./build/dash_tests/test_dash_shared
```

## Demo drive (until real CAN data arrives)

The M4 image runs `shared/dash_sim.c`: a repeating ~44 s loop of idle, full-throttle pulls through 1st to 5th gear,
cruising and braking, with boost, AFR, battery, temperatures, oil pressure, fuel and lamps that go with it. Loop 2
(about 90 s in) injects an oil pressure sag, loop 3 a charging fault and a check engine lamp; the pattern repeats
every 3 loops. The values are a plausible model, not measurements. Boost, battery, AFR and gear have no CAN source
yet (TCAN-4): only the demo marks them valid, so with real data they show "--". The M7 console (ST-LINK USB serial,
115200 baud) prints the gear, rpm, speed and boost once a second to compare with the screen. The design has no gear
indicator, so gear is only in that log.

## Flashing

Zephyr's default runner for this board is STM32CubeProgrammer, which is not installed here. The board
documentation also allows OpenOCD. Each core is flashed separately (M7 at 0x08000000, M4 at
0x08100000). **Both images must be flashed**: the M7 starts the M4, and without the M4 image the screen
shows "waiting for M4" and then the "CAN CORE LOST" alert.

**How the M4 starts (read from this Zephyr tree, `soc/st/stm32/stm32h7x/soc_m7.c`):** if the option byte
BCM4 already started the M4, the M7 releases it through a hardware semaphore; otherwise the default
`CONFIG_STM32H7_BOOT_M4_AT_INIT=y` (resolved in both builds) makes the M7 start the M4 itself. The board
documentation's `STM32H7_BOOT_CM4_CM7` name no longer exists in this tree.

If the two images were built from different snapshot layouts (for example only one core was reflashed
after `dash_snapshot_t` changed), the M7 shows "REFLASH BOTH CORES" and draws nothing from the M4.

See `docs/user-todo/dash-todo.md` for the bench test.

## Known risks (see docs/journal/2026-11-21-tcan3-digital-dash.md)

- FDCAN on the M4 (M2): the kernel clock source is chosen by the M7; whether the M4 driver can use it
  as configured is unproven.
- The 1024x600 Winstar panel is a memory-bandwidth stretch on this board (16-bit SDRAM).
