#!/usr/bin/env bash
# Downloads ChaN's FatFs into iop/ and configures ffconf.h for exFAT + f_mkfs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/iop"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

ok=0
for ver in 16 15a 15; do
    url="https://elm-chan.org/fsw/ff/arc/ff${ver}.zip"
    echo ">> trying $url"
    if curl -fsSL "$url" -o "$TMP/ff.zip"; then ok=1; break; fi
done
if [ "$ok" = 1 ]; then
    unzip -q "$TMP/ff.zip" -d "$TMP/ff"
else
    echo ">> elm-chan.org unavailable, using the GitHub mirror (abbrev/fatfs)"
    if ! git clone -q --depth 1 https://github.com/abbrev/fatfs "$TMP/ff"; then
        echo "!! Could not download FatFs. Get it manually from https://elm-chan.org/fsw/ff/"
        echo "   and copy ff.c ff.h ffconf.h diskio.h ffunicode.c from its source/ folder into iop/"
        exit 1
    fi
fi
SRC="$(dirname "$(find "$TMP/ff" -name ff.c | head -n 1)")"
cp "$SRC"/{ff.c,ff.h,ffconf.h,diskio.h,ffunicode.c} "$DEST/"

CONF="$DEST/ffconf.h"
set_opt() {
    if ! grep -Eq "^#define[[:space:]]+$1[[:space:]]" "$CONF"; then
        echo "!! option $1 not found in ffconf.h"; exit 1
    fi
    sed -i -E "s|^#define[[:space:]]+$1[[:space:]].*|#define $1\t$2|" "$CONF"
}

set_opt FF_FS_READONLY 0
set_opt FF_USE_MKFS    1
set_opt FF_USE_LFN     1            # exFAT needs LFN (static buffer is fine here)
set_opt FF_CODE_PAGE   437          # default 932 (Japanese) would add ~100 KB of tables
set_opt FF_FS_EXFAT    1
set_opt FF_LBA64       1            # disks > 2 TiB
set_opt FF_MIN_GPT     0x100000000  # use MBR whenever possible (OPL expects MBR)
set_opt FF_FS_NORTC    1            # PS2 IOP has no simple RTC access here

echo ">> FatFs ready in iop/ ($(grep -m1 -o 'R0\.[0-9a-z]*' "$DEST/ff.h" || echo '?'))"
