#!/usr/bin/env bash
# Checks that struct block_device in the host stub (irx_imports.h) is the same
# as in PS2SDK's bdm.h, so the host tests exercise the real layout.
set -euo pipefail
cd "$(dirname "$0")"

BDM_H="${PS2SDK:-}/iop/include/bdm.h"
if [ -z "${PS2SDK:-}" ] || [ ! -f "$BDM_H" ]; then
    echo "check_bd_struct: PS2SDK not set or bdm.h missing - SKIPPED"
    exit 0
fi

extract() {
    # struct body, whitespace-normalised
    sed -n '/^struct block_device$/,/^};/p' "$1" | tr -s '[:space:]' ' '
}

sdk="$(extract "$BDM_H")"
stub="$(extract irx_imports.h)"
if [ -z "$sdk" ]; then
    echo "check_bd_struct: struct block_device not found in $BDM_H"
    exit 1
fi
if [ "$sdk" != "$stub" ]; then
    echo "check_bd_struct: struct block_device differs from $BDM_H:"
    diff <(sed -n '/^struct block_device$/,/^};/p' "$BDM_H") \
         <(sed -n '/^struct block_device$/,/^};/p' irx_imports.h) || true
    exit 1
fi
echo "check_bd_struct: struct block_device matches $BDM_H"
