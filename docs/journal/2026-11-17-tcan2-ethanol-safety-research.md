# 2026-11-17: TCAN-2 ethanol safety, research and design record

Branch `TCAN-2-Ensure-ethanol-safeties` (contains the merged TCAN-1 CAN library).
**No firmware has been changed for TCAN-2 yet.** This entry records the audit, the research,
the owner's answers and what is still open. Tags: **[verified]** read in a primary source
this session, **[doc]** stated by a page, not cross-checked, **[unverified]** not sourced.
Related: [CAN decisions](2026-11-17-can-backbone-decisions.md),
[CAN hardware research](2026-11-17-can-hardware-research.md),
[owner to-do list](../user-todo/hardware-todo.md).

## Why this matters

The Ethanol Node gives the ECU the current ethanol content. A wrong value in either
direction is a hazard: reporting too little ethanol on E85 under-fuels the engine (lean),
and reporting too much on pump gas can over-advance timing and boost. Neither direction is
"the safe one" without knowing the tune.

## Owner facts and answers (round 1)

- The ECU is a **reflash ECU with a plain analog IO pin**. It has no sensor-fault logic
  that we know of. The Node processes the sensor's PWM signal and drives the ECU input, so
  **the fail-safe behavior is entirely ours to design**.
- The DAC (from the owner's DigiKey orders) is the DFRobot **DFR0971**, a 2-channel I2C
  module (GP8403). Its output goes to the ECU as **0 to 5 V**.
- Ethanol content is mapped 0 to 100 % onto 0.5 to 4.5 V (existing code constants).
- Hold time: owner asked whether 10 s should be longer, and asked what the output is at the
  first instant of power-up, before the first value is processed (the **boot window**).
- Last-known-good across power cycles: owner agreed the risk (refuel while off) makes sense
  and asked to research how modern ECUs handle it.
- Redundancy: yes to (a) software validation, (b) output readback, (c) second measurement
  method. Owner wants a hardware to-do list in a folder in the repo.
- Watchdog: use the best recommendation, backed by a verified source.
- CAN faults: report them, but **only extreme faults** should raise the "something is very
  wrong" error screen on the dash.
- Owner asked for decisions, research and this history to be journaled, and for a skill
  that makes unknown safety questions go to verified sources (see
  `.claude/skills/automotive-safety-research/`).

### Correction to my own round-1 advice

I told the owner to wire the DAC output to a free ADC pin for readback and they agreed.
**That was wrong.** The DAC outputs up to 5 V (or 10 V in its other range) and the RP2040
datasheet limits an input to IOVDD + 0.3 V, about 3.6 V (**[verified]**, datasheet p.616).
Readback needs a high-impedance resistor divider. The same limit applies to the sensor
pull-up (3.3 V, not 5 V).

## Audit of `Ethanol_Node/main.c` (before any TCAN-2 change)

| # | Hazard | Where |
|---|--------|-------|
| 1 | No signal timeout: `l_period` is never cleared, so a cut or shorted sensor wire holds the last value forever and presents it as live | `main.c` ~21-59, ISR `MeasurePwmSignal` |
| 2 | Divide by zero at boot: `1000000.0f / l_period` before the first edge (`l_period` is 0), then a float-to-int cast of infinity (undefined behavior) | ~91 |
| 3 | Every out-of-range value maps to 0 % ethanol (`... ? val_t : 0`), so any glitch becomes E0 on the ECU. Values 101-150 pass through and are clamped later | ~92 |
| 4 | No filtering: one edge-to-edge measurement is the output, with no median, rate limit or confirmation | ~91-93 |
| 5 | DAC task can hang silently: `i2c_write_blocking` has no timeout and its return value is ignored (also the range-select write) | ~117, ~136 |
| 6 | No watchdog. Stack-overflow and malloc-fail hooks spin with interrupts off, so the DAC holds a frozen value forever | ~28-43 |
| 7 | No timestamp or validity on the shared value (`atomic_uint`), so the DAC task cannot tell stale from fresh | ~26, ~121 |
| 8 | CAN reports faults as normal data: heartbeat status is always 0 and the ethanol message has no validity flag | `can/ethanol.c`, `can/trackcan.dbc` |

Hazards 2 and 3 mean the node can output **E0 on every cold boot** until the first valid
edge. The DAC's own power-on output is a separate, unknown boot-window risk (below).

## DFR0971 DAC: what the sources say

From DigiKey's product page (datasheet link is the DFRobot wiki page, not a PDF) and
DFRobot's wiki (**[doc]**):

- GP8403, **12-bit**, two channels, each **0 to 5 V or 0 to 10 V** (independently selectable).
- I2C addresses 0x58 to 0x5F by DIP switch; **0x5F is selectable** (matches `E_DAC_ADDR`).
- Supply 3.3 to 5 V; I2C high level 2.7 to 5 V; output error < 0.5 %, linearity error 0.1 %;
  supply current < 4 mA.

**Not stated on either page, so it must be measured with the oscilloscope:**
the output voltage and range at power-on (before any I2C write), whether a power-on value
can be stored, what happens if I2C stops, output current capability, and whether the output
is short-circuit protected (the spec table shows "-").

### DAC vendor library (DFRobot `DFRobot_GP8403`, **[doc]**, vendor source)

- Range register `0x01`: `0x00` = 0-5 V, `0x11` = 0-10 V. The existing code's
  `enable_5V = {0x01, 0x00}` is this write.
- Data: the 12-bit value is shifted left by 4 and sent low byte first. The existing
  `content_buf` bit layout matches. Data registers `0x02` (channel 0) and `0x04` (channel 1).
- **A `store()` function exists** ("Save the set voltage in the chip"). It is implemented by
  bit-banging a special sequence on SDA/SCL (not normal I2C writes), with a 10 ms wait.
  If it works as named, the safe output could be programmed into the DAC itself so it is
  present from power-up, before the MCU runs. Whether it persists the **range** as well as
  the value, and its write endurance, are not stated. **Needs measurement (to-do A1, A6).**
- **I2C pull-ups:** no source found says whether the DFR0971 has on-board pull-ups to its own
  supply. A sibling module (DFR1073) is listed with 4.7 kΩ pull-ups, which proves nothing
  about this one. If this module has them and is powered from 5 V, SDA/SCL idle at 5 V,
  above the RP2040's 3.6 V limit. **Measure first (to-do A5).** Note the module may need a
  5 V supply to reach a 5.0 V output; do not assume a 3.3 V supply works.

This is an engine-safety item: if the module powers up in its 0-10 V range, the first
instant could put more than 5 V on a 5 V ECU pin until the code selects 0-5 V.

No datasheet was added to `assets/`. The rule was DigiKey-hosted sheets only, and the URL
DigiKey lists is a web page; a guessed DigiKey-hosted PDF path returned 404.

## Research findings

### How modern ECUs handle a failed sensor

- **[doc] HP Tuners ECU parameter guide, "Flex Fuel Composition Sensor Default":**
  "Specifies the ethanol percentage that will be indicated by this module if diagnostics
  report that the flex fuel composition sensor is malfunctioning." It is a **fixed, tunable
  value**. The page gives no units, power-up behavior or last-known-value behavior. Related
  tunables listed in its sidebar: composition DTC min/max and frequency DTC min/max.
- **[verified] AUTOSAR Diagnostic Event Manager (R22-11):** per-event **counter-based**
  debounce (7.7.3.1, SWS_Dem_00414; fault detection counter -128..+127) and **time-based**
  debounce (7.7.3.2, SWS_Dem_00426), then **fault confirmation** after a configured number of
  failed cycles (7.7.4, SWS_Dem_00528). The specification text contains **no "substitute
  value"**, so the fallback value is left to the application.
- **[unverified]** Forum posts: ECUs flag a flex-fuel frequency below ~40 Hz or above ~160 Hz;
  one owner reports a GM falling back to pump-gas fueling. Leads only.
- **[weak] Patents** (fuel composition sensor rationality test; failed-sensor compensation in
  flexible-fuel vehicles) show these techniques exist; they do not show standard practice.
- No verified source was found for carrying a last-learned ethanol value across a power
  cycle, or for the power-up assumption. Do not assume one.

### Watchdog (Q5)

- **[verified] AUTOSAR Watchdog Manager (R23-11) 7.3.3:** "In the state
  WDGM_GLOBAL_STATUS_STOPPED, the Watchdog Manager module stops setting the trigger
  condition to Watchdog Interface. As a result, after the timeout of the hardware watchdog,
  it will cause a reset of the ECU." So the watchdog is **withheld on failure**, not fed
  unconditionally. 7.3.4 / SWS_WdgM_00133: optional **immediate MCU reset**
  (WdgMImmediateReset) as an independent shutdown path.
- **[verified] Three supervision types (1.3.1 to 1.3.3):** *alive* (periodic execution, counted
  alive indications per supervision reference cycle), *deadline* (time between a start and an
  end checkpoint), *logical* (control-flow errors; cites IEC 61508 / ISO 26262).
- **[verified] RP2040 datasheet:** 4.7.1 the watchdog is a **countdown timer** (no windowed
  mode found in the text); 4.7.3 **erratum RP2040-E1: the counter decrements twice per tick**,
  so program double the intended count (the Pico SDK accounts for this); 4.7.4 **eight 32-bit
  scratch registers survive soft resets** and are cleared by toggling RUN or cycling DVDD;
  2.12.3 a **brown-out** forces a power-on reset; p.616 input high maximum is IOVDD + 0.3 V.
- Pico SDK `watchdog_enable()`, `watchdog_caused_reboot()` and scratch use: header read in
  `external/pico-sdk` (**[verified]**).

### Standard practice (general, from earlier research)

NASA/JPL Power of Ten (bounded loops, no allocation after init, two assertions per function,
checked return values); bounded-time, deterministic fault reactions that latch and reset
deliberately; hold-last-value needs its own timeout; read the reset cause at startup.
These are **[doc]**; see Sources below.

### Sensor facts

- **[verified]** Continental flex-fuel sensor sheet (`assets/FFS01 (3).jpg`): open-collector
  output, needs a pull-up of **1.2 to 3 kΩ**, pull-up voltage **3.3 to 18 V**; never connect
  the output directly to Vcc. A 3.3 V pull-up is within this range and within the RP2040 limit.

## Hardware on hand (from the owner's orders; part numbers only)

Teensy 4.1 (no Ethernet); STM32H745I-DISCO; DFR0971 DAC; MCP2562FD transceivers (10); MCP3208
ADC; H11L1 optocouplers (4); 1.5KE36A TVS diodes (10); FQP47P06 P-channel MOSFETs (5, for
reverse-polarity protection); 1N5245B zener diodes (10; **15 V**, not a 3.3 V clamp); PESD1CAN
CAN TVS diodes (10); OKI-78SR-5/1.5 5 V converters (5); Mean Well RSD-30G-5; 1N5819 Schottky
diodes; assorted resistors (including 120 Ω, 1k, 2.2k, 4.7k, 10k, 22k) and capacitors; Deutsch
DT and AMP connectors; a Rigol DHO804 oscilloscope; a KA3005DE bench power supply. The
RP2040 CAN Feather was not seen in the parsed text of the four invoices (the extraction was
incomplete, so this does not mean it is missing).

## Decisions so far

- ECU input is a plain analog pin, driven by this Node. Fail-safe behavior is ours.
- Redundancy: software validation, **divider-conditioned** DAC readback, and a second
  independent frequency measurement.
- Watchdog: withhold-on-failure from a health-monitor task, plus a hardware reset; on a fatal
  error, set the safe output first, then reset.
- Persisted ethanol value: diagnostics only. Output across a real power-up uses the safe
  state until the first qualified reading. After a *watchdog* reset only, scratch registers may
  restore the last good value (seconds have passed, not a refuel).
- CAN: heartbeat status carries a fault code and reset cause; the ethanol message gets a
  validity flag. The dash's big error screen is reserved for extreme faults.

## Open (round 2 questions for the owner) - ANSWERED, see "Round 2" and "Implementation" below

1. Can the reflash tune treat an input outside 0.5 to 4.5 V as a fault and use a tuned
   default? If yes, the Node signals faults by going out of range and a bias resistor makes
   a dead Node read out of range. If no, the Node must output an in-range value and the
   owner and tuner must choose the least dangerous one.
2. Hold-time tiers (glitch hold, degraded hold, safe default), and whether to hold until
   key-off.
3. Recovery: latch faults until power cycle, or auto-recover after N good seconds.
4. Which faults count as extreme enough for the dash error screen.
5. Boot window: measured DAC power-on output, and how fast the first qualified reading
   arrives.

## Round 2: owner answers and ECU research

### Owner answers

- **ECU/tune:** 2002 WRX, **JDM EJ205 with AVCS**, tuned with **RomRaider / ECUFlash on a
  Carberry ROM**. The owner thinks the tune probably **cannot** treat an out-of-range ethanol
  voltage as a sensor fault. Design consequence: **the ECU can never be told the sensor
  failed, so the Node must always send an in-range value.** The driver is the only one who
  can be warned (dash screen).
- **Hold time:** owner delegated; recommendation is to **hold the last qualified value until
  key-off** (the ECU cannot detect the fault, so stepping to a fixed default mid-drive would
  itself be a sudden wrong value).
- **Recovery:** latch critical faults until power cycle. A plain sensor dropout may recover
  after the signal has been valid and stable for 10 s.
- **Dash error screen (extreme faults only):** a confirmed sensor loss, a watchdog reset while
  running, a DAC readback mismatch that is **large** (the owner said "big mismatch, not
  minuscule values"), and an over-voltage on the ECU line.
- **Boot qualification:** 10 agreeing sensor periods before leaving the boot value.

### ECU research

- **[doc] Cobb Subaru NexGen Flex Fuel Tuning Guide (page text read, truncated):** "If
  transitioning from 10% ethanol to 85% ethanol ~43% more fuel mass is required." Ethanol
  stoich 9.8:1 versus 14.7:1 for gasoline; about 63 % more fuel mass from 0 % to 100 %.
  "Higher ethanol allows more boost, more aggressive timing, or both." **Sensor failure,
  fallback and out-of-range behavior: not stated.**
- **[doc] Cobb flex-fuel kit (search summaries only, not read in full):** Subaru analog flex
  inputs are the TGV-L, TGV-R or rear O2 sensor inputs (0-5 V), with fuel scaling blended
  between low- and high-ethanol maps. **[unverified]** RomRaider forum posts say Carberry
  offers the same three inputs; one 2024 poster on a JDM ECU with Carberry had not found a
  good flex answer.
- **[doc] MaxxECU error 28 "Flex fuel sensor error":** with no sensor input the ECU goes
  into "default values", shown as **85 % ethanol and 0.0 degC**, and uses the E85 stoich AFR
  of 9.7. Whether it warns or limits engine operation: not stated.
- **[doc] HP Tuners:** a fixed, tunable default ethanol percentage on a sensor fault.
- **[unverified]** One GM owner reports a dead sensor falling back to pump-gas fueling (0 %).
- **Conclusion:** documented ECUs do **not** agree on a fallback. One falls to E85 (rich side),
  one uses a tuner-chosen fixed value, and an anecdote says E0. The fallback is a
  **deliberate tuning decision**, not a universal standard.
- **[derived from Cobb's 43 %]** Reporting E0 on E85 leaves the engine about **30 % short of
  fuel** (1/1.43); reporting E85 on E10 over-fuels by about **43 %**. The lean error is the
  one that damages engines under boost. This only covers fueling: if the tune also ties
  timing or boost to the ethanol value, an overstated reading can add timing and boost on
  pump gas.

### Open after round 2

1. **What the tune does with the ethanol value:** fuel only, or also timing, boost or AVCS?
   This decides whether a rich-biased default (overstate ethanol) is safe.
2. **Which ECU input carries it** (TGV-L, TGV-R or rear O2) and whether that input has its
   own diagnostics (check-engine code).
3. **Reserved fault voltage:** can the ROM's ethanol-input table axis take a breakpoint below
   0.5 V or above 4.5 V, so a fault voltage selects a tuned limp behavior inside the tune?
4. **Thresholds for a "big" DAC mismatch** (proposed below).
5. **The safe default value** for the boot window and for a sensor that was never qualified.

### Proposed DAC readback thresholds (pending bench measurements A1-A5)

Scale: 4 V for 100 % ethanol, so **0.04 V = 1 percentage point**. Proposed: **critical** at a
mismatch of 10 points (0.40 V) or more, sustained 500 ms; **warning only** at 3 points
(0.12 V) or more. Expected error budget: DAC error under 0.5 % (25 mV), divider resistor
tolerance about 1 %, plus ADC error; confirm with the scope.

## Implementation (coding started after the owner confirmed the design)

### Owner confirmations (final round)

- The tune uses ethanol for **fuel only** (no timing or boost). The Node's only job is to drive
  the DAC with the right 0-5 V value; the owner wires the DAC output to the ECU pin.
- **Default is E85** (rich side, cannot run lean). Sensor: the Continental ethanol-content sensor.
- **Critical alert in under 3 s** after key-on (implemented as 2.0 s, see below).
- Thresholds from round 2 accepted.

### What was built

| Piece | Where | Notes |
|-------|-------|-------|
| Signal conditioning | `Ethanol_Node/safety/ethanol_signal.*` | Median of 8 periods, 45-155 Hz window, lost after 100 ms, edge-count cross-check over 300 ms. Never divides by zero; first edge after boot yields no period |
| Safety state machine | `Ethanol_Node/safety/ethanol_safety.*` | Boot (E85) -> Qualifying (10 agreeing samples) -> Ok -> Holding -> Critical |
| DAC math | `Ethanol_Node/safety/ethanol_dac_math.*` | Percent to code (0 % = 410, 100 % = 3685, E85 = 3194), readback to millivolts through the 22k/10k divider |
| Alive supervision | `Ethanol_Node/safety/ethanol_health.*` | AUTOSAR WdgM style: reader and DAC tasks must check in; the feed stops after 2 failed 200 ms cycles |
| Scratch record | `Ethanol_Node/safety/ethanol_scratch.*` | Registers 0-3 only, magic + complement + checksum; restores last good after a watchdog reset |
| Hardware layer | `Ethanol_Node/main.c` | Boot order: reset cause and scratch, DAC range and first output BEFORE the scheduler, then watchdog, then tasks |
| CAN | `can/trackcan.dbc`, `can/ethanol.*`, `can/dash.*` | Ethanol_Data now 4 bytes (status byte); heartbeat status = fault code; Dash raises `ethanol_alert` |

### Behavior (all thresholds in `Ethanol_Node/safety/ethanol_config.h`)

- **Boot:** output is E85 (or the restored last good after a watchdog reset), flagged "default".
- **Qualify:** 10 consecutive samples within 2.0 points of each other (about 0.5 s of good signal).
- **Never qualified:** Critical 2.0 s after boot (clears if the sensor then qualifies).
- **Sensor lost:** under 2 s the value is held silently; from 2 s Holding (warning); after **10 s
  in a row with no valid sample**, Critical latched until power cycle. The value stays held.
- **Recovery:** a Holding dropout recovers only after 10 s of steady signal.
- **Noise and jumps:** changes up to 1 point follow; larger changes follow at 5 points per second;
  a steady different value for 5 s is accepted (a refuel mixing in).
- **DAC faults:** readback mismatch of 10+ points for 500 ms is Critical (3+ points: warning);
  more than 5.25 V for 200 ms is Critical; DAC writes failing for 1 s is Critical. All latched.
- **Watchdog reset:** restores the last good value, raises Critical (latched) and `ResetRecent`.
- **Status LED (GPIO13):** solid = Ok, 1 Hz blink = booting or qualifying, 2 Hz = Holding,
  10 Hz strobe = Critical. Added so there is a key-on indicator while the dash is a stub.

### Refinement of the earlier plan

I had proposed "on a fatal error, set the safe output, then reset". With the hold-last-good
policy that would be wrong: the DAC is a separate chip and keeps its last value through a reset,
and the new boot restores the last good value. So the fatal hooks record the reason and reset at
once; nothing steps the ECU to a default.

### Verification (what was actually run)

- **Host tests:** 128 checks for the Ethanol logic (built with address and undefined-behavior
  sanitizers and `-Wconversion`), 144 for the CAN layer and Dash. All pass.
- **Mutation checks:** 12 deliberate bugs (loss falls to default, rate limit removed, qualify
  after 1 sample, non-wrap-safe timers, E0 default, unclamped DAC code, watchdog never stops,
  implausible values clamped, checksum not verified, the critical-timer bug found during
  development, ...). The tests caught every one.
- **Bug found by the tests during development:** the 10 s critical timer started at the first
  lost sample, so a short dropout that needed 10 s to recover always latched Critical. It now
  measures continuous time without a valid sample.
- **Bug found in review, before any hardware:** the first version treated `watchdog_caused_reboot()`
  as "our watchdog timed out". On the RP2040 that is also true after a UF2 flash or a
  `watchdog_reboot()`, so every flash would have booted into a latched Critical and restored a
  stale value. Now only `watchdog_enable_caused_reboot()` (the marker `watchdog_enable()` leaves,
  which flashing clears) counts; the decision is a pure function (`ethanol_boot.*`) with a
  host-tested truth table, and the fatal hooks now spin and let the real watchdog time out instead
  of calling `watchdog_reboot()`. A mutation that treats every boot as a fault is caught.
- **Also fixed in review:** the edge-count window was 250 ms, equal to its minimum, so scheduling
  jitter skipped the cross-check about half the time (now 300 ms with margin), and the last-edge
  time is 64-bit so the age cannot wrap into a false "alive" after about 71 minutes of silence.
- **Not covered by tests:** `main.c` itself (the interrupt, the edge ring and window, the boot
  order, the DAC task, the LED) is compiled only. Several bugs were in exactly that part.
- **Builds:** Ethanol firmware (default and readback-ON variants) and the Zephyr DataLogger compile
  and link with no warnings from the new code. **Nothing was flashed or run on hardware.**
- **SDK facts read from `external/pico-sdk`:** `watchdog_enable()` already applies the erratum
  RP2040-E1 factor of 2 (do not double again; maximum 8388 ms); the SDK uses scratch register 4
  and the bootrom 4-7, so only 0-3 are used.

### Deliberately not built

- **A second independent hardware measurement (PIO).** The edge-count cross-check is algorithmic
  (period median against counted edges), not an independent hardware path. A PIO program could not
  be tested without hardware, and a bug there is worse than not having it. Deferred; say so.
- **DAC channel 1 (temperature to the ECU)** is not driven. Temperature goes out on CAN only.
- **DataLogger parsing of the new status fields:** it records raw frames, so it already logs them.

### Unverified and measure-first

- The sensor's start-up time is not published (the official Continental PDF is a two-page
  marketing sheet). The 2.0 s never-qualified limit must exceed it or every key-on raises a false
  Critical (to-do A7).
- That the DAC holds its output through an MCU reset, that a repeated range write every 2 s does
  not disturb VOUT, and what the I2C pins do while the RP2040 reboots (to-do A2, A3, A8).
- The sensor frequency mapping (50 Hz = E0, 150 Hz = E100) and the temperature formula come from
  ECU-maker documentation and resellers, not a Continental specification.
- **All nodes must be flashed together.** Ethanol_Data grew from 3 to 4 bytes, and the CAN core
  drops frames whose length does not match, so an old DataLogger or Dash would count every ethanol
  frame as bad.

## Sources

Tier 1 and 2 (read this session):

- AUTOSAR Watchdog Manager R23-11: https://www.autosar.org/fileadmin/standards/R23-11/CP/AUTOSAR_CP_SWS_WatchdogManager.pdf
- AUTOSAR Diagnostic Event Manager R22-11: https://www.autosar.org/fileadmin/standards/R22-11/CP/AUTOSAR_SWS_DiagnosticEventManager.pdf
- RP2040 datasheet (final URL after two redirects from `datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf`): https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf
- HP Tuners ECU parameter guide, Flex Fuel Composition Sensor Default: https://docs.hptuners.com/CORE_ECU_Parameter_Guide/Fuel/Flex_Fuel/Tunable_Parameters/Flex_Fuel_Composition_Sensor_Default/
- DigiKey DFR0971 product page: https://www.digikey.com/en/products/detail/dfrobot/DFR0971/18069239
- DFRobot DFR0971 wiki (the datasheet link DigiKey lists): https://wiki.dfrobot.com/dfr0971/
- DFRobot GP8403 library: https://github.com/DFRobot/DFRobot_GP8403
- Cobb Subaru NexGen Flex Fuel Tuning Guide: https://cobbtuning.atlassian.net/wiki/spaces/PRS/pages/2864185460/Subaru+NexGen+Flex+Fuel+Tuning+Guide
- MaxxECU error 28, flex fuel sensor error: https://maxxecu.com/webhelp/information-error_codes-28.html
- Continental OE flex fuel sensor application sheet (two pages, no electrical specification): https://www.continental-aftermarket.com:443/media/3213/oe_ffs_conti_2023.pdf
- Continental flex-fuel sensor electrical interface sheet: `assets/FFS01 (3).jpg` (in this repo)

General practice (**[doc]**):

- Power of Ten: https://en.wikipedia.org/wiki/The_Power_of_10:_Rules_for_Developing_Safety-Critical_Code and Holzmann's paper https://www.cs.otago.ac.nz/cosc345/resources/nasa-10-rules.pdf
- Watchdog practice: https://interrupt.memfault.com/blog/firmware-watchdog-best-practices , https://mcuoneclipse.com/2023/03/26/using-a-watchdog-timer-with-an-rtos/ , https://www.analog.com/en/resources/technical-articles/the-basics-of-windowed-watchdogs.html
- Sensor plausibility and safety mechanisms: https://www.motor.com/2026/02/sensor-plausibility-checks-diagnostics/ , https://notebook-dg9.pages.dev/kb/embedded/asil/safety-mechanisms/

Weak evidence (patents, **leads only**):

- Fuel composition sensor rationality test: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6389874
- Failed sensor in a flexible fueled vehicle: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5947089
- Method for checking analog sensors: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6421625

Forum posts and course pages (**[unverified]**, leads only):

- Cobb flex fuel overview and kit: https://cobbtuning.atlassian.net/wiki/x/IwDCww
- Evans Performance Academy, flex fuel on a Carberry ROM (course video, not read): https://evansperformanceacademy.vhx.tv/videos/ecu-flash-subaru-pt-25-ecu-flash-subaru-pt-25
- Haltech flex fuel (50 Hz = 0 %, 150 Hz = 100 % default calibration): https://support.haltech.com/portal/en/kb/articles/flex-fuel-composition
- https://forums.linkecu.com/topic/10855-link-fury-fueling-issues-we85/ (40-160 Hz fault thresholds)
- https://www.corvetteforum.com/forums/c6-corvette-general-discussion/4406085-flex-fuel-sensor-question.html (GM fallback anecdote)
