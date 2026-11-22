#!/usr/bin/env bash
# Build and flash the Digital Dash (STM32H745I-DISCO). Two images: M7 (display) and M4 (CAN core).
#   scripts/dash_build_flash.sh build [demo|live]   build both into build/dash_m7 and build/dash_m4
#   scripts/dash_build_flash.sh flash               flash both (the board must be connected by its ST-LINK USB port)
#   scripts/dash_build_flash.sh all [demo|live]     build, then flash
# demo (the default until the FDCAN port exists, TCAN-6) publishes the demo drive and puts a DEMO banner and red frame
# on every frame. live publishes only real CAN data: with no CAN port yet everything shows "--" and the ethanol alert
# comes on after 2 s, which is the correct fail-closed result.
# Paths below are this Mac's; override them with environment variables.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export ZEPHYR_BASE="${ZEPHYR_BASE:-/opt/toolchains/zephyr_support/zephyr}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-gnuarmemb}"
export GNUARMEMB_TOOLCHAIN_PATH="${GNUARMEMB_TOOLCHAIN_PATH:-/opt/homebrew}"
VENV="${ZEPHYR_VENV:-$HOME/zephyrproject/.venv}"
CUBE_BIN="${CUBE_PROGRAMMER_BIN:-$(ls -d /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macos64_*/tools/bin 2>/dev/null | sort | tail -1)}"
export PATH="$VENV/bin:$CUBE_BIN:$PATH"

MODE="${2:-demo}"
case "$MODE" in
    demo) DEMO_FLAG=ON ;;
    live) DEMO_FLAG=OFF ;;
    *) echo "mode must be demo or live"; exit 2 ;;
esac

build() {
    echo "== building the M4 as a $MODE image"
    west build -b "stm32h745i_disco/stm32h745xx/m7" -d "$ROOT/build/dash_m7" "$ROOT/DigitalDash_Node/m7" -- \
        -DTRACKCAN_PATH="$ROOT" -DPython3_EXECUTABLE="$VENV/bin/python"
    west build -b "stm32h745i_disco/stm32h745xx/m4" -d "$ROOT/build/dash_m4" "$ROOT/DigitalDash_Node/m4" -- \
        -DTRACKCAN_PATH="$ROOT" -DPython3_EXECUTABLE="$VENV/bin/python" -DDASH_DEMO="$DEMO_FLAG"
}

flash() {
    command -v STM32_Programmer_CLI >/dev/null || { echo "STM32_Programmer_CLI not found (set CUBE_PROGRAMMER_BIN)"; exit 1; }
    # M4 first, M7 last: the M7 starts the M4 after the final reset.
    west flash -d "$ROOT/build/dash_m4" --runner stm32cubeprogrammer
    west flash -d "$ROOT/build/dash_m7" --runner stm32cubeprogrammer
}

case "${1:-}" in
    build) build ;;
    flash) flash ;;
    all) build; flash ;;
    *) echo "usage: $0 build|flash|all [demo|live]"; exit 2 ;;
esac
