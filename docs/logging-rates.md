# Recommended logging rates (the logging spec)

**Source:** supplied by the owner on 2026-11-23 as a screenshot titled "Recommended logging rates". The origin of the numbers is not recorded and they have
not been independently verified. They are logging rates, not safety values; the owner treats them as the requirement. Hz = samples per second, per channel.

| Sensor | Daily driving | High resolution |
|---|---|---|
| Engine RPM | 10 Hz | 100 Hz |
| Boost / MAP | 10 Hz | 100 Hz |
| Throttle position | 10 Hz | 100 Hz |
| AFR / wideband | 10 Hz | 50 to 100 Hz |
| Oil pressure | 10 Hz | 50 to 100 Hz |
| Fuel pressure | 10 Hz | 50 to 100 Hz |
| Coolant temperature | 1 Hz | 10 Hz |
| Oil temperature | 1 Hz | 10 Hz |
| Ethanol percentage | 1 Hz | 1 to 5 Hz |
| DCCD lock command | 10 Hz | 50 Hz |
| Accelerometer / G-force | 10 Hz | 100 to 200 Hz |
| GPS | 1 to 10 Hz | 10 to 20 Hz |

## How the table is applied (owner decision, 2026-11-23: "only apply what we currently have"; TCAN-28)

The logger records raw CAN frames, so a rate in the table is realised in two places: **how often the sender transmits** (the cycle time in `can/trackcan.dbc`,
which is the high-resolution rate, because high-res logs every frame) and **how the daily mode reduces the frames** (an interval per message,
`seg_daily_interval_ms()` in `DataLogger_Node/src/logger/logger_segments.c`, keyed on the contract's own ID macros). Several signals share one frame, so a frame takes the
rate of its fastest signal. `DataLogger_Analog` was split for exactly that reason.

| Channel | CAN message (ID) | High-res (send rate) | Daily (logged every) | Status |
|---|---|---|---|---|
| Engine RPM (and speed) | Dash_Engine (0x100) | **20 ms = 50 Hz, unchanged** (target 100 Hz) | 100 ms = 10 Hz | daily applied; the 100 Hz send rate is **deferred to TCAN-7** (see below) |
| Oil pressure | DataLogger_OilPressure (0x203, new) | 20 ms = **50 Hz** | 100 ms = 10 Hz | applied (the low end of 50 to 100) |
| Oil temperature, coolant temperature, fuel level | DataLogger_Analog (0x200, now 6 bytes) | 100 ms = **10 Hz** | 1000 ms = 1 Hz | applied (fuel level is not in the table: it rides with the slow signals) |
| Ethanol percentage | Ethanol_Data (0x300) | **unchanged, 100 ms = 10 Hz** | 1000 ms = 1 Hz | daily applied; the send rate is deliberately NOT changed (see below) |
| Lights, status, button, heartbeats | 0x201, 0x202, 0x400, 0x701 to 0x703 | unchanged | 1000 ms = 1 Hz (default) | not in the table: default |

### Measured on the hardware (bench: the real DataLogger CAN code in loopback, a played engine stream at 50 Hz), per file, frames per second

| Message | Daily file | Target | High-res file | Target |
|---|---|---|---|---|
| Oil pressure (0x203) | 9.3 | 10 | **50.0** | 50 to 100 |
| Oil temperature, coolant, fuel (0x200) | 0.9 to 1.0 | 1 | **10.0** | 10 |
| Engine (0x100, played at 50 Hz) | 9.6 | 10 | 49 | 100 (not yet sendable) |
| Lights / status / heartbeat | 0.9 / 0.9 / 0.6 | 1 | 10 / 5 / 1 | not in the table |

The daily reduction keeps a frame only when **at least** the interval has passed since the last kept one, so it never delivers more than the target and, with the real jitter of
the senders, about 90 to 95% of it (9.3 instead of 10). That is deliberate: it is a simple guarantee a checker can verify (`scripts/check_datalogger_log.py` fails a daily log that
holds a message more densely than its interval).

### Why some table rates are NOT applied yet

* **Engine RPM at 100 Hz (needs a 10 ms cycle).** The Dash's CAN core sends once per M4 loop pass, and that loop runs every 20 ms and is supervised by a watchdog whose check-in window is
  tuned to 20 ms (a 10 ms loop would exceed its upper limit). A DBC that says 10 ms while the sender sends every 20 ms would also make receivers see the message as stale (3 cycles = 30 ms).
  The RPM source itself does not exist until TCAN-7, so this is built there, together with its sender loop. **Note for TCAN-7:** the live Dash image today never sets the engine values, so
  it transmits `Dash_Engine` with RPM 0 and speed 0 every 20 ms on a real bus.
* **Ethanol at 1 to 5 Hz.** The ethanol message's cycle time also sets how fast the Dash detects a lost sensor (stale after 3 cycles). Slowing it to 5 Hz would double the time before the Dash
  raises its ethanol alert, a safety-relevant change that needs its own decision. High-res logs every ethanol frame, which is more than the table needs and costs nothing.

**Stale timeouts follow the cycle.** A receiver treats a message as stale after 3 of its cycles: 60 ms for oil pressure (20 ms), 300 ms for the slow analog message (100 ms). The Dash
gives oil pressure its own freshness (`oil_pressure_state`) so that a stalled slow message can never hide the oil pressure or switch off the low-oil alarm. To be checked on the real bus
for false "stale" under load (TCAN-11).

## Decoding old logs

Before TCAN-28, `DataLogger_Analog` (0x200) was 8 bytes with oil pressure first (oil pressure, oil temperature, coolant, fuel). Since TCAN-28 it is 6 bytes (oil temperature, coolant, fuel) and
oil pressure is its own 2-byte message, 0x203. Logs from before the split must be decoded with the old layout. The daily logs from before this change also reduced everything to 1 Hz.

## Channels in the table with no sensor, node or CAN message yet (gaps)

Boost / MAP, throttle position, AFR / wideband, fuel pressure, DCCD lock command, accelerometer / G-force, GPS. Each needs hardware and a message before it can be logged
at these rates. Engine RPM capture is TCAN-7. A rough bus-load check: every channel in the table at its top high-res rate, as separate frames, is about 900 frames per
second, which is roughly 23% of a 500 kbit/s classic CAN bus at the worst-case frame length (an estimate, to be confirmed on the real bus, TCAN-11).
