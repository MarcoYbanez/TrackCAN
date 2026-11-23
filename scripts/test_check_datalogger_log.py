#!/usr/bin/env python3
"""Tests for check_datalogger_log.py: every check must pass a good log and fail a log broken in exactly that way (TCAN-17).

    python3 -I scripts/test_check_datalogger_log.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKER = os.path.join(HERE, "check_datalogger_log.py")


def good_log():
    lines = ["t_ms,dir,id,dlc,data", "1,NOTE,,,start", "1,NOTE,,,mode=loopback bench data"]
    t = 10
    for c in range(20):  # heartbeat TX + its loopback RX every 50 ms, counter 0..19
        lines += [f"{t},TX,702,2,00{c:02X}", f"{t},RX,702,2,00{c:02X}"]
        t += 50
    assert lines[11].startswith("210,TX") and lines[12].startswith("210,RX") and lines[13].startswith("260,TX")
    return lines


def run(lines, loopback=True):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "LOG0001.CSV")
        with open(p, "w") as f:
            f.write("\n".join(lines) + "\n")
        args = [sys.executable, "-I", CHECKER, p] + (["--loopback"] if loopback else [])
        return subprocess.run(args, capture_output=True, text=True).returncode


def pair_at(ts):  # a neutral TX+RX pair inserted after the t=210 heartbeat pair
    l = good_log()
    l[13:13] = [f"{ts},TX,300,1,00", f"{ts},RX,300,1,00"]
    return l


def drop(l, idx):
    return [x for i, x in enumerate(l) if i not in idx]


CASES = [  # (name, lines, expected exit code: 0 pass, 1 fail)
    ("good log", good_log(), 0),
    ("bad header", ["t,dir,id,dlc,data"] + good_log()[1:], 1),
    ("cut-short last line", good_log() + ["1010,TX,702,2,00"], 1),
    ("dlc does not match data", good_log() + ["2000,TX,200,8,0011"], 1),
    ("same time", pair_at(210), 0),
    ("1 ms back (tolerated)", pair_at(209), 0),
    ("2 ms back (tolerated)", pair_at(208), 0),
    ("3 ms back", pair_at(207), 1),
    ("50 ms back", pair_at(160), 1),
    ("heartbeat gap, silent", drop(good_log(), (11, 12)), 1),
    ("heartbeat gap, noted as dropped", drop(good_log(), (11, 12))[:3] + ["20,NOTE,,,dropped=2"] + drop(good_log(), (11, 12))[3:], 0),
    ("loopback TX without RX", [x for x in good_log() if not x.startswith(("510,RX", "560,RX", "610,RX", "660,RX"))], 1),
    ("loopback log without its NOTE", [x for x in good_log() if "loopback" not in x], 1),
    ("flood frames are TX only", good_log() + ["2000,TX,7F0,4,00000000", "2001,TX,7F0,4,00000001"], 0),
    ("flood sequence gap, silent", good_log() + ["2000,TX,7F0,4,00000000", "2001,TX,7F0,4,00000005"], 1),
]

if __name__ == "__main__":
    bad = 0
    for name, lines, expect in CASES:
        rc = run(lines)
        ok = rc == expect
        bad += not ok
        print(f"{'ok   ' if ok else 'WRONG'} {name} (expected {'FAIL' if expect else 'PASS'})")
    print(f"{len(CASES) - bad}/{len(CASES)} as expected")
    sys.exit(1 if bad else 0)
