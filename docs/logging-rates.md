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

## How the table is applied (owner decision, 2026-11-23: "only apply what we currently have")

The logger records raw CAN frames, so a rate in the table is realised in two places: **how often the sender transmits** (the cycle time in `can/trackcan.dbc`,
which is the high-resolution rate, because high-res logs every frame) and **how the daily mode reduces the frames** (an interval per message, in
`DataLogger_Node/src/logger/logger_segments.c`). Several signals share one frame, so a frame takes the rate of its fastest signal.

| Channel | CAN message (ID) | High-res (send rate) | Daily (logged every) | Status |
|---|---|---|---|---|
| Engine RPM (and speed) | Dash_Engine (0x100) | 10 ms = 100 Hz | 100 ms = 10 Hz | applied (the RPM source itself is TCAN-7) |
| Oil pressure | DataLogger_OilPressure (0x203) | 20 ms = 50 Hz | 100 ms = 10 Hz | applied (the low end of 50 to 100) |
| Oil temperature, coolant temperature, fuel level | DataLogger_Analog (0x200) | 100 ms = 10 Hz | 1000 ms = 1 Hz | applied (fuel level is not in the table: it rides with the slow signals) |
| Ethanol percentage | Ethanol_Data (0x300) | **unchanged, 100 ms = 10 Hz** | 1000 ms = 1 Hz | daily applied; the send rate is deliberately NOT changed (see below) |
| Lights, status, button, heartbeats | 0x201, 0x202, 0x400, 0x701 to 0x703 | unchanged | 1000 ms = 1 Hz (default) | not in the table: default |

**Why the ethanol send rate stays at 100 ms.** The table asks for 1 to 5 Hz, but the ethanol message's cycle time is also what the Dash uses to decide the sensor is lost
(stale after 3 cycles). Slowing it to 5 Hz would double the time before the Dash raises its ethanol alert, which is a safety-relevant behaviour change, so it needs its own
decision. High-res logs every ethanol frame, which is more than the table needs, and costs nothing.

**Stale timeouts follow the cycle.** A receiver treats a message as stale after 3 of its cycles, so a 10 ms engine cycle means a 30 ms timeout and a 20 ms oil-pressure
cycle a 60 ms timeout. To be checked on the bench for false "stale" on a busy bus.

## Channels in the table with no sensor, node or CAN message yet (gaps)

Boost / MAP, throttle position, AFR / wideband, fuel pressure, DCCD lock command, accelerometer / G-force, GPS. Each needs hardware and a message before it can be logged
at these rates. Engine RPM capture is TCAN-7. A rough bus-load check: every channel in the table at its top high-res rate, as separate frames, is about 900 frames per
second, which is roughly 23% of a 500 kbit/s classic CAN bus at the worst-case frame length (an estimate, to be confirmed on the real bus, TCAN-11).
