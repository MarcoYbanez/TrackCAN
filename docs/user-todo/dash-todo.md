# Digital dash to-do (TCAN-3)

Things only you can do. Claude cannot run SquareLine Studio, flash the board or look at it.

## 1. Re-export the UI for the built-in display (480x272, LVGL 9)

The design is `UI/DigitalDash/DigitalDash.spj` in THIS repository. `DashProject` (in Documents) is your old
working copy: the repo's `DigitalDash.slp` file still stores absolute paths into it, which is why the export
paths looked strange and the export failed (the saved Project Export Root,
`DashProject/DigitalDash_480x272/SquareLine_Project`, does not exist). The folder
`UI/DigitalDash_480x272/SquareLine_Project` is only an OLD export (LVGL 8.3.11, for the STM32H7B3I-DK).

In SquareLine: Project Settings, **File Export**:

- [ ] **Project Export Root:** `/Users/openinvite/Documents/TrackCAN/UI/DigitalDash_480x272/lvgl9_project`
      (an empty folder, only used by "Export Project". Do NOT point it at `SquareLine_Project`, it would
      overwrite the old export.)
- [ ] **UI Files Export Path:** `/Users/openinvite/Documents/TrackCAN/UI/DigitalDash_480x272/lvgl9_export`
- [ ] **LVGL Include Path:** `lvgl.h` (not `lvgl/lvgl.h`: Zephyr's LVGL provides `<lvgl.h>`; there is no
      `lvgl/` subfolder in its include path).
- [ ] **Image export mode:** leave "Source code" for now (see the size warning below).

In the project settings (not the File Export page):

- [ ] **LVGL version 9.5** (Zephyr here ships exactly 9.5.0). The export you made today in
      `DashProject/UI/DigitalDash_LVGL9` is still **LVGL 8.3.11** (its `ui.h` says so), so the version
      was not applied. Re-export and check that `ui.h` now says `LVGL version: 9.5.x`.
- [ ] Resolution **480 x 272**, colour depth **16 bit**, no board profile (or any profile: only the generated
      starter project changes, not the UI files).
- [ ] Use **Export UI Files**, then send me the exact error text if one still appears.
- [ ] Saving the project rewrites `UI/DigitalDash/DigitalDash.slp` with the new paths: that is expected.

### Size warning: the design does not fit in flash as exported

The 480x272 design as exported today holds **3.9 MB** of image and font data (94 images = 3.79 MB, 3 fonts
= 0.10 MB), but each core's internal flash bank is **1 MB**, and the M7 image already uses about 0.4 MB
for LVGL and the app. The cause is one animation: **61 frames of a 461x39 tachometer strip (3.3 MB)**, plus a
full-screen 480x272 background with alpha (392 KB). The design needs to change (use LVGL's own bar or
arc widgets for the tachometer, an opaque or gradient background) or the images must live in external flash.
Decide with Claude before spending time on the export.

## 2. Bench test of M1 (needs the board)

Build per `DigitalDash_Node/README.md`, then flash the M4 and M7 images.

- [ ] Flash both images and reset the board. Expected on the LCD: "waiting for M4", then "M4 link up"
      with the heartbeat counter and signal counter rising, and the rpm and speed test values changing.
- [ ] Reset only the M4 (or stop it in the debugger). Expected: within about 250 ms a full-screen red
      **"NO DATA / CAN CORE LOST"**, and recovery when the M4 runs again.
- [ ] Note which flashing method worked (STM32CubeProgrammer or OpenOCD) and any error text.

## 3. CAN on the board

- [ ] The two CAN connectors: you read them as **CN10 for FDCAN2 and CN11 for FDCAN1**. ST's manual
      (UM2488 Rev 10) lists the CAN FD parts as **U21 and U22** but its text does not name the
      connectors, so please re-read the silkscreen and send me the exact labels.
- [ ] Which connector do you want on the car bus? FDCAN1 uses PH13 (TX) and PH14 (RX); FDCAN2 uses
      PB13 (TX) and PB5 (RX), from the manual's pin table.
- [ ] Keep the bus classic CAN at 500 kbit/s with exactly two 120 ohm terminators (see
      `hardware-redundancy.md`, `hardware-todo.md` B6). Check whether the board has a terminator on
      the connector.

## 4. The future display

- [ ] You are buying the Winstar WF101JSYAHDNN0 later and do not have it yet. When you have time, download
      its datasheet in a browser and put it in `assets/documentation/` (Winstar's site and the reseller
      mirror both refuse automated downloads). I need the pixel clock, the timing table and the backlight
      current.
