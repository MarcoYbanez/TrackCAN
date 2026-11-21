# Hardware redundancy to-do (Ethanol Node)

Firmware checks alone cannot catch every failure, because the firmware only sees what the pins
tell it. Each layer below adds a **physical tap** so a second, independent path can disagree with
the first. This file lists every wire, part and decision that redundancy needs, what already
works in firmware, and what does not exist yet.

Related: [hardware-todo.md](hardware-todo.md) (bench and car checks),
`docs/journal/2026-11-17-tcan2-ethanol-safety-research.md` (why and how).

## Summary

| # | Layer | Catches | You must add | Firmware today | Needed? |
|---|-------|---------|--------------|----------------|---------|
| R1 | Safe sensor input | Sensor short to 12 V, open wire, noise | Pull-up to **3.3 V**, series resistor, clamp | Works | **Required** |
| R2 | **DAC output readback** | Dead DAC, stuck I2C, wrong range, over-voltage on the ECU line | Divider from the DAC output to ADC pin **GPIO27 (A1)** | Built, **OFF by default** (`-DETH_DAC_READBACK=ON`) | **Strongly recommended** |
| R3 | Hard voltage limit on the ECU line | More than 5 V reaching the ECU even if firmware and DAC both fail | Clamp or divider on the ECU line | n/a (hardware only) | Recommended |
| R4 | Bias resistor on the ECU line | A dead Node leaving the ECU pin floating | One resistor to a defined voltage | n/a | Depends on your ECU (C1) |
| R5 | **Second sensor tap** | A fault in the interrupt/edge path being invisible, because everything reads the same pin the same way | Second wire from the sensor signal to a **PWM B-channel pin** | **Not built** | Optional |
| R6 | Power and ground quality | Cranking dips, transients, ground offset between Node and ECU | TVS, reverse-polarity FET, shared sensor ground | n/a | Recommended |
| R7 | External watchdog chip | The RP2040's own watchdog failing | A supervisor IC | Not built | Optional, not planned |

**Correction to something I told you earlier:** I once said to wire the DAC output "straight to
an ADC pin". That was wrong. The DAC puts out up to 5 V (10 V in its other range) and an RP2040
pin allows at most 3.6 V (datasheet p.616). R2 below is the right way.

## Feather pin map (what is used, what is free)

Verified from Zephyr's `boards/adafruit/feather_canbus_rp2040` files and the RP2040 datasheet
function table.

| GPIO | Header | Used for | |
|------|--------|----------|---|
| 26 | A0 | **Sensor PWM input** (edge interrupt) | in use |
| 27 | A1 | **DAC readback (ADC1)** | R2, reserved |
| 2, 3 | SDA, SCL | I2C to the DAC | in use |
| 14, 15, 8 | SCK, MOSI, MISO | SPI to the CAN controller | in use |
| 19, 22 | (internal) | CAN chip select, interrupt | in use |
| 16, 18 | (internal) | CAN standby, reset | in use |
| 13 | D13 | Status LED | in use |
| 0, 1 | TX, RX | UART (USB is used for output instead) | free |
| 4, 6, 10, 12, 24 | D4, D6, D10, D12, D24 | | free |
| **5** | **D5** | PWM2 **B** | **free, R5 candidate** |
| **9** | **D9** | PWM4 **B** | free, R5 candidate |
| **11** | **D11** | PWM5 **B** | free, R5 candidate |
| **25** | **D25** | PWM4 **B** | free, R5 candidate |
| 28, 29 | A2, A3 | ADC | free (A3 may be a battery monitor: check before use) |

## R1. Safe sensor input (required)

The Continental sensor output is open collector and needs a pull-up of 1.2 to 3 kΩ to a voltage
of 3.3 to 18 V (sheet `assets/FFS01 (3).jpg`). The RP2040 pin must never see more than 3.6 V.

```
 3.3 V ──[ 2.2 kΩ ]──┬──────────── sensor OUT (open collector)
                     │
                     ├──[ 1 kΩ ]──┬──── GPIO26 (primary input, edge interrupt)
                     │            │
                     │         clamp: 1N5819 Schottky, anode to the pin, cathode to 3.3 V
```

- [ ] Fit the 3.3 V pull-up (not 5 V or 12 V).
- [ ] Add the series resistor and the clamp, so a short of the sensor line to 12 V cannot reach
      the pin.
- [ ] Do **not** use the 1N5245B zener as a pin clamp (it is a 15 V part).
- [ ] The sensor itself runs from the car's 12 V supply (reseller sources: 9 to 18 V). A 5 V
      supply is too low for it.

## R2. DAC output readback (strongly recommended)

This lets the firmware see what the ECU actually receives. It catches a dead DAC, a stuck I2C
bus, a wrong voltage range, and more than 5 V on the line. Firmware compares the reading with
what it commanded: a gap of 3+ points (0.12 V) is a warning, 10+ points (0.40 V) for 500 ms is
Critical, and more than 5.25 V for 200 ms is Critical.

```
 DAC VOUT0 ──┬───────────────────────────────► ECU input pin
             │
             └──[ 22 kΩ ]──┬──────────────────► Feather A1 (GPIO27, ADC1)
                           │
                           └──[ 10 kΩ ]──────► GND
```

Ratio 0.3125: 5 V reads 1.56 V and a stray 10 V reads 3.13 V, so the pin is safe in either DAC
range. The divider draws about 0.16 mA at 5 V.

- [ ] Fit the 22 kΩ and 10 kΩ resistors (both are on your parts list: 22K and 10K, 1 %).
- [ ] With the DAC on the bench (no ECU), put 5 V and 4.5 V on the divider and confirm the
      ADC pin never exceeds about 1.6 V.
- [ ] Rebuild the Ethanol firmware with `-DETH_DAC_READBACK=ON` and confirm the status LED does
      **not** show Critical at key-on. (Left OFF, an unwired pin would raise a false Critical.)
- [ ] Deliberately disconnect the DAC and confirm a Critical within about 1 s.

## R3. Hard voltage limit on the ECU line (recommended)

If the DAC powers up in its 0 to 10 V range, or the firmware's range write fails, more than 5 V
could reach the ECU for a moment. Firmware cannot prevent that. A part can.

- [ ] Decide the limit part **after** you know the ECU input impedance (hardware-todo C1): a
      series resistor forms a divider with that input.
- [ ] Candidate: a clamp to a 5 V rail through a Schottky (1N5819 is on your parts list), or a
      5.1 V zener. Verify with the scope that it does not bend the 4.5 V end of the signal.
- [ ] Measure the DAC's real power-on range first (hardware-todo A1). If it always starts in
      0 to 5 V, this layer matters less.

## R4. Bias resistor on the ECU line (depends on the ECU)

If the Node is dead or unpowered, the ECU input could float or read zero. Zero is E0, the
dangerous direction. Your tune probably cannot treat a bad voltage as a fault, so this is about
giving the line a defined value.

- [ ] Measure what the ECU pin reads with the Node unpowered (hardware-todo D1).
- [ ] If it reads low, consider a resistor to a voltage that reads as high ethanol. Choose the
      value with your tuning knowledge after C1 and C3 are answered.

## R5. Second sensor tap (optional, NOT built yet)

Today the firmware measures the sensor in one way: a GPIO edge interrupt on GPIO26, checked a
second way in software (median period against counted edges). That is an algorithmic cross
check. It is **not** an independent hardware path.

A real second path uses one RP2040 PWM slice as a hardware edge counter on a **B-channel** pin
(datasheet section 4.5: with `DIVMODE = RISE` "the counter advances with each rising edge of the
PWM B pin", and "by allowing the slice to run for a fixed amount of time ... it's possible to
measure the duty cycle or frequency of an input signal"). It needs the sensor signal on a
second pin:

```
 sensor signal node ──[ 1 kΩ ]──┬──── GPIO5 (D5, PWM2 B)   second tap (counter)
 (after the pull-up, R1)        │
                              clamp as in R1
```

- [ ] **Decide whether you want this.** If yes, tell me, and I will build the counter, with host
      tests, before you spend time on wiring.
- [ ] Pick the pin: **GPIO5 (D5)** is my suggestion. GPIO9 and GPIO25 share one slice (PWM4) and
      GPIO27 shares slice 5 with the sensor input, so D5 avoids both.
- [ ] If yes: run a short wire from the sensor signal node (after the pull-up) to D5 through the
      1 kΩ, with the same clamp.
- [ ] Do **not** tap the pin A0 trace itself with a long stub: a branch off the pad adds
      loading and noise. Branch at the sensor connector.

## R6. Power, ground and protection (recommended)

- [ ] **Reverse polarity:** series P-channel FET (FQP47P06, on your parts list).
- [ ] **Transients:** TVS on the 12 V input (1.5KE36A, on your parts list).
- [ ] **5 V rails:** the OKI-78SR-5/1.5 or Mean Well RSD-30G-5 converter; scope the 12 V input
      during cranking (hardware-todo D3).
- [ ] **Ground reference:** the DAC signal is only meaningful against a ground. Tie the DAC
      ground to the **ECU's sensor ground**, not just to chassis, so a ground offset does not
      shift the ethanol reading. This is general practice and **[unverified] for your ECU**:
      confirm against your ECU's documentation (hardware-todo C1).
- [ ] **Shielding:** the DAC-to-ECU wire and the sensor wire run through an engine bay, so use
      twisted pair or shielded cable for both and keep them away from ignition wiring.

## R7. External watchdog (optional, not planned)

The RP2040's internal watchdog is a plain countdown timer (datasheet 4.7.1). A separate
supervisor chip would survive a fault that disables the whole RP2040, but it is extra hardware
and firmware, and the DAC already holds its last value through a reset (to be confirmed,
hardware-todo A8). Skip unless A8 shows the output glitches during a reset.

## Order to do it in

1. Bench checks first (hardware-todo A1 to A8), nothing connected to the ECU.
2. R1, then R2 (with the divider tested), then enable the readback.
3. R6 (power) before the Node goes in the car.
4. R3 and R4 once you know the ECU input (C1) and the DAC's power-on behavior (A1).
5. R5 only if you want the second path built.
