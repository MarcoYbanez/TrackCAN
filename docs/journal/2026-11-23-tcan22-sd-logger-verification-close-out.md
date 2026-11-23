# TCAN-22: closing out what TCAN-17, 19 and 21 were merged without proving (2026-11-23)

Walked the list one item at a time with the owner. Evidence is in the logs (all read over USB with `scripts/datalogger_pull.py` and checked with
`scripts/check_datalogger_log.py`) and in the console recordings.

| Item | Result |
|---|---|
| Status LED, slow blink while logging | **Verified** (owner). |
| Status LED, fast flicker with no card | **Verified** (owner). Reported "pretty instant" with the idle probe; about 10 s after the pull on the earlier write path (see below). |
| Card pulled and put back while running, with traffic | **Verified.** Write failed at once, logger stopped ("no card"), retried every 3.4 s, reopened a new file when the card returned, one boot only. |
| Frames queued during a card outage | **Verified: none lost.** LOG0084 -> LOG0085, a ~20 s outage: the heartbeat counter is continuous. The queue (2048 records) bridges an outage of about 33 s at the bench's ~62 frames/s, but only ~0.25 s at the 8,000 frames/s flood. |
| Real 600 s rotation | **Verified.** LOG0083 begins `continues=LOG0082.CSV` at 601,109 ms for a file opened at 1,109 ms: exactly 600 s. |
| Real 30 MB rotation | **Verified.** LOG0095 is 31,458,000 bytes against a limit of 31,457,280 (over by 720 bytes, within one buffer write); the chain LOG0095 -> LOG0096 passes the checker; the 31 MB file came over USB with a good CRC. |
| Rollover failure (the next file cannot be opened) | **Verified by fault injection** (`rollfail` bench build, the 2nd rollover fails on purpose): "cannot create", "logging stopped", "no card", reopened the same, never-created number 3 s later (nothing overwritten), kept rolling every 20 s. The heartbeat counter is continuous across it: no frames lost. One boot. |
| Removal of an idle logger (no traffic) | **Found a limit, fixed, verified.** The logger only touched the card to write, so on a silent bus it never noticed removal and the LED kept saying "logging". Fix: `logger_idle_probe_due` (host-tested, 5 mutants caught) reads sector 0 every 2 s when idle. Verified: the pull is noticed at once ("the card did not answer an idle read"), recovery with a new file when the card returned. |
| The unexplained 13,000-frame drop episodes | **Explained by measurement, not proven at the root.** Caught with the timing instrumentation on the 30 MB run: no single long stall (slowest write 139 ms); a stretch of about 5 s (9:14 to 9:20, 45 s into the 8,000 frames/s step) when 8 KB writes took 100 to 139 ms each (60 to 80 KB/s against about 208 KB/s needed), the 2,048-record queue stayed full, 12,635 frames dropped, every one counted and warned about. Why the card slows (its own housekeeping is my guess) is not proven. At the real worst case (4,000 frames/s, about 104 KB/s) this would be borderline; typical car traffic is far lower. |
| Real bus traffic | **Not done:** TCAN-11 (needs a transceiver and a second node). |

## Findings made while doing it
- **Mac tool bug (fixed):** `datalogger_pull.py` copied its whole buffer for every received chunk, so a 31 MB file (the size a 30 MB rotation produces) took
  quadratic time: 52 s against a simulated device, and on the real Teensy the host fell behind and the Node abandoned the transfer after its 3 s stall
  timeout. Now reads straight into one buffer (2.2 s in the simulation). Regression test: a 31 MB file (the test fails on the old code).
- **A failed write takes about 10 s to report** (the SD driver's own timeouts), so with traffic the LED changes about 10 s after a pull; an idle read fails at once.
- The log shows the real flood drop count and the files agree (12,635 counted, 12,538 flood frames missing plus other frames).

## Still open
- Real bus (TCAN-11). The cause inside the card of the write slowdowns. A write failure with traffic taking about 10 s to be reported.
