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
    """lines: one file's lines, or a list of (file name, lines) for a rotated log."""
    files = [("LOG0001.CSV", lines)] if lines and isinstance(lines[0], str) else lines
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for name, content in files:
            p = os.path.join(d, name)
            with open(p, "w") as f:
                f.write("\n".join(content) + "\n")
            paths.append(p)
        args = [sys.executable, "-I", CHECKER] + paths + (["--loopback"] if loopback else [])
        return subprocess.run(args, capture_output=True, text=True).returncode


def pair_at(ts):  # a neutral TX+RX pair inserted after the t=210 heartbeat pair
    l = good_log()
    l[13:13] = [f"{ts},TX,300,1,00", f"{ts},RX,300,1,00"]
    return l


def drop(l, idx):
    return [x for i, x in enumerate(l) if i not in idx]


def chain(first_beats=range(0, 10), second_beats=range(10, 20), t2=520, second_name="LOG0002.CSV", note2="continues=LOG0001.CSV",
          loop2="1,NOTE,,,mode=loopback bench data"):
    """Two rotated files: heartbeat counters 0..9 then 10..19, 50 ms apart."""
    a = ["t_ms,dir,id,dlc,data", "1,NOTE,,,start", "1,NOTE,,,mode=loopback bench data"]
    t = 10
    for c in first_beats:
        a += [f"{t},TX,702,2,00{c:02X}", f"{t},RX,702,2,00{c:02X}"]
        t += 50
    b = ["t_ms,dir,id,dlc,data", f"{t2},NOTE,,,{note2}" if note2 else "1,NOTE,,,start"] + ([loop2] if loop2 else [])
    t = t2
    for c in second_beats:
        b += [f"{t},TX,702,2,00{c:02X}", f"{t},RX,702,2,00{c:02X}"]
        t += 50
    return [("LOG0001.CSV", a), (second_name, b)]


def flood_chain(seq_break):
    """Two rotated flood files; the second starts at sequence 5 or, with seq_break, at 9."""
    a = ["t_ms,dir,id,dlc,data", "1,NOTE,,,start", "1,NOTE,,,mode=loopback bench data"] + [f"{10 + i},TX,7F0,4,0000000{i}" for i in range(5)]
    b = ["t_ms,dir,id,dlc,data", "20,NOTE,,,continues=LOG0001.CSV", "20,NOTE,,,mode=loopback bench data"]
    b += [f"{20 + i},TX,7F0,4,0000000{(9 if seq_break else 5) + i}" for i in range(5)]
    return [("LOG0001.CSV", a), ("LOG0002.CSV", b)]


def daily(rows, name="D0000001.CSV"):
    """A daily file: rows are (t, dir, id_hex, data_hex)."""
    lines = ["t_ms,dir,id,dlc,data", "1,NOTE,,,start", "1,NOTE,,,mode=daily one frame per ID per second", "1,NOTE,,,mode=loopback bench data"]
    lines += [f"{t},{d},{i},{len(x) // 2},{x}" for t, d, i, x in rows]
    return [(name, lines)]


def rotated(first, second):
    """Two rotated files with the given names (a continues= NOTE in the second)."""
    a = ["t_ms,dir,id,dlc,data", "1,NOTE,,,mode=highres every frame", "1,NOTE,,,mode=loopback bench data", "10,TX,702,2,0000"]
    b = ["t_ms,dir,id,dlc,data", f"20,NOTE,,,continues={first}", "20,NOTE,,,mode=highres every frame", "20,NOTE,,,mode=loopback bench data", "30,TX,702,2,0001"]
    return [(first, a), (second, b)]


CASES = [  # (name, lines, expected exit code: 0 pass, 1 fail)
    ("daily: one frame per ID per second", daily([(1000, "TX", "200", "00"), (1500, "TX", "201", "00"), (2000, "TX", "200", "00")]), 0),
    ("daily: exactly 1000 ms apart", daily([(1000, "TX", "200", "00"), (2000, "TX", "200", "00")]), 0),
    ("daily: 998 ms apart (inside the 2 ms jitter)", daily([(1000, "TX", "200", "00"), (1998, "TX", "200", "00")]), 0),
    ("daily: 997 ms apart is too dense", daily([(1000, "TX", "200", "00"), (1997, "TX", "200", "00")]), 1),
    ("daily: oil pressure (203) at 100 ms is allowed", daily([(1000, "TX", "203", "0000"), (1100, "TX", "203", "0000"), (1200, "TX", "203", "0000")]), 0),
    ("daily: engine (100) at 100 ms is allowed", daily([(1000, "RX", "100", "00000000"), (1100, "RX", "100", "00000000")]), 0),
    ("daily: oil pressure 98 ms apart (inside the 2 ms jitter)", daily([(1000, "TX", "203", "0000"), (1098, "TX", "203", "0000")]), 0),
    ("daily: oil pressure 97 ms apart is too dense", daily([(1000, "TX", "203", "0000"), (1097, "TX", "203", "0000")]), 1),
    ("daily: oil pressure at 20 ms (a high-res rate) is too dense", daily([(1000, "TX", "203", "0000"), (1020, "TX", "203", "0000")]), 1),
    ("daily: engine at 20 ms is too dense", daily([(1000, "RX", "100", "00000000"), (1020, "RX", "100", "00000000")]), 1),
    ("daily: the slow analog (200) at 100 ms is too dense", daily([(1000, "TX", "200", "000000000000"), (1100, "TX", "200", "000000000000")]), 1),
    ("daily: ethanol (300) at 100 ms is too dense", daily([(1000, "RX", "300", "00000000"), (1100, "RX", "300", "00000000")]), 1),
    ("bench engine frames are RX only (more than the pairing tolerance)", good_log() + [f"{2000 + 20 * i},RX,100,4,00000000" for i in range(8)], 0),
    ("daily: the same ID 500 ms apart", daily([(1000, "TX", "200", "00"), (1500, "TX", "200", "00")]), 1),
    ("daily: TX and RX of one ID are separate streams", daily([(1000, "TX", "200", "00"), (1010, "RX", "200", "00")]), 0),
    ("daily: heartbeat gaps and loopback pairing are not expected", daily([(1000, "TX", "702", "0000"), (2000, "TX", "702", "0005"), (3000, "TX", "702", "0009")]), 0),
    ("daily: flood sequence gaps are expected in a reduced stream", daily([(1000, "TX", "7F0", "00000000"), (2000, "TX", "7F0", "00000064"), (3000, "TX", "7F0", "000000FA")]), 0),
    ("high-res: the same heartbeat gaps ARE a failure", [("LOG0001.CSV", ["t_ms,dir,id,dlc,data", "1,NOTE,,,mode=loopback bench data", "1,NOTE,,,mode=highres every frame", "10,TX,702,2,0000", "10,RX,702,2,0000", "20,TX,702,2,0005", "20,RX,702,2,0005"])], 1),
    ("new names: D files in order", rotated("D0000001.CSV", "D0000002.CSV"), 0),
    ("new names: M files in order", rotated("M0000041.CSV", "M0000042.CSV"), 0),
    ("new names: a D file is not the next after a gap", rotated("D0000001.CSV", "D0000003.CSV"), 1),
    ("new names: 7-digit rollover 9999999 -> 10000000 is not a valid name", rotated("M0000008.CSV", "M0000010.CSV"), 1),
    ("rotated log: two files in order", chain(), 0),
    ("rotated log: next file is not number+1", chain(second_name="LOG0003.CSV"), 1),
    ("rotated log: no continues= note", chain(note2=None), 1),
    ("rotated log: continues= names the wrong file", chain(note2="continues=LOG0007.CSV"), 1),
    ("rotated log: time restarts at the boundary", chain(t2=100), 1),
    ("rotated log: heartbeat skips at the boundary, silent", chain(second_beats=range(12, 22)), 1),
    ("rotated log: second file lacks the bench label", chain(loop2=None), 1),
    ("rotated flood: sequence continues", flood_chain(False), 0),
    ("rotated flood: sequence gap at the boundary, silent", flood_chain(True), 1),
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
    ("bench button frames are RX only", good_log() + ["2000,RX,400,1,00", "2100,RX,400,1,01", "2200,RX,400,1,01", "2300,RX,400,1,00"], 0),
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
