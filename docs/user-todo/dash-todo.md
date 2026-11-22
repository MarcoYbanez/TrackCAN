# Digital dash to-do (TCAN-3)

Things only you can do. Claude cannot run SquareLine Studio, flash the board or look at it.

## 1. Re-export the UI for the built-in display (480x272, LVGL 9)

The design is `UI/DigitalDash/DigitalDash.spj`. The folder `UI/DigitalDash_480x272/` is only an OLD
export (LVGL 8.3.11, for the STM32H7B3I-DK), so it cannot be used with Zephyr's LVGL 9.5.

- [ ] Open `UI/DigitalDash/DigitalDash.spj` in the new SquareLine Studio. It may upgrade the project
      file (it was saved by 1.6.1, project version 58). The project is in git, so you can undo.
- [ ] Project settings: **resolution 480 x 272**, **colour depth 16 bit**, **LVGL 9.x** (as close to
      9.5 as the tool offers), no board profile.
- [ ] Give the widgets clear names (for example `ui_LabelRpm`, `ui_LabelEthanol`) and include a
      **critical alert screen or panel** for the Ethanol Node fault (big, red, readable at a glance).
- [ ] **Export UI Files** (not "Export Project") into a new folder `UI/DigitalDash_480x272/lvgl9_export/`.
- [ ] Tell Claude which SquareLine and LVGL versions it reported.

Until then the M7 app uses a small hand-written LVGL screen, so CAN and the cross-core path are not
blocked by the design.

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
