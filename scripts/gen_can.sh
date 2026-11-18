#!/usr/bin/env bash
# Regenerate can/generated/trackcan.{c,h} from can/trackcan.dbc.
# Needs the cantools Python package: python3 -m venv .venv && .venv/bin/pip install cantools
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CANTOOLS="${CANTOOLS:-cantools}"
"$CANTOOLS" generate_c_source \
    --database-name trackcan \
    --no-floating-point-numbers \
    -o "$ROOT/can/generated" \
    "$ROOT/can/trackcan.dbc"
