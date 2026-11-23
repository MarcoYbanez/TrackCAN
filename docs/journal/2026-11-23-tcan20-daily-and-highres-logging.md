# TCAN-20: daily and button-triggered high-resolution logging (2026-11-23)

## What it does
- **Daily** (`Dnnnnnnn.CSV`): about one frame per (direction, CAN ID) per second, always on. **High-res** (`Mnnnnnnn.CSV`): every frame, from a button
  press received over CAN to the next press or an automatic stop (default 10 minutes). No pre-roll (owner decision). The old `LOGnnnn.CSV` files are never touched.
- The button is the new CAN message `Logger_Control` (0x400, bit `LogButton`, a level sent about every 100 ms). The source node is TBD, modelled as a generic
  `Controller` node. The logger reports its state in `DataLogger_Status` (0x202: `LogMode`, `CardState`) every 200 ms.
- A **rising edge** toggles the mode: debounced (300 ms), a button already down at the first frame (or when frames resume after 400 ms of silence) does not
  toggle. Each family is a **ring with its own space quota** (daily 4 GB, high-res 8 GB by default): when over, the logger deletes ITS OWN oldest file, never
  the newest, never another family's, never a file the USB copy is reading. LED: slow blink daily, 2 blinks a second high-res, 5 no card.

## Verified  [verified: observed]
- **Pure logic** (host): 129 checks, 35 mutations caught (names, ring, daily reduction, button, mode); CAN contract 181 checks; transfer protocol 91 checks
  (21 mutations); checker 38 cases (9 mutations).
- **Mode switching** (bench button played on the bus at 20 s, 50 s and 80 s, 40 s limit): daily 0 to 19.6 s, high-res 20.1 to 50.0 s (2,448 frames, every one),
  daily 50.0 to 80.0 s, high-res 80.1 to 120.1 s (exactly 40.0 s, the time limit), then daily. Repeated on the final build with the same result.
- **Status message** decoded from the logged frames: daily files carry mode 0, high-res files mode 1, card state 3 (logging).
- **Quota ring** (5 MB high-res quota, 1000 KB files, flood): the six oldest M files were deleted and nothing else; all 103 LOG and 7 D files kept their names and
  sizes. The high-res files that survived a run with three full USB downloads going on at the same time pass the checker (239,514 flood frames, 0 missing).

## Bugs found by running it, all fixed
1. **A mode switch rolled the new file over 12 ms later.** The loop's rotation check used a time read before the switch; the new file's age went negative and the
   unsigned subtraction read it as huge. Fixed by taking a fresh time after a switch or a rollover.
2. **The checker crashed on a daily file containing flood frames** (a `KeyError`), and I had committed with that test failing. Fixed; the test is green.
3. **Downloads over USB started failing with `-12` (ENOMEM) after heavy use, and only a reboot cleared it.** Root cause, found with counters on the device:
   - `fs_read` returned `-9` one millisecond after a successful `fs_open`, and `fs_close` then returned 0 without freeing the file object (Zephyr's `fs_close`
     does nothing when the object's mount pointer is NULL). The pool of file objects (2) was exhausted: the logger's plus the leaked one.
   - The mount pointer was zeroed by **DMA cache maintenance**. The NXP SD driver invalidates the data cache over a read buffer after every read DMA, and the
     invalidation rounds out to whole 32-byte lines. The FatFS volume object `l_fat` (its 512-byte sector window is its last member) ended 24 bytes into a line that
     it shared with a file object, so a directory scan on the logger thread undid the `fs_open` just done on the transfer thread. The quota ring's frequent directory
     scans made it likely; the symbol map showed the adjacency (`l_fat` at 0x202884e8, size 0x230, a file object at 0x20288718).
   - Fix: every buffer the SD controller reads or writes by DMA (`l_fat`, the logger's write buffer, the idle probe and card-identity buffers, the USB copy's read
     buffer) is `__nocache` and cache-line aligned. Before: the stress run (ring flood plus repeated full downloads) failed 3 of 3 times. After: 3 full downloads in a
     row, 0 file-pool errors; the only 2 failures were files the ring legitimately deleted between the listing and the download.
   - This could also have corrupted other variables next to an SD buffer (the logger's write buffer sat next to the old read buffer). I found no evidence it ever
     did in any log check, but I cannot prove it never happened.

## Not verified / open
- **The LED rhythm for high-res** (2 blinks a second against 1 daily and 5 no card): in the code, not watched by a person.
- **The real button source**: nothing real sends `Logger_Control` yet; only the bench player was used. The Dash and Ethanol nodes share the changed CAN core: the Dash
  builds (both cores); the Ethanol firmware was NOT rebuilt (no configured build directory), only the shared core's host tests ran.
- A power cut during a mode switch or while the ring is deleting; the real quota sizes (only 5 MB was exercised); more than one day of daily files.
- The FlexCAN "Unhandled status" warning in loopback is still not understood; the bench builds now silence it.
- The earlier checks of TCAN-19 and TCAN-21 were made on an image with the cache bug present; the fix removes a rare risk, it does not invalidate those results.
