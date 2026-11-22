# TCAN-6: the Dash M4 FDCAN port (classic CAN, loopback first)

Date: 2026-11-22. Tags: **[measured]** observed on the STM32H745I-DISCO this session, **[read]** read in the Zephyr tree or UM2488,
**[unverified]** a lead, not checked.

## Decisions

- **Live is now the default image.** `scripts/dash_build_flash.sh build|all [live|loopback|demo]`. Live runs the real FDCAN on the bus
  and publishes only received data; loopback is a bench image with no wiring; demo is the old fake data with no CAN.
- **FDCAN1** (PH13 = TX, PH14 = RX) on the M4, classic CAN, 500 kbit/s, sample point target 87.5 %, bit timing from the devicetree.
  The owner reported "CN1 = FDCAN1"; the connector labels are still unverified (below).
- **Loopback test = the real stack, simulated Nodes.** The M4 runs the FDCAN controller in its internal loopback and a simulated
  DataLogger and Ethanol Node (`dash_loopback_source`, built on the real TrackCAN core and the generated packers) send frames
  through it. The frames go through the transmit FIFO, back into the receive FIFO, through the Dash decoder and to the screen.
  The data is still simulated, so the DEMO flag is set.
- **CAN diagnostics ride in the snapshot** (controller state, error counters, frame counters, the failing start step with its
  errno, the clock and bit timing), because the M4's console (USART2) does not reach the ST-LINK USB. The M7 prints them once a
  second and the existing CAN lamp shows the controller's health.

## Findings

1. **The first hardware run failed: the FDCAN device was "not ready".** [measured] The driver's recorded init result was **-134
   (ENOTSUP)**. [read] Cause: `stm32_clock_control_configure()` in `clock_stm32_ll_h7.c` first calls `enabled_clock()` and returns
   `-ENOTSUP` for a clock source that the *calling core's* devicetree does not declare enabled. The M4's board file does not enable
   `clk_hse`, because the M7 owns the clocks.
2. **Fix: declare HSE (25 MHz) in the M4's overlay.** UM2488 section 6.5: "X1: 25 MHz oscillator for the microcontroller" [read];
   the M7's board file already enables HSE at 25 MHz [read]. On the M4 the driver compiles out all oscillator, PLL and prescaler
   setup (it is inside `CONFIG_CPU_CORTEX_M7`) [read], so the declaration changes no hardware setting; it only tells the driver the
   clock exists and its frequency. HSE was chosen as the FDCAN kernel clock instead of a PLL because the M4 does not configure PLLs.
   HSE is also the reset default of the FDCAN clock mux (RM0399, FDCANSEL), so the clock is HSE even if the mux write did nothing.
3. **After the fix the controller runs.** [measured] Loopback image: state `ACTIVE`, error counters 0, frames received rising at
   about 93 per second, `fresh: analog 1 ethanol 1 lights 1`, lamp bits from the demo script arriving through the bus.
4. **The driver's clock and timing, as it computes them:** [measured] kernel clock 25,000,000 Hz, prescaler 1, 50 time quanta per bit
   (exactly 500 kbit/s), sample point **86.0 %** (43 of 50), 1.5 % under the 87.5 % target. Zephyr's `can_calc_timing()` returns
   the sample point *error*, not the sample point (an early mislabel in the log, fixed).
5. **Transmit buffers.** The FDCAN message RAM layout (`bosch,mram-cfg = <0x0 28 8 6 0 0 3 3>`, 72-byte elements) gives only 3
   transmit buffers. The simulated Nodes all fired in the same 20 ms tick and about 11 frames per second were dropped. The
   simulation now starts each message on its own step: dropped frames fell to 0 (one stray frame in 15 s). Real Nodes are separate
   devices and do not burst like this. The host test fails if a step sends more than 3 frames (mutation checked).
6. **Live image with nothing on the bus:** [measured] the controller starts in normal mode and within a second the transmit error
   counter reaches **128: `PASSIVE`**, the textbook behavior of a lone node whose frames nobody acknowledges. It shows the pins,
   transceiver path and clock are live. It does not prove the bit rate; only a second node can.

## What is proven and what is not

| Proven on hardware | Not proven |
|---|---|
| FDCAN1 starts on the M4 with the clock selected from the M4 | the bit rate and sample point against a real node |
| Frames go through the controller and the whole Dash decode path to the screen (loopback) | any frame on the real bus |
| Live mode fails closed: `--` everywhere, ethanol alert after 2 s, CAN lamp red | the connector and its CANH/CANL pins |

## Absences (what the sources do not say)

- **UM2488 has no CAN section:** no connector pinout, no transceiver part, no termination. The only CAN facts are "2 x CAN FDs (U21, U22)" in the
  layout figure and the pin table (FDCAN1 TX PH13, RX PH14; FDCAN2 RX PB5, TX PB13). The owner's connector labels are unverified.
- **[unverified]** An ST community reply says the board has two MCP2562FD transceivers and needs no external one; that is a forum
  lead, not a source. The schematic pack on st.com is where the connector routing and the part numbers will be.
- Zephyr's driver log (`LOG_ERR`) is invisible on the M4; the error code came from the device state instead.

## Next (needs hardware, see `docs/user-todo/dash-todo.md`)

Wire the Dash to a node on a real bus and flash the live image: the expected console is `can: ACTIVE`, the frame counter rising, and
`fresh: analog 1` once the DataLogger is sending. Then unplug it and check the boxes show `--` within three cycles.
