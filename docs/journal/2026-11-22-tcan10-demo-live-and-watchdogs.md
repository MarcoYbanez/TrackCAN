# TCAN-10: demo and live build modes, the DEMO marker, and per-core watchdogs

Date: 2026-11-22. Tags: **[measured]** observed on the STM32H745I-DISCO this session, **[read]** read in the Zephyr tree or the
ST HAL header, **[unverified]** not checked.

## Decisions

- **Build option, not a runtime switch.** The M4 image is built as `demo` or `live` (`-DDASH_DEMO=ON|OFF`; the script takes
  `demo|live`). Demo stays the default until the FDCAN port exists (TCAN-6), as the ticket says. A runtime switch was
  rejected: a demo that can start in a car can be mistaken for real readings.
- **The DEMO marker rides in the snapshot** (`DASH_FLAG_DEMO`), set only by the demo image as a compile-time constant. The M7
  draws a red frame round the whole screen and a red DEMO badge in a patch measured empty in the design (x 167 to 287,
  y 69 to 99). A live image cannot set the flag.
- **Alive supervision** follows the Ethanol Node's design (AUTOSAR WdgM style): each task checks in, a monitor feeds the
  hardware watchdog only while all tasks check in within a window, two failed 100 ms periods stop the feed for good.
  The monitor thread has the top application priority, so a busy main loop cannot starve it into a false reset.

## Findings

1. **The stub CAN clock returned a constant 0**, so in a live build nothing could ever go stale and the ethanol boot alert
   (2 s) could never fire: a fail-open. The stub now uses `k_uptime_get_32()`. [measured] The live image shows the alert
   after the grace period.
2. **RPM and speed had no validity flag**, so a live image with no source would have shown `0 mph`. The snapshot now carries
   `DASH_EXTRA_ENGINE`; without it the bars stay empty and the speed shows `--`. Layout version 4.
3. **Zephyr describes only IWDG1.** The H745 has a second watchdog, IWDG2 (`IWDG2_BASE = D3_APB1PERIPH_BASE + 0x4C00` in
   `stm32h745xx.h`) [read]. The M4 gets it through a devicetree overlay: the Zephyr driver (`wdt_iwdg_stm32.c`) uses the first
   enabled `st,stm32-watchdog` node. Its debugger-halt option freezes IWDG1 only, so it is not used on the M4.
4. **A watchdog reset from either core restarts the whole chip.** [measured] After an M7 hang the M4's demo restarted from
   the beginning, and an M4 hang reset the M7 (reset cause 0x11, WATCHDOG). The reference manual's reset-domain text was not
   read; this is the observed behavior.

## Measurements (console over the ST-LINK serial, timestamps taken on the Mac)

| Test | Result |
|---|---|
| 75 s of normal running, both watchdogs on | one boot banner, no false reset |
| M7 busy-hangs at 5 s | chip resets about 0.6 s after the hang (banner every 5.6 s), `last reset cause 0x11 (WATCHDOG)` |
| M4 busy-hangs at 5 s | chip resets about 0.4 s after the hang; the demo restarts |
| Live image, 2 s grace | `demo 0 engine 0 analog 0 ethanol 0 lights 0`, ethanol alert on after the grace |
| Demo image | `demo 1 engine 1`, alert off |
| Boot, warm reset | UI at 7 ms, first frame 35 ms, first real data 75 ms (after kernel start) |

Expected worst case from the design: a task stalls mid-period, the next two periods fail, the feed stops at the second
failure and the 300 ms timeout then fires: about 0.5 to 0.6 s.

## What the screen does when the M7 stalls

[unverified on the display, designed] LTDC keeps scanning the last frame from SDRAM, so a frozen picture stays up for at most
about 0.6 s; then the chip resets and every value shows `--` until live data arrives. The owner can see it with a hang build:
`-DDASH_TEST_HANG_AFTER_MS=5000` on the M7 project.

## Not done

- Reset-domain behavior of IWDG1 and IWDG2 from the reference manual (only observed).
- Option bytes for hardware-started watchdogs (the software-started default worked).
- Whether a hung render inside LVGL is caught the same way as the busy loop used in the test (it stops the loop, so the
  render check-ins stop: the same path, but not injected).
