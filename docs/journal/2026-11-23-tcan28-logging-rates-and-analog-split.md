# TCAN-28: the owner's logging rates, per-message daily rates, the analog message split (2026-11-23)

Spec and measurements: `docs/logging-rates.md`. This is what was done and what was found.

## What changed
- **CAN contract:** `DataLogger_Analog` (0x200) is now 6 bytes (oil temperature, coolant, fuel level), every 100 ms. Oil pressure moved to a new message `DataLogger_OilPressure` (0x203, 2 bytes) every 20 ms.
  `Dash_Engine` and `Ethanol_Data` are unchanged (reasons in the spec). Generated with cantools 45.2.0.
- **Dash:** `dash_display` has a new `oil_pressure_state`. The oil-pressure box and the full-screen low-oil alarm use it, so a stalled slow message (temperatures, fuel) can no longer hide the
  oil pressure or switch off the alarm. Before the split a single message carried both, so this dependency did not exist; the split removes the possibility. The M4 and M7 sources changed: all three Dash modes (live, loopback,
  demo) build for both cores. The Dash was NOT run on the hardware for this ticket.
- **DataLogger:** sends both messages; the daily mode reduces per message (100 ms for engine and oil pressure, 1000 ms for the rest), the table is keyed on the contract's own ID macros.
- **Checker:** verifies each message against its own daily interval (written out independently of the firmware). 47 cases, mutations caught.

## Verified
- Host: CAN 199 checks (hand-worked bytes for both new layouts, independent staleness: oil pressure 60 ms, slow message 300 ms), logger segments 170, Dash loopback 479,868, checker 47; mutations of the
  new Dash handling, the simulator staggering, the core table, the daily table and the checker rule are all caught (7 + 7 + 6, plus the original set).
- Hardware (DataLogger, bench with the real CAN code in loopback): achieved rates per file match the table (see the spec), and every file passes the checker.

## Found by running it
1. **The daily file could not be created.** My first daily mode note was 79 characters; the logger's note formatter refuses anything that does not fit its 80-character line, so the header failed on every try.
   Worse, each 2-second retry left another empty file behind. Fixed: the note is short (`SEG_NOTE_DAILY`), a host test formats EVERY note the firmware writes at the widest timestamp (and shows
   the original text fails it), a failed header now removes the empty file it just made, and at start-up the logger removes any zero-byte D or M file (about 80 had piled up on the card; all gone).
2. **The loopback simulator exceeded the FDCAN controller's 3 transmit buffers.** The new 50 Hz oil pressure takes a slot every 20 ms step, so the bench-only simulator's start steps were re-planned
   (at most one other frame per step). The simulator is bench-only; real nodes have their own controllers.
3. **The coolant tolerance in the Dash loopback test was tied to the old 50 ms rate** (3 tenths of a degree, measured 4 with the 100 ms message). It is now derived from the cause: five 20 ms steps of
   the drive's largest coolant change, plus 2 for rounding.

## Findings for other tickets
- **TCAN-7:** the live Dash image never sets the engine values, so it transmits `Dash_Engine` with RPM 0 and speed 0 every 20 ms on a real bus. Engine RPM at 100 Hz needs the sender loop at 10 ms
  and the M4 watchdog window retuned: build it with the RPM source.
- **Ethanol 1 to 5 Hz:** not applied; the cycle time is also the Dash's stale detection (safety-relevant): needs its own decision.

## Not verified / open
- The Dash on the real hardware with the split messages (host tests and builds only): the oil-pressure box, the other three boxes and the low-oil alarm in the demo and loopback images.
- The Ethanol firmware was not rebuilt (no configured build directory); it shares `trackcan_core`, whose host tests pass.
- Stale timeouts on a busy real bus (60 ms for oil pressure); the real bus is TCAN-11.
- The daily rates deliver about 90 to 95% of nominal under sender jitter (9.3 for 10 Hz): by design, documented.
