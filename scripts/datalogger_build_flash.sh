#!/usr/bin/env bash
# Build and flash the DataLogger Node (Teensy 4.1, Zephyr).
#   scripts/datalogger_build_flash.sh build [variant]   build (default variant: full)
#   scripts/datalogger_build_flash.sh flash [variant]   flash it over USB (no button if the running firmware has the TCAN-18 USB reboot; else press PROGRAM when asked)
#   scripts/datalogger_build_flash.sh all [variant]     build, then flash
# Variants, to find where the boot hang is (TCAN-17, see docs/journal/2026-11-22-tcan17-datalogger-boot-hang-handoff.md):
#   full       CAN logic + SD logger                       -> build/datalogger_sd
#   nosd       CAN logic, no SD                            -> build/datalogger_nosd
#   loopback   full, with the CAN controller in loopback and a 100 ms console time mark (bench) -> build/datalogger_loopback
#   flood      loopback plus a rising-rate frame generator into the logger (bench) -> build/datalogger_flood
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
    loopback) BUILD="$ROOT/build/datalogger_loopback"
          EXTRA=(-DDATALOGGER_CAN_LOOPBACK=ON -DDATALOGGER_SD_BENCH_CUTTEST=ON) ;;
    flood) BUILD="$ROOT/build/datalogger_flood"
          EXTRA=(-DDATALOGGER_CAN_LOOPBACK=ON -DDATALOGGER_SD_BENCH_FLOOD=ON) ;;
    rotate) BUILD="$ROOT/build/datalogger_rotate"      # bench: flood + a new file every 20 s (age rule only)
          EXTRA=(-DDATALOGGER_CAN_LOOPBACK=ON -DDATALOGGER_SD_BENCH_FLOOD=ON -DDATALOGGER_SD_ROTATE_SECONDS=20 -DDATALOGGER_SD_ROTATE_KB=0) ;;
    rotatesize) BUILD="$ROOT/build/datalogger_rotatesize"  # bench: flood + a new file every 2000 KB (size rule only)
          EXTRA=(-DDATALOGGER_CAN_LOOPBACK=ON -DDATALOGGER_SD_BENCH_FLOOD=ON -DDATALOGGER_SD_ROTATE_SECONDS=0 -DDATALOGGER_SD_ROTATE_KB=2000) ;;
    rollfail) BUILD="$ROOT/build/datalogger_rollfail"   # bench: loopback, a new file every 20 s, the 2nd rollover fails on purpose
          EXTRA=(-DDATALOGGER_CAN_LOOPBACK=ON -DDATALOGGER_SD_ROTATE_SECONDS=20 -DDATALOGGER_SD_ROTATE_KB=0 -DDATALOGGER_SD_TEST_FAIL_ROLL=2) ;;
    size30) BUILD="$ROOT/build/datalogger_size30"      # bench: the REAL 30 MB limit, flood steps of 90 s so about 37 MB are written
          EXTRA=(-DDATALOGGER_CAN_LOOPBACK=ON -DDATALOGGER_SD_BENCH_FLOOD=ON -DDATALOGGER_SD_ROTATE_SECONDS=0 -DDATALOGGER_SD_ROTATE_KB=30720 -DDATALOGGER_SD_BENCH_FLOOD_STEP_MS=90000) ;;
    scaffold) BUILD="$ROOT/build/datalogger_scaffold"
          EXTRA=(-DDATALOGGER_SD=OFF -DDATALOGGER_CAN=OFF -DCONFIG_CAN=n -DCONFIG_DISK_ACCESS=n -DCONFIG_FILE_SYSTEM=n -DCONFIG_FAT_FILESYSTEM_ELM=n) ;;
    *) echo "variant must be full, nosd, scaffold, loopback, flood, rotate, rotatesize, rollfail or size30"; exit 2 ;;
esac

build() {
    west build -b teensy41 -d "$BUILD" "$ROOT/DataLogger_Node" -- \
        -DTRACKCAN_PATH="$ROOT" -DPython3_EXECUTABLE="$VENV/bin/python" ${EXTRA[@]+"${EXTRA[@]}"}
}

flash() {
    command -v teensy_loader_cli >/dev/null || { echo "teensy_loader_cli not found (brew install teensy_loader_cli)"; exit 1; }
    # TCAN-18: a running image with USB is rebooted into the bootloader over USB; otherwise it asks for the PROGRAM button.
    "$ROOT/scripts/teensy_usb_flash.sh" "$BUILD/zephyr/zephyr.hex"
}

case "${1:-}" in
    build) build ;;
    flash) flash ;;
    all) build; flash ;;
    *) echo "usage: $0 build|flash|all [full|nosd|scaffold|loopback|flood|rotate|rotatesize|rollfail|size30]"; exit 2 ;;
esac
