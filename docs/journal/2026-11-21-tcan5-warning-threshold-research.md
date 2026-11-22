# Dash warning thresholds: what the sources say (TCAN-5 research)

Date: 2026-11-21. Method: `automotive-safety-research` skill. Nothing in `docs/journal/` settled these, so each
claim below was looked up and the source text read. Tags: **[verified]** text read in a Tier 1-2 source this
session. **[doc]** stated by a page, not cross-checked. **[unverified]** not sourced, a lead only.

## Where the sources come from, and their limits

- **Subaru factory service manual, "2004 Impreza manuals" set** (book stamp `W1860BE`, printed January 2003, covers
  the WRX `H4DOTC` and STi engines). Hosted as PDFs on a **third-party mirror**,
  `https://raig0.planet.ee/subaru/Manuals/2004%20Impreza%20manuals/` (index: `sti_dir.htm`). I extracted the text
  myself from the downloaded PDFs. The mirror is not Subaru, so the pages cannot be checked against the original.
- **It is the 2004 model year book, not the owner's 2002 JDM EJ205.** The US WRX EJ205 and the JDM EJ205 share the
  engine family, but fan temperatures, switch parts and gauge scales can differ by market and year.
- **The ECU is reflashed (RomRaider / Carberry).** Fan on/off temperatures live in the ROM, so the factory table
  below is the stock behaviour, not necessarily what this car does. The owner can read the real values in
  RomRaider.
- Blocked or unreachable, not bypassed: `manualowl.com` (HTTP 403), `manuals.autorepairshq.com` (connection reset).

## Findings

### Oil pressure (engine) [verified]
- Factory spec, STi page `ME(STi)-26`, "6. Engine Oil Pressure, A: INSPECTION", read from the PDF text:
  "98 kPa (1.0 kgf/cm2, 14 psi) or more at 800 rpm" and "294 kPa (3.0 kgf/cm2, 43 psi) or more at 5,000 rpm".
  Note: "The specified data is based on an engine oil temperature of 80°C (176°F)."
- The same two figures appear on the `H4DOTC` (WRX turbo) page, printed `ME(H4DOTC)-38`
  (`https://www.manualsdir.com/manuals/148812/subaru-impreza-2004.html?page=39`, read by a fetch agent).
- Warning light, combination meter page `IDI-5`, item (7): "This light illuminates when the engine oil pressure
  decreases below 14.7 kPa (0.15 kgf/cm2, 2.1 psi)."
- The spec is a **minimum that rises with rpm**. One fixed limit cannot be right at both idle and 5,000 rpm.
- Absence: the manual gives no maximum pressure and no warm/hot idle figure other than the 800 rpm row.

### Coolant temperature [verified for the fan and gauge, not for an overheat limit]
- Gauge, `IDI-14`, "G: WATER TEMPERATURE GAUGE": "When the coolant is at a normal operating temperature of approx.
  70 to 100°C (158 to 212°F), the gauge's needle stays in the middle of the indication range". The drawing marks a
  red zone above that (scale 50, 65, 100, 120).
- Radiator fan, `CO(H4DOTC)-10`, "B: FUNCTION" (turbo, no A/C): fans OFF "Lower than 90°C (194°F)" and between 90 and
  95°C; "Higher than 96°C (205°F)": **Low speed** below 19 km/h and **High speed** at any higher speed. The ECM
  decides from the coolant sensor. (The non-turbo `H4SO` page, `CO(H4SO)-8`, differs: ON at 96 to 99°C below 43 mph,
  always ON above 100°C.)
- Absence: no page gave a temperature at which the engine is in danger, nor a thermostat opening temperature (the
  `H4DOTC` thermostat page `CO(H4DOTC)-7` has only the description). The 156°F and 172°F figures seen for aftermarket
  thermostats are **[unverified]** (vendor listings).

### Fuel level [verified for the light, [unverified] for the tank size]
- Combination meter `IDI-5`, item (10): "This light illuminates when the quantity of the fuel remaining in the tank
  has decreased to 9 liters (2.4 US gal, 2.0 Imp gal) or less."
- `IDI-16`, "2. OPERATION": the ECM turns the light on "if a resistance value corresponding to the critical fuel
  level is detected for a certain time while driving" (so a sloshing sub-tank does not trigger it).
- **[doc]** 2006 WRX owner's manual, printed page 3-8 (`manualsdir.com/manuals/746554/subaru-2006-impreza-wrx.html?page=121`):
  the light "does not go out unless the tank is replenished up to an internal fuel quantity of approximately 4.0 US
  gal (15 liters, 3.3 Imp gal)". That is a built-in hysteresis of about 6 L.
- **[unverified]** Tank capacity 60 L (an Australian 2002 WRX listing). The dash shows percent, so 9 L = 15 % and
  15 L = 25 % only if the tank is 60 L. Not found in any manual page read.

### Battery and charging [verified for the over-voltage lamp only]
- Generator, `SC(H4SO)-3`, "2) Diagnosis warning": the charge lamp lights when the generator has "Output voltage
  greater than (approx) 16 volts", or no voltage is generated, or terminal B or S is disconnected. `SC(H4DOTC)-2`
  says the DOHC turbo specifications are the same as the SOHC ones.
- Combination meter `IDI-5`, item (6): the charge light illuminates "when a fault occurs in the charging system
  while the engine is running".
- Absence: this manual set contains **no regulated-voltage figure** and no low-voltage threshold. `H4DOTC` and
  `STi` starting/charging pages only point to the `H4SO` pages, which hold no number either.
- **[unverified]** 14.1 to 14.8 V regulated (a 2010 WRX 2.5 workshop entry and a 1.8 L hardtop entry, neither a
  2002 WRX). Resting battery charts (about 12.4 V = 75 %, 12.2 V = 50 %) come from battery vendor blogs: leads only.

### Oil temperature [no verified source]
- Subaru's manual gives warnings for **ATF** (150°C) and **rear differential** oil (150°C), not engine oil. No
  engine-oil temperature limit was found.
- **[unverified]** A tuning blog (Crawford Performance) says 194 to 212°F is normal, 225 to 235°F on track, cool down
  near 250°F. A lead only.

## Proposed values for the owner to confirm

Values are stored in tenths (`dash_config_t`). Nothing below is in the firmware yet; limits stay unset until the
owner approves. "n/a" means no sourced value.

| Box | Limit | Proposed | Basis | Confidence |
|---|---|---|---|---|
| Oil pressure | alert low | 14 psi at 800 rpm rising to 43 psi at 5,000 rpm (minimum from the manual) | `ME(STi)-26` | high for the numbers; the rpm-dependent form is a design choice |
| Oil pressure | emergency | 2.1 psi (the car's own warning switch) | `IDI-5` (7) | high |
| Coolant | warning high | 212°F (100°C), top of the normal range | `IDI-14` | medium |
| Coolant | alert high | n/a | no source | owner decision |
| Fuel | warning low | 15 % (9 L) if the tank is 60 L, clear at 25 % | `IDI-5` (10), owner manual p3-8 | high for litres, tank size unconfirmed |
| Battery | alert high | 16.0 V | `SC(H4SO)-3` | medium (the car's own lamp, "approx") |
| Battery | low / warning high | n/a | no source | owner decision |
| Oil temp | warning / alert | n/a | no source | owner decision |

## Questions for the owner

1. **Oil pressure:** use a limit that follows rpm (recommended, the data is already in the snapshot), or one fixed
   number?
2. **Coolant alert:** what temperature do you want as red? (The factory sources stop at 100°C normal.)
3. **Fuel:** confirm the tank is 60 L (fill from the low light and divide), so 15 % means 9 L.
4. **Battery low and oil temp:** pick values from your own experience, or from a source you trust. This session found
   none.
5. In RomRaider, read your ROM's radiator fan on/off temperatures and compare with the table above.

## Not done

- Nothing was read from the 2002 JDM service manual (not available). The JDM and US ECU fan tables were not compared.
- No threshold was written to firmware. TCAN-5 phase 1 needs the answers above first.

## Owner decisions (2026-11-21, after reading the above)

| Box | Decision | Implemented as |
|---|---|---|
| Oil pressure | One fixed limit: red below **15 psi** (not rpm-dependent) | `DASH_OIL_PRESSURE_LOW_X10 149` (red at 14.9 psi or less), clears at 16.0 psi |
| Coolant | Red at **220 F** | `DASH_COOLANT_HIGH_X10 2200`, clears below 218 F |
| Oil temperature | Red at **235 F** (the figure motorsport forums use; no factory source) | `DASH_OIL_TEMP_HIGH_X10 2350`, clears below 233 F |
| Battery | The car's own charge lamp, read through an opto-coupler, drives the BAT lamp. No voltage limit on the dash. | New CAN bit `ChargeWarning` (DataLogger_Lights byte 1 bit 0), message widened 1 to 2 bytes |
| Fuel | The car's own low-fuel lamp (opto-coupler) turns the fuel box red. The level is the car's sensor, shown as percent. Tank 15.9 US gal (60.2 L), so the factory 9 L light is about 15 %. | Existing bit `FuelLow`; no percent limit on top, because the lamp already has the ECM's debounce and the factory 4.0 gal refill hysteresis |

The hysteresis margins (1 psi, 2 F, 2 F) are my choice, not sourced.

Known consequences to watch on the car:
- **Oil pressure reads 0 with the key on and the engine stopped, so the box is red until the engine runs.** The car's own
  oil lamp does the same. Gate it on rpm later if that is unwanted.
- **A hot idle at the factory minimum (14 psi at 800 rpm) is below the 15 psi limit.** A healthy engine near the spec
  minimum would show red at hot idle. Watch the real idle pressure and adjust `DASH_OIL_PRESSURE_LOW_X10`.
- The lights message is now 2 bytes. A DataLogger still sending the old 1-byte frame is rejected by the new Dash (lamps
  stay dim, fail closed); flash both. The dash layout tag moved to version 2.
- The DataLogger must set `ChargeWarning` and `FuelLow` from the opto-coupler inputs (TCAN-4). Nothing sets them yet.
