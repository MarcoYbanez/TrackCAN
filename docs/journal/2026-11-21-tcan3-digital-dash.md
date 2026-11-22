# 2026-11-21: TCAN-3 digital dash, research and M1

Branch `TCAN-3-Set-up-the-digital-dash-STM32H745I-DISCO`. Tags: **[verified]** read in a primary
source this session, **[doc]** stated by a page, not cross-checked, **[unverified]** not sourced or
not measured. Related: [CAN decisions](2026-11-17-can-backbone-decisions.md),
[owner to-do](../user-todo/dash-todo.md).

## Owner decisions

- **Toolchain:** Zephyr (same as the DataLogger).
- **Core split:** the **M7 renders the display, the M4 runs the CAN logic**, because the display will
  grow and need more compute.
- **Cross-core link:** shared SRAM plus hardware semaphores, for both synchronization and notification.
- **Display:** build for the board's built-in **480x272** LCD now. The owner **plans to buy the
  Winstar WF101JSYAHDNN0 later and does not own it yet**; that part changes then.
- **UI:** the owner designs in SquareLine Studio (newest version installed) and can re-export in any
  LVGL version needed. A small hand-written LVGL screen is used until the export exists.
- **CAN connectors:** the owner reports two on the board, one for FDCAN2 and one for FDCAN1 (labels
  read as CN10 and CN11, **[unverified]**: ST's manual text does not name them).

## Verified facts

### Board and Zephyr support (local Zephyr 4.4.99 files, STM32H745I-DISCO)

- **[verified]** The board is supported for both cores (`stm32h745i_disco/stm32h745xx/m7` and `/m4`).
  The M7 devicetree enables the 4.3" **480x272 RGB** panel through the LTDC (`ext-sdram = <&sdram2>`),
  an FT5336 touch panel, and both FDCAN controllers. Console is USART3 on the M7.
- **[verified]** Dual-core rules from the board notes: clock configuration is **M7 only** (the M4 can only
  switch bus clocks on and off); each peripheral is assigned to one core in the devicetree before
  compilation; GPIO and interrupt-controller access is protected by a hardware semaphore.
- **[verified]** Zephyr's H7 clock driver locks the RCC with a hardware semaphore when the M4 uses it.
  Zephyr reserves HSEM IDs 0 to 11, so the Dash uses ID 12. The HSEM mailbox is already enabled by the
  board file; the official sample `samples/boards/st/h7_dual_core` uses `ipm_send` and a callback.
- **[verified]** SRAM4 (D3, `0x38000000`) is declared in the H745 devicetree as a memory region and is not
  used by the M4's default RAM (SRAM1).
- **[verified]** FDCAN pins (ST UM2488 Rev 10 pin table, in `assets/documentation/`): **FDCAN1 TX = PH13,
  RX = PH14; FDCAN2 TX = PB13, RX = PB5**. The board layout lists "CAN FDs (U21, U22)" as parts; the text
  does not name the transceiver chip or the connector pins.
- **[verified]** Zephyr's LVGL here is **9.5.0**. The repo's existing UI export
  (`UI/DigitalDash_480x272/SquareLine_Project`) is SquareLine Studio 1.6.1 for **LVGL 8.3.11** with a
  CubeIDE port for the **STM32H7B3I-DK**: a different board and an incompatible LVGL version.
- **[verified]** The Zephyr tree has no DMA2D (2D accelerator) support in its devicetree or display
  drivers; LVGL has its own optional DMA2D setting. Whether this chip has the accelerator is **[unverified]**
  (ST's datasheet was not reachable).

### The future Winstar display

- **[doc]** Winstar's product page (via search results): WF101JSYAHDNN0 is a 10.1", **1024x600**, IPS,
  **RGB** interface, 1100 cd/m2, drivers EK79001HN-S + EK73215BCGA. A reseller mirror's datasheet text
  says 24-bit parallel RGB, 235.0 x 143.0 x 5.05 mm. **The pixel clock, timing table and backlight
  current were not retrievable** (Winstar returns 403; the reseller host has an invalid TLS certificate, which
  was not bypassed). The owner should add the datasheet to `assets/documentation/`.
- **Compute and memory (derived from verified board facts):**

  | | 480x272 | 1024x600 |
  |---|---|---|
  | Pixels | 130,560 | 614,400 (4.7x) |
  | One frame, 16-bit | 0.26 MB | 1.23 MB |
  | Scan-out at 60 Hz, 16-bit | 16 MB/s | 74 MB/s |
  | Scan-out at 60 Hz, 24-bit | 23 MB/s | 111 MB/s |

  The board's SDRAM is **16 bits wide and 8 MB usable** (board file: "only half is available by HW
  design"). **[unverified]** with an assumed 240 MHz bus clock its peak is about 160 MB/s, so 1024x600 at
  16-bit takes about half of it before any drawing, and 24/32-bit is unrealistic. A full frame does not
  fit in the chip's about 1 MB of internal RAM. Decision: measure LVGL frame rate on this board first, and
  treat the big panel as possibly needing a different board.

## M1 result

| Piece | State |
|-------|-------|
| `DigitalDash_Node/shared/dash_shared.*` | Done. 39 host checks (sanitizers); 8 deliberate bugs all caught |
| M4 image (`m4/`) | Builds (13.6 KB flash). Publishes **test values** under an HSEM lock and signals the M7 |
| M7 image (`m7/`) | Builds (389 KB flash). Frame buffer 255 KB in SDRAM (480x272, RGB565), LVGL 9.5, full-screen alert on a lost link |
| On hardware | **Nothing flashed or run** |

### Review fixes (before any hardware)

An independent review found four ways the M7 could have shown wrong data as live. All four are fixed
and covered by tests:

1. `dash_shared_read` copied into the caller's buffer before checking the sequence, so a torn copy could
   leak. It now copies to a local and hands it over only on a clean read; the M7 changes the screen only on a
   good read.
2. A block that was initialised but never written read as valid zeros. The magic is now set by the first
   completed write, not by init.
3. Nothing checked that the two separately flashed images agree on the snapshot layout. The writer now
   stores a layout tag (version plus `sizeof(dash_snapshot_t)`); a mismatch shows "REFLASH BOTH CORES".
4. Boot mode: this Zephyr tree starts the M4 from the M7 in both option-byte cases (see the README); the
   board documentation's older option name does not exist here.

Host checks are now 39 for the exchange (was 29), and 8 deliberate bugs are caught, including the torn copy
leaking, which the first suite could not detect (a test seam simulates the writer finishing mid-read).

### Known gap: nothing watches the display core

If the M7 hangs, the screen freezes on its last frame, including "link up" with no alert. Per-core
independent watchdogs (IWDG1 for the M7, IWDG2 for the M4) are not built yet and belong in a later
milestone, because the dash is the owner's main way of seeing status. **[unverified]** which watchdog
instance maps to which core on this chip.

## Export findings (2026-11-21, after the owner's first SquareLine attempt)

- **[verified]** The repo's `UI/DigitalDash/DigitalDash.slp` stores absolute export paths into the owner's old
  working copy (`/Users/openinvite/Documents/DashProject/...`). The saved Project Export Root does not exist
  there, which is the likely cause of the export error (the exact error text was not seen).
- **[verified]** The export the owner made into `DashProject/UI/DigitalDash_LVGL9` is **SquareLine 1.6.3 with
  LVGL 8.3.11**: the LVGL version setting was not changed to 9.x. Zephyr's LVGL is 9.5.0.
- **[verified, measured from that export]** Assets total **3.89 MB** (94 images 3.79 MB, 3 fonts 0.10 MB), all
  RGB565 with alpha. 61 frames of a 461x39 strip (3.3 MB) form the animated tachometer; one full-screen
  480x272 background is 392 KB. Each Zephyr image gets a **1 MB** internal flash bank (`flash0`, `flash1`), and
  the M7 image is already about 0.4 MB. As designed it does not fit: replace the frame animation with native LVGL
  widgets, or store the images in external memory (the board has QSPI flash and eMMC; not investigated).

### Second export (LVGL 9.5, after the owner fixed the paths)

- **[verified]** `UI/DigitalDash_480x272/lvgl9_export/ui.h` says `LVGL version: 9.5`; images are now palette (I8)
  format, so the assets total **1.45 MB** (was 3.89 MB): main tach 61 frames 1.11 MB, top strip 16 frames
  0.08 MB, background 0.13 MB, 16 small images 0.04 MB, fonts 0.11 MB. Without the two tach strips: 0.27 MB.
  The M7 bank has about 644 KB free, so only the 61-frame main tach is the blocker.
- **[verified]** A scratch build of the export into the M7 app fails: `ui.c:87 redefinition of
  'TopTach_Animation'`. The design has two animations with that name, plus an empty `TopTachometer_Animation`.
- **[verified]** The exported code hard-codes `#include "lvgl/lvgl.h"` and ignores the Include Path setting.
  Zephyr only provides `<lvgl.h>`, so the app needs a one-line shim header (`m7/shim/lvgl/lvgl.h`) instead of
  editing the generated files, which are rewritten on every export.
- **[verified]** The design's animations are self-running demo loops (frame counters), not driven by data.
  The real tach must be set from the RPM value in code.
- The design has labels for MPH, coolant, oil pressure and **AFR**; AFR is not in the CAN catalog yet.

### Dash screen layout, saved before the background image is removed from SquareLine

`UI/DigitalDash/dash_bg_layout.json` (made by `scripts/extract_dash_layout.py` from
`UI/DigitalDash/assets/Gauge_Status/dash_bg2.png`, which is also committed in git) records everything the
480x272 background image contains. **[verified]** by pixel analysis:

- **RPM bars (off state):** 60 main segments at 7 px pitch from x=10, y=20, 7 x 38 px (44 cyan-off `#0E1620`, 6
  amber-off `#2A2008`, 10 red-off `#2A0606`); 16 top segments 27 px wide, 8 px tall, y=8, from x=10.
  Lit colours: cyan `#00E0FF`, amber `#FFB300`, red `#FF1A1A` (one `#FF4014`). Zones: main bar 44/6/10, top bar 6/5/5.
- **7 lamp outlines** (CEL, OIL, BAT, TMP, TC, CAN, GPS) at y=74, about 20 x 13 px, x = 10, 33, 56, 78, 101, 124, 147.
- **Scale digits 0 to 9** at y=63, x = 10, 61, 112, 162, 213, 263, 314, 365, 415, 466.
- **Boost track** 163 x 8 px at (308, 177).
- **Captions:** RPM (412,85), SPEED (10,99), BOOST (308,99), **BAR** (308,158), MPH (10,209).
- The lit segments are drawn exactly on the off segments, so the background can also be recreated from this file.

**Decision recorded:** the RPM bars are drawn in code; lamps are red, normal or dimmed cyan; gauge tiles are
border-only art tinted by status; thresholds are variables (TCAN-5). Note: the baked caption says **BAR**
for boost but the owner wants **PSI**, so that caption must change (edit the PNG, or cover it with a label).

## Risks and unknowns

- **FDCAN on the M4 (M2):** the board devicetree enables FDCAN for the M7. For the M4 the peripheral must be
  assigned to it, and the FDCAN kernel clock source is selected by the M7 side (PLL2). Whether the M4 driver
  can use it is **unproven** and has to be tested on the board.
- **The semaphore:** if a core resets while holding the Dash semaphore, the other core's bounded try-lock
  fails and the link reads as lost (fail closed). Recovery by resetting the board.
- **Cache:** the M7 invalidates its data cache before reading SRAM4. The M4 has no data cache.
- **CAN transceivers:** the manual labels U21/U22; whether they are on-board transceivers or need the owner's
  MCP2562FD is **[unverified]**. M2 tests in FDCAN internal loopback first, which needs neither.

## Next (M2)

CAN receive on the M4 (classic, 500 kbit/s) using `dash_can_init()` and `dash_can_step()`, first in
loopback, then on the real bus with the DataLogger and Ethanol Node; replace the M1 test values with
`dash_can_get_display()`; then the critical-alert screen from the owner's SquareLine export.

## Sources

- ST UM2488 Rev 10 (`assets/documentation/um2488-stm32h745.pdf`, read locally)
- Zephyr 4.4.99: `boards/st/stm32h745i_disco/`, `drivers/clock_control/clock_stm32_ll_h7.c`,
  `soc/st/stm32/common/stm32_hsem.h`, `samples/boards/st/h7_dual_core`
- Winstar product pages (search results): https://www.winstar.com.tw/products/tft-lcd/high-brightness-tft/widescreen-display.html ,
  https://www.winstar.com.tw/products/tft-lcd/high-brightness-tft/wf101jsyahdnb0.html
- Reseller listing: https://unisystem.com/product/displays-lcd-tft/wf101jsyahdnn0-winstar-10-1-1024x600-lcd-tft-display
- ST Community (leads only): https://community.st.com/t5/stm32-mcus-boards-and-hardware/fdcan-communication-circuit-on-stm32h745-50-disco-board/td-p/135720
