# 2026-11-17: CAN backbone design decisions (TCAN-1)

Outcome of the design grilling for TCAN-1. Hardware facts are in
[2026-11-17-can-hardware-research.md](2026-11-17-can-hardware-research.md).

## Scope

One shared C library that all three Nodes (DataLogger, Ethanol, Dash) use to transmit and
receive CAN messages. No branch ordering: TCAN-1 is treated as the first piece of work.

## Decisions

| Topic | Decision |
|-------|----------|
| Frame format | **Classic CAN 2.0**, 11-bit IDs, max 8 bytes. The Ethanol Feather (MCP25625) is classic-only, so no CAN-FD frames on the bus. DataLogger and Dash receive classic frames. |
| Bitrate | 500 kbit/s |
| Byte order | Little-endian signals, engineering units as integers |
| Source of truth | DBC file; C generated from it and committed to the repo |
| Library | `can/`: portable C11, no OS or SDK calls. Per-Node port interface: `send`, `receive`, `now_ms` |
| Staleness | A signal is stale after 3 missed cycles |
| Safety | The Ethanol Node's PWM-to-DAC 5 V ECU output never depends on CAN. CAN failure is silent and non-blocking |
| Teensy bus | **CAN3 (FlexCAN3, TX pin 31 / RX pin 30)**, as drawn in `assets/DataLogger_schematic.drawio.svg`. Changed from CAN1 after the schematic review. Runs in classic mode, normal mode (the DataLogger transmits as well as receives) |
| Heartbeat | 2 bytes: status flag + rolling counter, from every Node at 1 Hz |
| Tests | Host-side C tests with `assert`, CMake + ctest. Expected bytes are worked out by hand, not only round-tripped |
| Git | Work on `TCAN-1-Create-CAN-backbone`. No commit or push until the owner asks |

## Messages

| ID | Message | Sender | Rate | Payload |
|----|---------|--------|------|---------|
| 0x100 | Dash_Engine | Dash | 50 Hz | RPM, speed |
| 0x200 | DataLogger_Analog | DataLogger | 20 Hz | oil pressure, oil temp, coolant temp, fuel level |
| 0x201 | DataLogger_Lights | DataLogger | 10 Hz | 8 OEM dash-light bits |
| 0x300 | Ethanol_Data | Ethanol | 10 Hz | ethanol content, fuel temp |
| 0x701 | Heartbeat_Dash | Dash | 1 Hz | status, counter |
| 0x702 | Heartbeat_DataLogger | DataLogger | 1 Hz | status, counter |
| 0x703 | Heartbeat_Ethanol | Ethanol | 1 Hz | status, counter |

## Signal encoding

| Signal | Type | Scale | Unit | Source |
|--------|------|-------|------|--------|
| RPM | uint16 | 1 | rpm | owner-confirmed |
| Speed | uint16 | 0.01 | km/h | proposed |
| Oil pressure | uint16 | 0.01 | **bar** | owner-confirmed unit (Defi Advance BF gauge) |
| Oil temp | int16 | 0.1 | **degC** | owner-confirmed unit |
| Coolant (water) temp | int16 | 0.1 | **degC** | owner-confirmed unit |
| Fuel level | uint16 | 0.1 | % | **default, not discussed** |
| Ethanol content | uint8 | 1 | % | proposed |
| Fuel temp | int16 | 0.1 | degC | proposed |

## Display units

The Dash **displays PSI and Fahrenheit** (owner preference). The bus and the SD log stay in
bar and degC, so there is one conversion point and logged data matches the wire.
The library provides integer conversion helpers (bar to PSI, degC to degF) for the Dash.

## Open items (not blocking the CAN code)

- **Defi sensors are tapped at the analog signal, not decoded from the Defi CAN
  message** (the Defi CAN protocol cannot be decoded). The DataLogger's MCP3208 reads
  the sensor signal. The transfer curves (volts or ohms to bar / degC) are **not
  known yet**; the DataLogger demo sends clearly labelled placeholder values.
- **Electrical risk of tapping in parallel with the gauge** (verify before wiring):
  - the ADC input may load the sensor signal and shift the gauge reading
  - the sensor voltage could exceed the MCP3208 reference voltage
- ~~Feather crystal frequency~~ resolved: 16 MHz, from Zephyr's `adafruit_feather_canbus_rp2040`
  devicetree (`osc-freq = <16000000>`). Not yet confirmed against the Adafruit schematic.
- Whether the Ethanol build must target an Adafruit Feather RP2040 CAN board instead of
  a plain Pico.
- The Dash (STM32H7) toolchain is undecided; only a port stub is delivered.

## Status at end of session (what was actually checked)

| Item | State |
|------|-------|
| `can/trackcan.dbc` + generated C | Done. `scripts/gen_can.sh` regenerates it; the output differs from a fresh run only by cantools' timestamp comment |
| Core library (`can/common/trackcan_core.*`) | Done. Host tests: **128 checks, 0 failures** (`cmake -S can -B build/can_tests && ctest --test-dir build/can_tests`) |
| Test quality | Expected bytes are hand-computed. Mutation-checked: an off-by-one in staleness and a big-endian RPM signal were both caught |
| DataLogger port (`can/datalogger.c`) | **Compiles and links** for `teensy41` (Zephyr 4.4.99, gnuarmemb), 500 kbit/s resolves in the devicetree. **Not flashed, not run on hardware** |
| Ethanol port (`can/ethanol.c`) | **Compiles and links** (Pico SDK + FreeRTOS, `.uf2` produced). **Not flashed, not run on hardware.** The MCP25625 driver and bit timing are unverified against a real chip |
| Dash (`can/dash.c`) | Logic done and host-tested (PSI/degF display conversion, stale handling, publishing). `dash_port_stm32h7.c` is a **stub**: no FDCAN driver |

### Not verified, check on hardware

- **MCP25625 bit timing** (CNF1/2/3 = 0x00/0xB5/0x01 for 16 MHz, 500 kbit/s, 87.5 %) is derived by hand. Confirm with a scope or by reading frames on another node.
- **STANDBY pin polarity** on the Feather (driven low = normal). Zephyr marks STANDBY and RESET "not used", so this was not confirmed from a primary source.
- **The bus needs exactly two 120 ohm terminators, one at each physical end.** The Feather
  has one fitted (`TERM` jumper): cut it if the Feather is not at an end of the bus. Three
  terminators give about 40 ohm, which is out of spec. The Teensy also needs an external
  transceiver (none on board).
- A classic-only controller on the bus is only safe because no FD frames are sent. The Teensy's CAN3 is FD-capable but runs in classic mode: do not request `CAN_MODE_FD` or set `CONFIG_CAN_FD_MODE` while the Feather is on the bus.
- **Confirm the DataLogger transceiver is wired to pins 30 (RX) and 31 (TX)** and that CAN3's TX/RX orientation matches (TX = 31, RX = 30).
- Pico SDK board target is `adafruit_feather_rp2040`, not the CAN Feather. SDK defaults differ but the Ethanol code sets its pins explicitly.
- Run `west`-less: this machine has no `west` on PATH. The DataLogger was built with plain `cmake -GNinja -DBOARD=teensy41` using the venv in `~/zephyrproject/.venv`.

### Changes to existing files

- `Ethanol_Node/main.c`: the two ECU-signal tasks were raised from priority 1 to 2 so the CAN task (priority 1) can never delay them, and the reader now publishes ethanol content and fuel temp to CAN.
- `DataLogger_Node`: `prj.conf` (CAN, LOG), `teensy41.overlay` (FlexCAN3 chosen as `zephyr,canbus`, 500 kbit/s, FlexCAN1 disabled), `CMakeLists.txt` (shared sources).
- Root `CMakeLists.txt`: passes `TRACKCAN_PATH` to the Ethanol build.
