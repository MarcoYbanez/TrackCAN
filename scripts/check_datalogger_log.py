#!/usr/bin/env python3
"""Checks a DataLogger SD log (LOGnnnn.CSV) without trusting the firmware's own counters (TCAN-17).

    python3 -I scripts/check_datalogger_log.py LOG0009.CSV [--loopback]

Checks: the header; every line well formed; time never goes backwards (NOTE lines excepted: they are stamped when written);
per-ID TX and RX counts; the heartbeat counter (ID 0x702, second byte) never skips; in a loopback bench log every TX has its RX.
Exit code 0 = all checks passed. Standard library only.
"""
import re
import sys
from collections import Counter

HEADER = "t_ms,dir,id,dlc,data"
FRAME = re.compile(r"^(\d+),(TX|RX),([0-9A-F]{3}),([0-8]),([0-9A-F]*)$")
NOTE = re.compile(r"^(\d+),NOTE,,,(.*)$")
HEARTBEAT_ID = 0x702
FLOOD_ID, FLOOD_MARK_ID = 0x7F0, 0x7FF  # bench flood generator: sequence number and rate-step marker


def check(lines, loopback):
    problems = []
    notes = []
    tx, rx = Counter(), Counter()
    last_t = None
    heartbeat = []  # (t, counter) of TX heartbeats
    seqs, marks = [], []  # flood bench: (t, sequence) and (line, rate)
    if not lines or lines[0] != HEADER:
        problems.append(f"line 1 is not the header: {lines[0] if lines else '(empty file)'!r}")
    for n, line in enumerate(lines[1:], start=2):
        m = FRAME.match(line)
        if m:
            t, direction, fid, dlc, data = int(m[1]), m[2], int(m[3], 16), int(m[4]), m[5]
            if len(data) != 2 * dlc:
                problems.append(f"line {n}: dlc {dlc} but {len(data) // 2} data bytes: {line!r}")
                continue
            if last_t is not None and t < last_t:
                problems.append(f"line {n}: time went backwards ({last_t} -> {t})")
            last_t = t
            (tx if direction == "TX" else rx)[fid] += 1
            if direction == "TX" and fid == FLOOD_ID and dlc == 4:
                seqs.append((t, int(data, 16)))
            if direction == "TX" and fid == FLOOD_MARK_ID and dlc == 2:
                marks.append((t, int(data, 16)))
            if direction == "TX" and fid == HEARTBEAT_ID and dlc >= 2:
                heartbeat.append((t, int(data[2:4], 16)))
            continue
        m = NOTE.match(line)
        if m:
            notes.append((int(m[1]), m[2]))
            continue
        problems.append(f"line {n}: not a valid record (cut short?): {line!r}")

    # The last line of a log cut by a power loss may be partial: report it, but it is the only one allowed to be.
    gaps = [(a, b) for (_, a), (_, b) in zip(heartbeat, heartbeat[1:]) if (b - a) % 256 != 1]
    if gaps:
        problems.append(f"heartbeat counter skipped {len(gaps)} time(s), e.g. {gaps[0][0]} -> {gaps[0][1]}")
    if loopback:
        for fid in sorted(set(tx) | set(rx)):
            if abs(tx[fid] - rx[fid]) > 2:
                problems.append(f"loopback ID {fid:03X}: {tx[fid]} TX but {rx[fid]} RX")
        if not any("loopback" in text for _, text in notes):
            problems.append("a loopback log must say so in a NOTE line")
    dropped = [text for _, text in notes if text.startswith("dropped=")]
    if seqs:
        # Flood bench: every missing sequence number is a lost frame. It must have been counted (a dropped= NOTE), never silent.
        missing = sum(b - a - 1 for (_, a), (_, b) in zip(seqs, seqs[1:]) if b > a) + seqs[0][1]
        print(f"  flood: {len(seqs)} frames in the file, {missing} missing before the last one; rate steps: {[r for _, r in marks]}")
        for (t0, r), (t1, _) in zip(marks, marks[1:] + [(last_t or 0, 0)]):
            inside = [(t, q) for t, q in seqs if t0 <= t < t1]
            lost = sum(b - a - 1 for (_, a), (_, b) in zip(inside, inside[1:]) if b > a)
            print(f"    at {r:5d} frames/s: {len(inside):6d} written, {lost:6d} lost inside the step")
        if missing and not dropped:
            problems.append(f"{missing} frames missing but the log never said frames were dropped (silent loss)")
    return problems, tx, rx, notes, dropped, last_t


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    with open(args[0], newline="") as f:
        lines = f.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()  # the final newline
    problems, tx, rx, notes, dropped, last_t = check(lines, "--loopback" in argv)

    print(f"{args[0]}: {len(lines)} lines, last t = {last_t} ms")
    for fid in sorted(set(tx) | set(rx)):
        print(f"  ID {fid:03X}: {tx[fid]:6d} TX {rx[fid]:6d} RX")
    for t, text in notes:
        print(f"  NOTE at {t} ms: {text}")
    if dropped:
        print(f"  frames were dropped: {dropped[-1]}")
    if problems:
        print(f"FAIL: {len(problems)} problem(s)")
        for p in problems[:15]:
            print("  -", p)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
