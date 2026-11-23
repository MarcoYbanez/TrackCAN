#!/usr/bin/env python3
"""Tests for datalogger_pull.py against a simulated DataLogger on a pseudo-terminal (TCAN-21). The simulated device is written from the
protocol in DataLogger_Node/src/transfer/logger_xfer.h, so it checks the tool, not the firmware (that is tested on the hardware).

    python3 -I scripts/test_datalogger_pull.py
"""
import faulthandler
import importlib.util
import os
import select
import sys
import tempfile
import termios
import threading
import zlib

sys.dont_write_bytecode = True  # no __pycache__ in the repo
faulthandler.dump_traceback_later(60, exit=True)  # a hang becomes a failure with a stack trace
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("pull", os.path.join(HERE, "datalogger_pull.py"))
pull = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pull)
pull.TIMEOUT_S = 0.6  # the real tool waits 5 s


class FakeDevice(threading.Thread):
    """Answers LIST and GET on the master side of a pty."""

    def __init__(self, master, files, corrupt=(), err_open=(), short=(), stale=b""):
        super().__init__(daemon=True)
        self.master, self.files, self.corrupt, self.err_open, self.short = master, files, set(corrupt), set(err_open), set(short)
        self.stale = stale
        self.commands = []
        self.stop = False

    def say(self, data):
        os.write(self.master, data if isinstance(data, bytes) else data.encode())

    def run(self):
        if self.stale:
            self.say(self.stale)  # left over from an abandoned transfer
        buf = b""
        while not self.stop:
            if not select.select([self.master], [], [], 0.05)[0]:
                continue
            buf += os.read(self.master, 4096)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.handle(line.decode().strip())

    def handle(self, line):
        self.commands.append(line)
        if line == "LIST":
            for name, data in sorted(self.files.items()):
                self.say(f"{name} {len(data)}\n")
            self.say("END\n")
        elif line.startswith("GET "):
            name = line[4:]
            if name not in self.files:
                self.say("ERR notfound\n")
            elif name in self.err_open:
                self.say("ERR open\n")
            else:
                data = self.files[name]
                self.say(f"OK {len(data)}\n")
                if name in self.short:
                    self.say(data[: len(data) // 2])  # stops half way: no CRC line
                    return
                sent = bytearray(data)
                crc = zlib.crc32(data)  # the Node's CRC is of the bytes it read; a flipped bit on the wire must be caught
                if name in self.corrupt:
                    sent[len(sent) // 2] ^= 0x01
                self.say(bytes(sent))
                self.say(f"CRC {crc:08x}\n")
        else:
            self.say("ERR badcmd\n")


def with_device(files, **kw):
    master, slave = os.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO  # a USB serial port does not echo; a pty would feed the device its own bytes back
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    dev = FakeDevice(master, files, **kw)
    dev.start()
    return dev, os.ttyname(slave), slave


FILES = {
    "LOG0001.CSV": b"t_ms,dir,id,dlc,data\n1,NOTE,,,start\n",
    "LOG0002.CSV": bytes(range(256)) * 100,  # binary-ish and large: nothing may be altered or cut
    "LOG0003.CSV": b"x" * 5000,
}
BIG = {"LOG0099.CSV": (b"0123456789abcdef" * 1024 * 1024 * 2)[: 31 * 1024 * 1024]}  # 31 MB: the size a 30 MB rotation produces
results = []


def check(name, ok):
    results.append(ok)
    print(("ok    " if ok else "WRONG ") + name)


def run_pull(dev_kwargs, dest, force=False):
    dev, path, slave = with_device(FILES, **dev_kwargs)
    port = pull.Port(path)
    try:
        return pull.pull(port, dest, force), dev
    finally:
        port.close()
        dev.stop = True
        os.close(slave)


def main():
    # The tool never asks for 134 baud (the Node's reboot request).
    dev, path, slave = with_device(FILES)
    port = pull.Port(path)
    check("port speed is not 134 baud", termios.tcgetattr(port.fd)[4] != termios.B134 and termios.tcgetattr(port.fd)[5] != termios.B134)
    # list
    listed = pull.list_files(port)
    check("list returns every file with its size", listed == sorted((n, len(d)) for n, d in FILES.items()))
    port.close()
    dev.stop = True
    os.close(slave)

    with tempfile.TemporaryDirectory() as d:
        (got, skipped, failed), dev = run_pull({}, d)
        same = all(open(os.path.join(d, n), "rb").read() == data for n, data in FILES.items())
        check("pull: all files downloaded, byte for byte", len(got) == 3 and not skipped and not failed and same)

        (got, skipped, failed), dev = run_pull({}, d)
        check("pull again: everything skipped, nothing requested", not got and len(skipped) == 3 and not [c for c in dev.commands if c.startswith("GET")])

        (got, skipped, failed), dev = run_pull({}, d, force=True)
        check("--force downloads again", len(got) == 3 and not skipped)

    with tempfile.TemporaryDirectory() as d:
        (got, skipped, failed), dev = run_pull({"corrupt": ["LOG0002.CSV"]}, d)
        check("a flipped bit: that file fails, the others succeed", failed == ["LOG0002.CSV"] and sorted(got) == ["LOG0001.CSV", "LOG0003.CSV"])
        check("a corrupt file is NOT left under its real name", not os.path.exists(os.path.join(d, "LOG0002.CSV")))
        check("a failed transfer leaves no file at all", sorted(os.listdir(d)) == ["LOG0001.CSV", "LOG0003.CSV"])

    with tempfile.TemporaryDirectory() as d:
        (got, skipped, failed), dev = run_pull({"err_open": ["LOG0003.CSV"]}, d)
        check("ERR open: reported, the others still arrive", failed == ["LOG0003.CSV"] and sorted(got) == ["LOG0001.CSV", "LOG0002.CSV"])

    with tempfile.TemporaryDirectory() as d:
        (got, skipped, failed), dev = run_pull({"short": ["LOG0001.CSV"]}, d)
        check("a transfer that stops half way fails, not written", failed == ["LOG0001.CSV"] and not os.path.exists(os.path.join(d, "LOG0001.CSV")))

    with tempfile.TemporaryDirectory() as d:
        (got, skipped, failed), dev = run_pull({"stale": b"junk from an abandoned GET\nCRC 00000000\n"}, d)
        check("stale bytes in the port are discarded before a command", len(got) == 3 and not failed)

    # A file that exists with the right size is skipped; with the WRONG size it is fetched again.
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, "LOG0003.CSV"), "wb").write(b"x" * 10)
        (got, skipped, failed), dev = run_pull({}, d)
        check("an incomplete copy (wrong size) is downloaded again", "LOG0003.CSV" in got and open(os.path.join(d, "LOG0003.CSV"), "rb").read() == FILES["LOG0003.CSV"])

    # A rotation-sized file: must arrive complete and fast (the old buffer handling was quadratic and the Node gave up on it).
    import time
    dev, path, slave = with_device(BIG)
    port = pull.Port(path)
    pull.TIMEOUT_S = 5.0
    started = time.time()
    try:
        data, seconds = pull.get_file(port, "LOG0099.CSV")
        check("a 31 MB file arrives complete and CRC ok", data == BIG["LOG0099.CSV"])
        check(f"a 31 MB file takes under 20 s ({time.time() - started:.1f} s)", time.time() - started < 20)
    except pull.TransferError as e:
        check(f"a 31 MB file arrives ({e})", False)
    finally:
        port.close()
        dev.stop = True
        os.close(slave)
        pull.TIMEOUT_S = 0.6

    # chains(): a file that says continues= joins the run before it; anything else starts a new run.
    with tempfile.TemporaryDirectory() as d:
        def mk(n, note):
            path = os.path.join(d, f"LOG{n:04d}.CSV")
            open(path, "w").write("t_ms,dir,id,dlc,data\n" + (f"5,NOTE,,,{note}\n" if note else "") + "6,TX,200,8,0000000000000000\n")
            return path
        p1, p2, p3, p4, p5 = mk(1, "start"), mk(2, "continues=LOG0001.CSV"), mk(3, "continues=LOG0002.CSV"), mk(4, "start"), mk(5, "continues=LOG0004.CSV")
        runs = pull.chains([p5, p3, p1, p4, p2])  # given out of order: it sorts
        check("chains: two runs split at the file without continues=", runs == [[p1, p2, p3], [p4, p5]])
        check("chains: a single file is one run", pull.chains([p1]) == [[p1]])
        check("chains: a continues= file with no run before it starts one", pull.chains([p2]) == [[p2]])

    print(f"{sum(results)}/{len(results)} as expected")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
