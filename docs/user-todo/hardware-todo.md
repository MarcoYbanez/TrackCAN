# Hardware to-do

Tools you own (per your orders): Rigol DHO804 oscilloscope, KA3005DE bench supply.

See also [hardware-redundancy.md](hardware-redundancy.md) for the redundancy wiring (readback
divider, second sensor tap, voltage limits) in one place.

Order of work: **A (bench) first, then B (wiring), answer C, and only then D (in the car).**
Do not connect the ECU until A is done and you have told Claude the results.

## A. Bench measurements (nothing connected to the ECU or the car)

Load the DAC output with a 10 kΩ resistor to ground, so there is no ECU on the line.

- [ ] **A1. DAC power-on output (most important).** Power the DFR0971 with **no I2C traffic
      at all** and capture VOUT0 on the scope, triggered on the supply rising edge, for 2 s.
      Record the voltage, the time to settle, and whether it is in the 0-5 V or 0-10 V
      range. Repeat at each supply voltage you might use (3.3 V and 5 V). *Why: DFRobot's page
      does not state the power-on output or range. A 0-10 V start could put more than 5 V on
      a 5 V ECU pin.*
- [ ] **A2. DAC behavior when I2C stops.** Write a mid value, then disconnect SDA and SCL.
      Does VOUT hold, drift or reset? *Why: decides whether a hung microcontroller leaves a
      frozen signal on the ECU.*
- [ ] **A3. DAC range and full scale.** Confirm the 0-5 V range write is accepted and that
      VOUT never exceeds 5.0 V at any step of start-up. Also check whether the DAC reaches
      4.5 V and 5.0 V at the supply you intend to use. *Why: a 3.3 V supply may not reach
      full scale, which would read as a lower ethanol content.* Also confirm that the firmware's
      range write, repeated every 2 s, does not disturb VOUT.
- [ ] **A4. CAN bit timing.** With two nodes talking, a bit should be **2.0 µs** (500 kbit/s)
      on CAN_H/CAN_L, with a sample point near 87.5 %. *Why: the Feather's controller timing
      was derived by hand, not tested.*
- [ ] **A5. DAC I2C idle voltage.** Power the DAC at the supply you intend to use and measure
      SDA and SCL idle voltage **before connecting it to the Feather**. *Why: DFRobot modules
      often have pull-ups to their own supply. If this one does and runs from 5 V, SDA/SCL sit
      at 5 V, above the RP2040's 3.6 V limit (datasheet p.616). If it is above 3.3 V, use an
      I2C level shifter (not a plain divider) or remove the module's pull-ups.*
- [ ] **A6. (Optional) DAC `store()` test.** DFRobot's library has a `store()` that is meant to
      save the output in the chip so it holds at power-up. If A1 shows an unsafe power-on
      output, try it: set a value, store, power-cycle, repeat A1. Check whether the **range**
      also persists. *Why: this could put the safe voltage on the ECU line from the first
      millisecond. Its write endurance is not stated, so store only a few times.*

- [ ] **A7. Sensor start-up time.** Power the flex-fuel sensor from the KA3005DE at 12 V, with a
      2.2 kΩ pull-up to 3.3 V on its output, and capture the output on the scope. Record the time
      from 12 V on to the first valid frequency, the frequency, and the pulse width. *Why: no
      source publishes it. The firmware raises Critical 2.0 s after boot if the sensor has not
      qualified; if the sensor needs longer, every key-on would show a false alarm (change
      `ETH_NEVER_QUALIFIED_CRITICAL_MS`).*
- [ ] **A8. DAC through a reset.** Scope VOUT while the RP2040 is reset (press RESET, or let the
      watchdog fire). Does VOUT hold, glitch or drop? Also watch SDA/SCL for glitches while the
      pins reinitialize. *Why: the design assumes the DAC keeps its last value while the
      microcontroller reboots, and that is not stated anywhere.*

## B. Wiring and protection

- [ ] **B1. DAC readback divider (this replaces my earlier "one wire to an ADC pin" advice,
      which was wrong).** The DAC can output up to 5 V (10 V in its other range) but an RP2040
      pin allows at most 3.6 V (IOVDD + 0.3 V, RP2040 datasheet p.616). *Proposed:* 22 kΩ from
      the DAC output to the ADC pin and 10 kΩ from the pin to ground (ratio 0.3125: 5 V reads
      1.56 V, 10 V reads 3.13 V). Use a free ADC pin (GPIO27, 28 or 29; GPIO26 is the PWM
      input). Confirm on the bench with the scope.
- [ ] **B1b. Turn the readback on.** After B1 is fitted and checked, rebuild the Ethanol firmware
      with `-DETH_DAC_READBACK=ON` (it is OFF by default, because an unwired pin would raise a
      false Critical on every start). Check the readback pin is GPIO27 (`ETH_READBACK_ADC_GPIO`).
- [ ] **B2. Sensor input pull-up.** Pull the sensor output up to **3.3 V** (not 5 V or 12 V)
      through about **2.2 kΩ** (the sensor sheet allows 1.2 to 3 kΩ and 3.3 to 18 V). Add a
      series resistor and a clamp so a short to 12 V cannot reach the pin.
- [ ] **B3. Do not use the 1N5245B as a pin clamp.** It is a **15 V** zener.
- [ ] **B4. Power input protection.** Series reverse-polarity protection (FQP47P06 P-FET), a
      TVS on 12 V (1.5KE36A), then the 5 V converter. Check the layout before powering.
- [ ] **B5. CAN bus wiring.** Teensy CAN3 TX = pin 31, RX = pin 30 into the MCP2562FD
      (check the VIO pin level in its datasheet, `assets/datasheets/digital_dash/`). Add the
      PESD1CAN TVS on CAN_H/CAN_L.
- [ ] **B6. Termination.** Exactly **two** 120 Ω terminators, one at each physical end of the
      bus. If the RP2040 CAN Feather is not at an end, **cut its `TERM` jumper**.
- [ ] **B7. Feather STANDBY pin polarity.** The firmware drives GPIO16 low to run the
      transceiver; confirm on the bench that CAN works with that and not with it high.
- [ ] **B8. DAC address.** Set the DIP switches to A0 = A1 = A2 = 1 (address 0x5F).
- [ ] **B9. Defi sensor tap (DataLogger).** Check that the ADC input does not load the sensor
      signal enough to shift the gauge reading, and that the sensor voltage cannot exceed
      the MCP3208 reference voltage.
- [ ] **B10. ECU input line: bias resistor.** Needs your answers to C1 and C2. Fit a bias
      resistor, if the design calls for one, so the pin has a defined voltage when the Node
      is dead.
- [ ] **B11. Hardware limit on the ECU line.** Add a way (for example a clamp or divider) so
      that **more than 5 V can never reach the ECU**, even if the firmware or the range write
      fails. This depends on C1: a series resistor forms a divider with the ECU's input
      impedance, so choose the values after you know it.

## C. Facts only you know (tell Claude)

- [ ] **C1. ECU analog input (answered in part: RomRaider / Carberry ROM, JDM EJ205).** Which
      ECU input carries the ethanol signal (TGV-L, TGV-R or rear O2), its input impedance,
      and whether that input has its own diagnostics that could set a check-engine code.
- [ ] **C2. Reserved fault voltage.** In RomRaider, can the ethanol-input table axis take a
      breakpoint **below 0.5 V or above 4.5 V**? If yes, the Node can send a fault voltage and
      the tune itself can select limp fuel, timing and boost.
- [ ] **C3. What does the tune do with the ethanol value:** fuel only, or also **timing,
      boost or AVCS**? This decides whether overstating ethanol (rich) is the safer error.
      Reporting E0 on E85 leaves the engine about 30 % short of fuel; reporting E85 on E10
      over-fuels about 43 % (derived from Cobb's 43 % figure). Pick the safe default value
      with your tuning knowledge, not Claude's.
- [ ] **C4. Defi sensor transfer curves** (volts or ohms to bar and degC) for the DataLogger.
      Until then the DataLogger sends zeros on the bus.
- [ ] **C5. Where the RP2040 CAN Feather sits on the bus** (end or middle), for B6.
- [ ] **C6. Dash:** the STM32H745I-DISCO board and its toolchain choice, so the Dash port can
      move past the stub.

## D. In-car checks (only after A and B pass and C is answered)

- [ ] **D1. ECU pin with the Node off.** With the ECU powered and the Ethanol Node completely
      unpowered, measure the voltage at the ECU input. *Why: decides whether the bias
      resistor in B10 is needed, and what a dead Node looks like to the ECU.*
- [ ] **D2. Flex-fuel sensor signal at the Node pin.** Capture frequency, amplitude (must stay
      at or below 3.3 V) and noise, ideally with the engine running, to see ignition noise.
- [ ] **D3. Supply at the Node during cranking.** Capture the 12 V rail at the Node. *Why: the
      5 V converters must hold up through a cold crank dip and load dump transients.*
- [ ] **D4. Key-on alert timing.** Turn the key on and watch the Feather's red LED (GPIO13): solid
      = Ok, 1 Hz blink = still booting or qualifying, 2 Hz = Holding, 10 Hz strobe = Critical.
      Note how long it takes to turn solid. Then test two failures and expect different results:
      (1) **Key on with the sensor unplugged:** Critical (strobe) about 2 s after power-up.
      (2) **Unplug the sensor while running:** a 2 Hz blink (Holding) after 2 s, and the Critical
      strobe only after 10 s with no signal. The value is held the whole time. The dash is still a
      stub, so the LED is the only key-on indicator today.

## E. Flashing

- [ ] **E1. Flash all nodes together.** The Ethanol message grew from 3 to 4 bytes. An old
      DataLogger or Dash counts every new ethanol frame as bad and ignores it.

## DataLogger SD card bench test (TCAN-17)

The SD logger builds and its logic is host tested, but it has **never run on the Teensy**. Claude cannot do this part.

- [ ] A **FAT32** microSD card (32 GB or less is the safe choice). Note its brand and size. It will not be formatted by the logger; if the card
      is exFAT or blank the LED stays steady on.
- [ ] Insert the card, plug the Teensy into the Mac by USB, run `! scripts/datalogger_build_flash.sh all` and press the Teensy's program
      button when it asks.
- [ ] Watch the board LED: **slow blink** = mounted and logging, **fast blink** = no card answered, **steady on** = card or file system error.
      (A USB-serial adapter on Teensy pins 0 and 1 at 115200 baud shows the same in words, if you have one.)
- [ ] Let it run about a minute, then **power off by unplugging USB** (that is the power-cut test), take the card to the Mac, and open `LOG0001.CSV`.
      Tell Claude what is in it: the header, then TX lines for ids 200, 201 and 4xx heartbeats, a NOTE line for the start.
- [ ] Reinsert the card, power on again: a **new** file (`LOG0002.CSV`) must appear and `LOG0001.CSV` must be untouched.
- [ ] Pull the card out while it is logging: the LED must go to the fast blink and the CAN side must keep running; put it back and logging
      must resume in a new file within a few seconds.

## DataLogger PCB (TCAN-26 / TCAN-27)

The board is designed and the Gerbers exist (`PCB/Datalogger/fab/`), but **nothing has been ordered or built**. Details in
[the journal](../journal/2026-11-23-tcan26-27-datalogger-pcb.md).

Before ordering:

- [ ] **Open the Gerbers in the fab's online preview** (or a Gerber viewer) and compare against `PCB/Datalogger/Datalogger_schematic.png` and the board in KiCad.
- [ ] **Check the Teensy row spacing** with calipers: the pin rows should be **15.24 mm** (0.6 in) apart, centre to centre. *Why: I measured it from a photo.*
- [ ] **Find the OKI-78SR-5/1.5-W36-C datasheet** (Murata) and tell Claude its **absolute-maximum input voltage**. *Why: decides the TVS. D3 is drawn as 1.5KE24A.*
- [ ] **Order the parts in `PCB/Datalogger/to_buy.csv`** and confirm each part number on DigiKey (several are marked unconfirmed).
- [ ] Decide on a **fuse** for the 12V input (none is drawn).

Before powering a built board (use the KA3005DE with the current limit set low, nothing in the sockets):

- [ ] Without the Teensy and MCP3208 fitted: 12 V in on J1 gives **5 V** at the Teensy VIN socket and the regulator output; swap the supply leads and confirm
      the board draws **no current** (reverse-polarity protection).
- [ ] Measure **3.3 V** only after the Teensy is fitted; each sensor input at 5 V in should read about **3.2 V** at the MCP3208 pin.
- [ ] **Teensy USB and board power:** cut the VUSB-VIN pad pair and solder a 1N5819 across it (USB side is the anode) *before* connecting USB with the board powered.
      Until then, unplug the 12V connector first. *Why: PJRC says power should not be applied to VIN while USB is connected; the diode is my suggestion, not PJRC's.*
- [ ] J3 console: adapter TX to pin 2 (Teensy RX), adapter RX to pin 3 (Teensy TX), **3.3 V logic only**, 115200 baud.
- [ ] On a bench CAN bus with a second node: bit time **2.0 us** (500 kbit/s) on CAN_H/CAN_L; set JP1 only on an end-of-bus node.
