#!/usr/bin/env bash
# Runs fsck.exfat -n on every exFAT volume created by the host tests.
#
# Needs root (losetup) and exfatprogs:
#   sudo apt install exfatprogs
#   make test
#   sudo tests/host/fsck_images.sh
#
# Safety: only regular files under /tmp/ps2-exfat-test/ are attached, read-only
# (losetup -r), at the volume offset. Block devices are never touched.
set -euo pipefail

DIR=/tmp/ps2-exfat-test
LIST="$DIR/images.txt"

if [ "$(id -u)" != 0 ]; then
    echo "Run as root: sudo $0"; exit 1
fi
command -v fsck.exfat >/dev/null || { echo "fsck.exfat missing: sudo apt install exfatprogs"; exit 1; }
[ -f "$LIST" ] || { echo "$LIST not found - run 'make test' first"; exit 1; }

rc=0
while read -r img off size; do
    case "$img" in
        "$DIR"/*.img) ;;
        *) echo "refusing $img (not in $DIR)"; rc=1; continue ;;
    esac
    if [ ! -f "$img" ] || [ -L "$img" ] || [ -b "$img" ]; then
        echo "refusing $img (not a regular file)"; rc=1; continue
    fi

    loop="$(losetup -f --show -r -o "$off" --sizelimit "$size" "$img")"
    echo "== $(basename "$img")  offset=$off  size=$size  -> $loop"
    if fsck.exfat -n "$loop"; then
        echo "   -> clean"
    else
        echo "   -> fsck.exfat exit code $?"; rc=1
    fi
    losetup -d "$loop"
done < "$LIST"

exit $rc
