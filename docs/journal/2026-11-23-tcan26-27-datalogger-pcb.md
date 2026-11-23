# TCAN-26 and TCAN-27: the DataLogger carrier PCB (2026-11-23)

Hardware only; no firmware changed. Merged as PR #18 (`547cb85`). Files: `PCB/Datalogger/` (KiCad 10 project, `fab/` Gerbers, `to_buy.csv`).
Nothing has been ordered, built or powered: every electrical claim below is from the schematic and the datasheets/PJRC pages I could read, not from a bench.

## What it is
A 100 x 72 mm two-layer through-hole board that carries a Teensy 4.1 on two 1x24 female headers.

| Block | Parts | Notes |
|---|---|---|
| Input | J1 (4-pin KK 254: 12V, GND, CAN H, CAN L), Q1 FQP47P06 P-FET, R1 10k, D2 1N5245B 15 V zener, D3 TVS | P-FET reverse-polarity protection: drain at the input, source at the load, gate to GND through R1, zener gate-to-source. The TVS sits after the FET, because a unidirectional TVS before it would short a reversed battery. |
| 5 V | U4 OKI-78SR-5/1.5-W36-C, C1 100 uF, C2/C3 10 uF, D1 1N5819 | D1 sits between the regulator and the Teensy VIN pin. |
| ADC | U2 MCP3208 (DIP-16) on **3V3** | SPI1: pin 27 SCK, 26 MOSI, 39 MISO, 38 CS. |
| Sensor inputs | J2 (6-pin KK 254: S1-S4, GND, GND), per channel 2.2k + 470 in series over 4.7k to ground, 100 nF, 1N5819 clamp to 3V3 | (2.2k + 470) / 4.7k gives about 3.2 V at 5 V in. The clamp catches a sensor wire shorted to 12 V. |
| CAN | U3 MCP2562FD-E/P (DIP-8), pins 30 (CRX3) / 31 (CTX3), VDD 5 V, VIO 3V3, STBY to GND, JP1 + R14 120 ohm termination | The Teensy keeps FlexCAN3 in classic mode (firmware), the transceiver is FD-capable. |
| Console | J3 (3 pins: GND, Teensy RX pin 0, Teensy TX pin 1) | LPUART6, 115200 baud, 3.3 V logic only. Added late so the board does not need a respin to get a serial console. |

Layout: Teensy along the top edge, USB end at the left and SD slot end at the right (nothing tall beside the SD end); sensor channels bottom-left,
ADC and CAN chip in the middle, power block at the right; connectors on the bottom and left edges; four M3 mounting holes; GND pour on both layers.

## Decisions and why
- **MCP3208 on 3V3, not 5 V.** The Teensy pins are not 5 V tolerant (PJRC: "pins are not 5V tolerant"). At 5 V the MCP3208 would drive 5 V into the Teensy
  MISO pin, and its high-level input threshold (0.7 x VDD) is above what the Teensy's 3.3 V outputs reach. Your original sketch showed 12 V on VDD/VREF; that
  would destroy the chip (it is a 2.7-5.5 V part).
- **MCP2562FD in the DIP-8 package** (`MCP2562FD-E/P`, listing confirmed on DigiKey) instead of the SOIC parts you already own, so it can be hand-soldered.
- **Pinout from PJRC's pinout card** (read from the card image): CTX3/CRX3 are pins 31/30, SPI1 is 26/27/38/39, Vin 3.6-5.5 V, 3.3 V pin 250 mA max.
  The 15.24 mm row spacing was **measured from the card photo**, not a dimension drawing: check it against your Teensy before ordering.
- **Connectors:** one 4-pin KK 254 for power + CAN, not two 2-pin ones, so a power plug cannot be mated into the CAN socket. Molex numbers other than
  `0016020086` (your screenshot) are unconfirmed.
- **TVS value.** The OKI-78SR datasheet could not be fetched (power.murata.com redirects to a murata.com URL that returned HTTP 500), so its absolute-maximum
  input is **unverified**. D3 is drawn as 1.5KE24A (clamp about 33 V) instead of the 1.5KE36A you own (clamp about 50 V), which may exceed the OKI limit.

## Corrections made along the way
- I first said the program button is always needed to flash. The firmware (TCAN-18) can reboot into the bootloader when the USB serial port is set to 134 baud; the
  repo's own notes still mark that unverified under Zephyr, and the README still says to press the button. Keep access to the button until it is proven.

## Checks run
- Schematic ERC: 0 errors, 0 warnings. Board DRC (KiCad 10.0.8 CLI): 0 errors, 0 unconnected, 0 schematic differences; 41 warnings that placed footprints differ from
  their library copies (cause not investigated).
- Gerber drill counts: 162 plated holes = 159 pads + 3 vias, 4 unplated holes (the M3 mounts). Gerbers were **not** opened in a viewer.

## Tooling findings (so they do not have to be rediscovered)
- **Konnect** adds tools on demand, but the Claude desktop app never lists tools added after startup, so none of its schematic/PCB tools worked. Fix: a launcher
  (`~/Library/Application Support/konnect/preload_proxy.py`, set as the `konnect` command in `claude_desktop_config.json`, backup kept beside it) that loads the
  toolsets right after startup. Konnect's own schematic viewer binary is missing on macOS; `live_viewer.py` in the same folder replaces it (port 8777; 8765 is taken
  by a Logitech plugin).
- **Autorouting:** Freerouting v2.5.0 (a 64.6 MB jar you approved, kept in `~/Library/Application Support/konnect/freerouting/`, no vendor checksum to compare with).
  Konnect's DSN export refuses rounded outlines and pours, so the board is routed on a scratch copy with KiCad's bundled Python (`pcbnew`). Gotchas: leave the GND
  pads unassigned in the routing copy so the pour handles ground; set the default net class to 0.25 mm or the router emits 0.15 mm tracks; replace the board file only
  with the board closed in KiCad. Result: 178 track segments, 3 vias, power nets 0.6 mm, CAN 0.3 mm, rest 0.25 mm.
- A commit `a0693b4` and a duplicate `lib/trackcan.*` library appeared on the branch that I did not make (same git author). Harmless, still in the repo.

## Still open
- Confirm the OKI-78SR absolute-maximum input, then buy the 1.5KE24A (or confirm the 1.5KE36A is safe).
- USB and board power together: PJRC says power should not be applied to VIN while USB is connected, or the bottom pad pair can be cut. My suggestion, **not from
  PJRC**: cut the pair and solder a 1N5819 across it (USB side anode). Check the pad pair against PJRC's photos first. Until then, unplug the 12V connector before USB.
- No fuse on the 12V input.
- Open the Gerbers in the fab's preview. Check the Teensy row spacing. Confirm the Molex and Sullins part numbers in `to_buy.csv`.
- Updating over CAN is TCAN-29 (research): estimates there are calculated, not measured.
