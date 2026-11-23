#!/usr/bin/env python3
"""Copies the DataLogger's SD logs to the Mac over USB, without taking the card out (TCAN-21).

    python3 -I scripts/datalogger_pull.py list [--port /dev/cu.usbmodemXXXX]
    python3 -I scripts/datalogger_pull.py pull [--dest logs] [--force] [--check] [--loopback] [--port ...]

Protocol and rules: DataLogger_Node/src/transfer/logger_xfer.h. The Node serves only closed LOGnnnn.CSV files (never the one being written),
read-only. Each file is checked against the CRC-32 the Node sends; a file is written only once it is complete and
correct (a failed transfer leaves nothing behind). Files already in the destination with the same size are skipped (a closed log never changes) unless --force. With --check the
downloaded logs are run through scripts/check_datalogger_log.py, one run (a chain of rotated files) at a time.
Standard library only. Never sets the port to 134 baud: that is the Node's reboot-into-bootloader request.
"""
import argparse
import os
import re
import select
import subprocess
import sys
import termios
import time
import tty
import zlib

BAUD = termios.B115200  # ignored by a USB serial port, but it must NOT be 134
TIMEOUT_S = 5.0
LIST_LINE = re.compile(r"^(LOG\d{4}\.CSV) (\d+)$")


class TransferError(Exception):
    pass


def find_port():
    """The Node's USB serial port, found by its product name (ioreg), as scripts/teensy_usb_flash.sh does."""
    out = subprocess.run(["ioreg", "-r", "-c", "IOUSBHostDevice", "-l", "-w0"], capture_output=True, text=True).stdout
    i = out.find('"USB Product Name" = "TrackCAN DataLogger"')
    m = re.search(r'"IOCalloutDevice" = "([^"]+)"', out[i:]) if i >= 0 else None
    if not m:
        raise TransferError("no TrackCAN DataLogger on USB (is it plugged in and running the TCAN-18/21 firmware?)")
    return m.group(1)


class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd, termios.TCSANOW)  # not TCSAFLUSH: that waits for pending output and can hang on an unresponsive port
        attrs = termios.tcgetattr(self.fd)
        attrs[4] = attrs[5] = BAUD
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buf = b""

    def close(self):
        os.close(self.fd)

    def _fill(self, timeout):
        if not select.select([self.fd], [], [], timeout)[0]:
            raise TransferError("timed out waiting for the DataLogger")
        data = os.read(self.fd, 65536)
        if not data:
            raise TransferError("the DataLogger closed the port")
        self.buf += data

    def drain(self):
        """Throws away anything stale (an earlier, abandoned transfer)."""
        while select.select([self.fd], [], [], 0.2)[0]:
            os.read(self.fd, 65536)
        self.buf = b""

    def send_line(self, text):
        os.write(self.fd, (text + "\n").encode())

    def read_line(self, timeout=None):
        timeout = TIMEOUT_S if timeout is None else timeout
        while b"\n" not in self.buf:
            self._fill(timeout)
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def read_exact(self, n, timeout=None):
        timeout = TIMEOUT_S if timeout is None else timeout
        while len(self.buf) < n:
            self._fill(timeout)
        data, self.buf = self.buf[:n], self.buf[n:]
        return data


def list_files(port):
    port.drain()
    port.send_line("LIST")
    files = []
    while True:
        line = port.read_line()
        if line == "END":
            return files
        if line.startswith("ERR"):
            raise TransferError(f"LIST failed: {line}")
        m = LIST_LINE.match(line)
        if not m:
            raise TransferError(f"unexpected reply to LIST: {line!r}")
        files.append((m[1], int(m[2])))


def get_file(port, name):
    """Returns (bytes, seconds). Raises TransferError unless the size and CRC-32 both match."""
    port.drain()
    port.send_line(f"GET {name}")
    head = port.read_line()
    m = re.match(r"^OK (\d+)$", head)
    if not m:
        raise TransferError(head)
    size = int(m[1])
    started = time.time()
    data = port.read_exact(size)
    trailer = port.read_line()
    seconds = time.time() - started
    m = re.match(r"^CRC ([0-9a-f]{8})$", trailer)
    if not m:
        raise TransferError(f"transfer failed after {size} announced bytes ({trailer!r})")
    if int(m[1], 16) != zlib.crc32(data):
        raise TransferError(f"CRC mismatch (Node {m[1]}, received {zlib.crc32(data):08x})")
    return data, seconds


def chains(paths):
    """Splits log files (in number order) into rotated runs: a file whose header says `continues=` belongs to the run before it, any other
    file starts a new run. Each run is checked as one stream by check_datalogger_log.py."""
    runs = []
    for path in sorted(paths):
        with open(path, errors="replace") as f:
            head = [f.readline() for _ in range(6)]
        if runs and any(re.match(r"^\d+,NOTE,,,continues=", line) for line in head):
            runs[-1].append(path)
        else:
            runs.append([path])
    return runs


def pull(port, dest, force):
    os.makedirs(dest, exist_ok=True)
    got, skipped, failed = [], [], []
    for name, size in sorted(list_files(port)):
        path = os.path.join(dest, name)
        if not force and os.path.exists(path) and os.path.getsize(path) == size:
            skipped.append(name)
            continue
        try:
            data, seconds = get_file(port, name)
        except TransferError as e:
            print(f"  FAILED {name}: {e}")
            failed.append(name)
            continue
        with open(path, "wb") as f:  # only after the size and CRC-32 have been verified: a bad transfer never reaches the disk
            f.write(data)
        rate = len(data) / seconds / 1e6 if seconds > 0 else 0
        print(f"  {name}: {len(data)} bytes, CRC ok, {rate:.2f} MB/s")
        got.append(name)
    return got, skipped, failed


def main(argv):
    ap = argparse.ArgumentParser(description="Copy the DataLogger's SD logs to the Mac over USB.")
    ap.add_argument("command", choices=["list", "pull"])
    ap.add_argument("--port", help="serial port (default: found by its USB product name)")
    ap.add_argument("--dest", default="logs", help="where pull puts the files (default: ./logs)")
    ap.add_argument("--force", action="store_true", help="download files that are already in the destination")
    ap.add_argument("--check", action="store_true", help="run check_datalogger_log.py on the downloaded files")
    ap.add_argument("--loopback", action="store_true", help="with --check: the logs are bench (loopback) data")
    args = ap.parse_args(argv)

    try:
        port = Port(args.port or find_port())
    except (TransferError, OSError) as e:
        print(f"error: {e}")
        return 2
    try:
        if args.command == "list":
            files = list_files(port)
            for name, size in sorted(files):
                print(f"{name} {size}")
            print(f"{len(files)} closed file(s)")
            return 0
        got, skipped, failed = pull(port, args.dest, args.force)
        print(f"{len(got)} downloaded, {len(skipped)} already there, {len(failed)} failed")
        if args.check and got:
            checker = os.path.join(os.path.dirname(os.path.abspath(__file__)), "check_datalogger_log.py")
            bad = 0
            for run in chains([os.path.join(args.dest, n) for n in got]):
                print(f"--- run of {len(run)} file(s): {os.path.basename(run[0])} .. {os.path.basename(run[-1])}")
                cmd = [sys.executable, "-I", checker] + (["--loopback"] if args.loopback else []) + run
                bad += subprocess.run(cmd).returncode != 0
            print(f"{bad} run(s) failed the check" if bad else "all runs passed the check")
            return 1 if (bad or failed) else 0
        return 1 if failed else 0
    except TransferError as e:
        print(f"error: {e}")
        return 1
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
