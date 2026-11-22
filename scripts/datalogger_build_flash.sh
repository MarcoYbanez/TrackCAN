#!/usr/bin/env bash
# Build and flash the DataLogger Node (Teensy 4.1, Zephyr).
#   scripts/datalogger_build_flash.sh build [variant]   build (default variant: full)
#   scripts/datalogger_build_flash.sh flash [variant]   flash it: plug the Teensy in by USB, then PRESS ITS PROGRAM BUTTON when asked
#   scripts/datalogger_build_flash.sh all [variant]     build, then flash
# Variants, to find where the boot hang is (TCAN-17, see docs/journal/2026-11-22-tcan17-datalogger-boot-hang-handoff.md):
#   full       CAN logic + SD logger                       -> build/datalogger_sd
#   nosd       CAN logic, no SD                            -> build/datalogger_nosd
#   scaffold   neither: the original bring-up firmware     -> build/datalogger_scaffold
# The Teensy has no debug probe: it is flashed by its USB bootloader (teensy_loader_cli).
# Paths below are this Mac's; override them with environment variables.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export ZEPHYR_BASE="${ZEPHYR_BASE:-/opt/toolchains/zephyr_support/zephyr}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-gnuarmemb}"
export GNUARMEMB_TOOLCHAIN_PATH="${GNUARMEMB_TOOLCHAIN_PATH:-/opt/homebrew}"
VENV="${ZEPHYR_VENV:-$HOME/zephyrproject/.venv}"
export PATH="$VENV/bin:$PATH"
VARIANT="${2:-full}"
EXTRA=()
case "$VARIANT" in
    full) BUILD="$ROOT/build/datalogger_sd" ;;
    nosd) BUILD="$ROOT/build/datalogger_nosd"
          EXTRA=(-DDATALOGGER_SD=OFF -DCONFIG_DISK_ACCESS=n -DCONFIG_FILE_SYSTEM=n -DCONFIG_FAT_FILESYSTEM_ELM=n) ;;
    scaffold) BUILD="$ROOT/build/datalogger_scaffold"
          EXTRA=(-DDATALOGGER_SD=OFF -DDATALOGGER_CAN=OFF -DCONFIG_CAN=n -DCONFIG_DISK_ACCESS=n -DCONFIG_FILE_SYSTEM=n -DCONFIG_FAT_FILESYSTEM_ELM=n) ;;
    *) echo "variant must be full, nosd or scaffold"; exit 2 ;;
esac

build() {
    west build -b teensy41 -d "$BUILD" "$ROOT/DataLogger_Node" -- \
        -DTRACKCAN_PATH="$ROOT" -DPython3_EXECUTABLE="$VENV/bin/python" "${EXTRA[@]}"
}

flash() {
    command -v teensy_loader_cli >/dev/null || { echo "teensy_loader_cli not found (brew install teensy_loader_cli)"; exit 1; }
    echo "Plug the Teensy in by USB and press its PROGRAM BUTTON (next to the USB connector) now."
    west flash -d "$BUILD" --runner teensy
}

case "${1:-}" in
    build) build ;;
    flash) flash ;;
    all) build; flash ;;
    *) echo "usage: $0 build|flash|all [full|nosd|scaffold]"; exit 2 ;;
esac
