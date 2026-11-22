#!/usr/bin/env bash
# Build and flash the Digital Dash (STM32H745I-DISCO). Two images: M7 (display) and M4 (CAN core).
#   scripts/dash_build_flash.sh build   build both into build/dash_m7 and build/dash_m4
#   scripts/dash_build_flash.sh flash   flash both (the board must be connected by its ST-LINK USB port)
#   scripts/dash_build_flash.sh all     build, then flash
# Paths below are this Mac's; override them with environment variables.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export ZEPHYR_BASE="${ZEPHYR_BASE:-/opt/toolchains/zephyr_support/zephyr}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-gnuarmemb}"
export GNUARMEMB_TOOLCHAIN_PATH="${GNUARMEMB_TOOLCHAIN_PATH:-/opt/homebrew}"
VENV="${ZEPHYR_VENV:-$HOME/zephyrproject/.venv}"
CUBE_BIN="${CUBE_PROGRAMMER_BIN:-$(ls -d /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macos64_*/tools/bin 2>/dev/null | sort | tail -1)}"
export PATH="$VENV/bin:$CUBE_BIN:$PATH"

build() {
    for core in m7 m4; do
        west build -b "stm32h745i_disco/stm32h745xx/$core" -d "$ROOT/build/dash_$core" "$ROOT/DigitalDash_Node/$core" -- \
            -DTRACKCAN_PATH="$ROOT" -DPython3_EXECUTABLE="$VENV/bin/python"
    done
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
    *) echo "usage: $0 build|flash|all"; exit 2 ;;
esac
