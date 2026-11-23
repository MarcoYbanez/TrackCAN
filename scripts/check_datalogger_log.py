#!/usr/bin/env python3
"""Checks DataLogger SD logs (LOGnnnn.CSV) without trusting the firmware's own counters (TCAN-17, TCAN-19).

    python3 -I scripts/check_datalogger_log.py [--loopback] LOG0005.CSV [LOG0006.CSV ...]

One file is checked on its own. Several files are checked as ONE stream, in the order given (a rotated log): consecutive file numbers,
each later file says `continues=<previous file>`, and time, the heartbeat counter and the flood sequence carry on across each boundary.

Checks: the header; every line well formed; time never goes backwards by more than 2 ms (NOTE lines excepted: they are stamped when
written); the heartbeat counter (ID 0x702, second byte) never skips; in a loopback bench log every TX has its RX and every file says
it is bench data; a bench flood's sequence numbers have no gap. A gap is acceptable only if a `dropped=` NOTE says frames were dropped.
Exit code 0 = all checks passed. Standard library only.
"""
import os
import re
import sys
from collections import Counter

HEADER = "t_ms,dir,id,dlc,data"
FRAME = re.compile(r"^(\d+),(TX|RX),([0-9A-F]{3}),([0-8]),([0-9A-F]*)$")
NOTE = re.compile(r"^(\d+),NOTE,,,(.*)$")
FILE_NAME = re.compile(r"^LOG(\d{4})\.CSV$", re.IGNORECASE)
HEARTBEAT_ID = 0x702
JITTER_MS = 2  # two threads stamp a frame before queuing it: a step back of 1-2 ms is possible (bench flood only)
FLOOD_ID, FLOOD_MARK_ID = 0x7F0, 0x7FF  # bench flood generator: sequence number and rate-step marker


def parse(name, lines):
    """One file -> (records, notes, problems). A record is (t, dir, id, dlc, data_hex); a note is (t, text)."""
    records, notes, problems = [], [], []
    if not lines or lines[0] != HEADER:
        problems.append(f"{name}: line 1 is not the header: {lines[0] if lines else '(empty file)'!r}")
    for n, line in enumerate(lines[1:], start=2):
        m = FRAME.match(line)
        if m:
            t, direction, fid, dlc, data = int(m[1]), m[2], int(m[3], 16), int(m[4]), m[5]
            if len(data) != 2 * dlc:
                problems.append(f"{name} line {n}: dlc {dlc} but {len(data) // 2} data bytes: {line!r}")
                continue
            records.append((t, direction, fid, dlc, data))
            continue
        m = NOTE.match(line)
        if m:
            notes.append((int(m[1]), m[2]))
            continue
        problems.append(f"{name} line {n}: not a valid record (cut short?): {line!r}")
    return records, notes, problems


def check(files, loopback):
    """files: [(name, lines)] in stream order. Returns (problems, info)."""
    problems = []
    parsed = []
    for name, lines in files:
        recs, notes, probs = parse(name, lines)
        problems += probs
        parsed.append((name, recs, notes))

    tx, rx = Counter(), Counter()
    heartbeat, seqs, marks, all_notes = [], [], [], []
    last_t, gaps_between = None, []
    for i, (name, recs, notes) in enumerate(parsed):
        all_notes += notes
        if loopback and not any("loopback" in text for _, text in notes):
            problems.append(f"{name}: a loopback log must say so in a NOTE line (every file)")
        if i > 0:
            previous = parsed[i - 1][0]
            want = f"continues={previous}"
            m_prev, m_cur = FILE_NAME.match(os.path.basename(previous)), FILE_NAME.match(os.path.basename(name))
            if m_prev and m_cur and int(m_cur[1]) != int(m_prev[1]) + 1:
                problems.append(f"{name}: not the next file after {previous}")
            if not any(text == want for _, text in notes):
                problems.append(f"{name}: no NOTE '{want}'")
            if recs and last_t is not None:
                gaps_between.append((previous, name, recs[0][0] - last_t))
        for t, direction, fid, dlc, data in recs:
            if last_t is not None and t + JITTER_MS < last_t:
                problems.append(f"{name}: time went backwards ({last_t} -> {t})")
            last_t = t if last_t is None else max(last_t, t)
            (tx if direction == "TX" else rx)[fid] += 1
            if direction == "TX" and fid == FLOOD_ID and dlc == 4:
                seqs.append((t, int(data, 16)))
            if direction == "TX" and fid == FLOOD_MARK_ID and dlc == 2:
                marks.append((t, int(data, 16)))
            if direction == "TX" and fid == HEARTBEAT_ID and dlc >= 2:
                heartbeat.append((t, int(data[2:4], 16)))

    dropped = [text for _, text in all_notes if text.startswith("dropped=")]
    info = {"tx": tx, "rx": rx, "notes": all_notes, "dropped": dropped, "last_t": last_t, "gaps": gaps_between, "seqs": seqs,
            "marks": marks, "lines": sum(len(lines) for _, lines in files)}

    hb_gaps = [(a, b) for (_, a), (_, b) in zip(heartbeat, heartbeat[1:]) if (b - a) % 256 != 1]
    if hb_gaps and not dropped:
        problems.append(f"heartbeat counter skipped {len(hb_gaps)} time(s), e.g. {hb_gaps[0][0]} -> {hb_gaps[0][1]}, "
                        "and the log never said frames were dropped (silent loss)")
    info["heartbeat_gaps_explained"] = len(hb_gaps) if dropped else 0
    if loopback:
        for fid in sorted(set(tx) | set(rx)):
            if fid in (FLOOD_ID, FLOOD_MARK_ID):
                continue  # the bench flood is TX only by design
            if abs(tx[fid] - rx[fid]) > 2 + (len(parsed) - 1) * 2:  # a rollover may split a TX from its RX
                problems.append(f"loopback ID {fid:03X}: {tx[fid]} TX but {rx[fid]} RX")
    if seqs:
        # Flood bench: every missing sequence number is a lost frame. It must have been counted (a dropped= NOTE), never silent. The
        # count starts from 0 only if the first file is the start of a run.
        starts_run = not any(text.startswith("continues=") for _, text in parsed[0][2])
        missing = sum(b - a - 1 for (_, a), (_, b) in zip(seqs, seqs[1:]) if b > a) + (seqs[0][1] if starts_run else 0)
        info["flood_missing"] = missing
        if missing and not dropped:
            problems.append(f"{missing} frames missing but the log never said frames were dropped (silent loss)")
    return problems, info


def read_lines(path):
    with open(path, newline="") as f:
        lines = f.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()  # the final newline
    return lines


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    files = [(os.path.basename(p), read_lines(p)) for p in args]
    problems, info = check(files, "--loopback" in argv)

    print(f"{', '.join(n for n, _ in files)}: {info['lines']} lines in {len(files)} file(s), last t = {info['last_t']} ms")
    tx, rx = info["tx"], info["rx"]
    for fid in sorted(set(tx) | set(rx)):
        print(f"  ID {fid:03X}: {tx[fid]:6d} TX {rx[fid]:6d} RX")
    for t, text in info["notes"]:
        if not text.startswith(("start", "continues=", "mode=")):
            print(f"  NOTE at {t} ms: {text}")
    if info["gaps"]:
        worst = max(g for _, _, g in info["gaps"])
        print(f"  rollovers: {len(info['gaps'])}, time between the last record of a file and the first of the next: "
              f"min {min(g for _, _, g in info['gaps'])} ms, max {worst} ms")
    if info.get("heartbeat_gaps_explained"):
        print(f"  heartbeat gaps: {info['heartbeat_gaps_explained']}, explained by a dropped= note")
    if info["seqs"]:
        print(f"  flood: {len(info['seqs'])} frames, {info['flood_missing']} missing; rate steps: {[r for _, r in info['marks']]}")
    if info["dropped"]:
        print(f"  frames were dropped: {info['dropped'][-1]}")
    if problems:
        print(f"FAIL: {len(problems)} problem(s)")
        for p in problems[:15]:
            print("  -", p)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
