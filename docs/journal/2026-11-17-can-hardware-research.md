# 2026-11-17: CAN hardware research (Teensy 4.1 + Feather CAN)

Context: TCAN-1 (CAN backbone). Before designing the shared CAN layer, record what the
hardware can actually do. Each fact is marked **[verified]** (read from a primary source
in this session), **[doc]** (stated by a vendor or third-party page, not cross-checked)
or **[unverified]** (general knowledge, check before relying on it).

## 1. Teensy 4.1 (DataLogger Node, i.MX RT1062, Zephyr)

### CAN controllers

| Bus | Controller | FD? | TX pin | RX pin | SoC pads (TX / RX) | Zephyr node |
|-----|-----------|-----|--------|--------|--------------------|-------------|
| CAN1 | FlexCAN1 | No | **22** | **23** | AD_B1_08 / AD_B1_09 | `flexcan1` (`nxp,flexcan`) |
| CAN2 | FlexCAN2 | No | **1** | **0** | AD_B0_02 / AD_B0_03 | `flexcan2` (`nxp,flexcan`) |
| CAN3 | FlexCAN3 | **Yes** | **31** | **30** | EMC_36 / EMC_37 | `flexcan3` (`nxp,flexcan-fd`) |

- **[verified]** Pins and pads: Zephyr `boards/pjrc/teensy4/doc/index.rst` and
  `teensy4-pinctrl.dtsi` (Zephyr 4.4.99 at `$ZEPHYR_BASE`).
- **[verified]** Only CAN3 is the FD controller (`nxp,flexcan-fd`, 64 message buffers).
- **[verified]** PJRC product page: 3 CAN buses, 1 with CAN FD; **no on-board
  transceiver** ("a CAN transceiver chip must be added"); pins are 3.3 V only and
  **not 5 V tolerant**.
- **[verified]** A PJRC forum thread questioned the CAN3 pin mapping (EMC_36/EMC_37 vs
  EMC_24). Zephyr's pinctrl for the Teensy uses EMC_36 = TX (pin 31) and EMC_37 = RX
  (pin 30), matching the table above. Still worth a scope check on real hardware.
- **[doc]** Boards made after July 2021 use the "B" silicon revision, which PJRC says
  fixes obscure CAN bus hardware bugs.

### Zephyr default state

- **[verified]** `teensy41.dts` includes `teensy40.dts`. It sets
  `zephyr,canbus = &flexcan1` and enables **only `flexcan1`**. `flexcan2` and `flexcan3`
  have pinctrl set but no `status = "okay"`, so they must be enabled in the overlay.
- **[verified]** The Zephyr nodes are defined in `dts/arm/nxp/imxrt/nxp_rt10xx.dtsi`.

### Conflicts to watch

- **TrackCAN uses CAN3 (pins 30/31)**, per the owner's DataLogger schematic. The firmware
  chooses `flexcan3` as `zephyr,canbus` and disables the board's default `flexcan1`.
- **[verified] CAN2 shares pins 0/1 with UART6 (LPUART6).** The DataLogger overlay
  (`DataLogger_Node/boards/teensy41.overlay`) sets `zephyr,console = &lpuart6`, which is
  pins 0/1. **CAN2 cannot be used while the console is on LPUART6.** CAN1 (22/23) and
  CAN3 (30/31) are free of this conflict.
- Transceiver needed on every bus used. The repo has the MCP2562FD datasheet
  (`assets/datasheets/digital_dash/CANFD-MCP2562FDT.pdf`), which is an FD-capable
  transceiver and would suit the Teensy's CAN3.

## 2. CAN Feather (Ethanol Node)

Assumed to be the **Adafruit RP2040 CAN Bus Feather** (RP2040 + MCP25625). **Needs
confirming with the owner.** Adafruit also sells an M4 CAN Feather with a true
CAN-FD-capable MCU, so check the board's silkscreen.

- **[doc]** MCU: RP2040, 133 MHz, 3.3 V logic.
- **[doc]** CAN controller: **MCP25625** ("an MCP2515 with built-in transceiver").
  Standard and extended frames up to **1 Mbit/s**. The Adafruit guide does not mention
  CAN FD.
- **[unverified] This is a classic CAN 2.0B controller. It does not support CAN FD.**
  Confirm against the MCP2515/MCP25625 datasheet.
- **[doc]** Termination: 120 ohm between CAN_H and CAN_L, removable by cutting the
  `TERM` jumper on the front of the board.
- **[doc]** Connector: 3.5 mm terminal block `CANBUS` (L, common ground, H). A 5 V
  charge pump supplies the transceiver.

### Controller wiring (SPI1)

| Signal | RP2040 GPIO | Feather name |
|--------|-------------|--------------|
| SCK | GPIO14 | SCK (SPI1) |
| MOSI | GPIO15 | MO (SPI1) |
| MISO | GPIO8 | MI (SPI1) |
| CS | GPIO19 | `CAN_CS` |
| INT | GPIO22 | `CAN_INTERRUPT` |
| RESET | GPIO18 | `CAN_RESET` |
| STANDBY | GPIO16 | `CAN_STANDBY` |
| TX0RTS | GPIO17 | n/a |
| RX0BF | GPIO23 | n/a |

- **[doc]** Source: Adafruit Learn guide. Only the first 100k of 234k characters of the
  page were read. Cross-check against the Adafruit pinout diagram / schematic before
  wiring.
- Note: GPIO22 is used for CAN INT. The Ethanol Node currently uses GPIO26 (PWM in) and
  GPIO2/3 (I2C1), so there is no clash with `Ethanol_Node/main.c`.

## 3. Consequence for the bus design

The architecture diagram labels the bus "CAN-FD Backbone", but the Ethanol Node's
controller is classic-only. Both facts cannot hold for every frame:

- **[unverified] A classic-only controller that sees a CAN FD frame treats it as a
  protocol error and signals an error frame**, which disturbs the whole bus. Classic
  controllers are not "FD tolerant" unless the datasheet says so. Verify for the
  MCP25625.
- So either (a) all TrackCAN traffic uses **classic 2.0 frames** (8-byte payload,
  max 1 Mbit/s), with FD-capable Nodes simply running in classic mode, or (b) the
  Ethanol Node gets an FD-capable controller (for example MCP2518FD).
- Whichever is chosen, the DataLogger and the Dash must be able to **receive and
  interpret classic CAN frames**, as required.

## Sources

- Zephyr 4.4.99 source, local: `boards/pjrc/teensy4/{teensy40.dts,teensy41.dts,teensy4-pinctrl.dtsi,doc/index.rst}`, `dts/arm/nxp/imxrt/nxp_rt10xx.dtsi`
- PJRC Teensy 4.1 product page: https://www.pjrc.com/store/teensy41.html
- PJRC forum, FlexCAN_T4 thread and CAN3 pin question: https://forum.pjrc.com/threads/56035-FlexCAN_T4-FlexCAN-for-Teensy-4
- Adafruit RP2040 CAN Bus Feather guide: https://learn.adafruit.com/adafruit-rp2040-can-bus-feather
- Zephyr `nxp,flexcan-fd` binding: https://docs.zephyrproject.org/latest/build/dts/api/bindings/can/nxp,flexcan-fd.html
