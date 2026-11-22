#!/bin/bash
# Flash the DataLogger (Teensy 4.1) without pressing PROGRAM (TCAN-18).
#   scripts/teensy_usb_flash.sh [hex file] [serial port]
# 1. finds the running firmware's USB serial port ("TrackCAN DataLogger"), 2. sets it to 134 baud (PJRC's reboot request),
# 3. waits for the bootloader (USB 16C0:0478), 4. flashes with teensy_loader_cli. If no firmware answers (it has no USB, or it
# is hung) press the PROGRAM button: teensy_loader_cli -w waits for that too.
set -euo pipefail
cd "$(dirname "$0")/.."
HEX="${1:-build/datalogger_bl/zephyr/zephyr.hex}"
PORT="${2:-}"
[ -f "$HEX" ] || { echo "no hex file: $HEX"; exit 1; }

if [ -z "$PORT" ]; then
  PORT=$(ioreg -r -c IOUSBHostDevice -l -w0 | awk '/"USB Product Name" = "TrackCAN DataLogger"/{f=1} f&&/IOCalloutDevice/{gsub(/.*= "|"/,""); print; exit}' || true)  # || true: awk exits early, ioreg gets SIGPIPE, pipefail would abort
fi

if [ -n "$PORT" ]; then
  echo "requesting the bootloader on $PORT"
  stty -f "$PORT" 134 || true   # opens the port and sets 134 baud: the firmware sees SET_LINE_CODING
else
  echo "no running TrackCAN DataLogger found: press the PROGRAM button on the Teensy"
fi

# -w waits for the bootloader to appear (after the request or the button press)
teensy_loader_cli --mcu=TEENSY41 -w -v "$HEX"
