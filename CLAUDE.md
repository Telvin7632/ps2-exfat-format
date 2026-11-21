# CLAUDE.md

PS2 homebrew: formats the internal HDD (BDM device `ata0`) as MBR + exFAT
with FatFs `f_mkfs`. EE program `ee/` + IOP module `iop/` (exfatfmt.irx).
See README.md for the user-facing description.

## Commands

```bash
make                 # downloads FatFs into iop/ on first run, builds iop/ then ee/
make distclean && make   # full clean rebuild (also re-downloads FatFs)
mipsel-none-elf-nm iop/exfatfmt.notiopmod.elf | grep " U "   # must print nothing
make test            # host tests (gcc + ASan/UBSan), sparse images in /tmp/ps2-exfat-test
sudo tests/host/fsck_images.sh   # fsck.exfat -n via read-only loop devices (user runs sudo)
make test-clean      # remove host build + /tmp/ps2-exfat-test
```

Toolchain: `/usr/local/ps2dev`, `PS2DEV`/`PS2SDK` set in `~/.bashrc`.
EE `mips64r5900el-ps2-elf-gcc`, IOP `mipsel-none-elf-gcc`. Goal: zero warnings.

## Rules from the owner

* Changes to formatter behaviour (f_mkfs parameters, device selection,
  confirmations) only after the owner's explicit OK.
* Never access `/dev/sd*` or real disks. Disk images only under `/tmp`.
* Do not install system packages; give the `sudo` commands to the owner.
* No push / no GitHub repo creation without confirmation.
* Docs state only what code or checks confirm.

## Key facts and decisions (verified against PS2SDK sources)

* Module result: `(value << 2) | MODULE_NO_RESIDENT_END`; MODLOAD passes the
  full value to the EE and only checks `& 3` (modload.c). EE decodes `>> 2`.
* `_start` runs on MODLOAD's thread at priority 8; the worker is 0x40, so
  `_start` resumes at `SignalSema` before the worker has left `worker()`.
  MODLOAD frees the module right after `_start` returns, so `_start` loops on
  `DeleteThread() == KE_NOT_DORMANT` with no cap (a 1000-try cap was removed).
* Device: `bdm_get_bd()` returns raw `g_mount[i].bd` (not BDM's cache device).
  Pick `name=="ata" && devNr==0 && parNr==0`. BDM's built-in partition driver
  adds `ata0pN` devices; they must never be chosen.
* `ata_bd` caps `sectorCount` at 0xFFFFFFFF (u32 `total_sectors_lba48`), so on
  the console GPT (`FF_MIN_GPT 0x100000000`) is never reached: disks > 2 TiB
  get a 2 TiB MBR volume. The host test "3TB-raw" (GPT) is not console-reachable.
* `ata_bd` read/write return `count` or `-EIO`; `flush` is `void`. Sector size
  is hard-coded 512.
* MBR partition at LBA 63 (FatFs `N_SEC_TRACK`), cluster heap 1 MiB aligned
  via `GET_BLOCK_SIZE` = 2048. Intended; don't "fix".
* Pad: `pad_wait_press()` re-reads the held buttons before each prompt, so
  only presses after the prompt count (owner-approved change).
* Known, intentionally unchanged (owner chose not to): INFO returns size and
  ERR_* codes through the same channel, so an INFO-time ERR_THREAD (103) looks
  like a 103 MiB disk.
* FatFs source differs by origin: elm-chan `ff16.zip` = plain R0.16; the
  GitHub fallback `abbrev/fatfs` = "R0.16 w/patch 2". Not pinned/checksummed.

## Host tests (tests/host)

* Compiles the real `iop/main.c`, `iop/diskio.c`, FatFs. IOP sources are
  copied to `tests/host/build/src/` first: `#include "irx_imports.h"` looks in
  the including file's directory first, so compiling in `iop/` would pick the
  PS2SDK header instead of the stub.
* `_start` is renamed with `-D_start=exfatfmt_start` (clashes with crt `_start`).
* Stub `StartThread` runs the worker synchronously; `DeleteThread` can be told
  to return `KE_NOT_DORMANT` N times.
* `check_bd_struct.sh` diffs the stub's `struct block_device` against
  `$PS2SDK/iop/include/bdm.h` (skips if PS2SDK unset).
* Error codes 100..104 are duplicated in `test_format.c`; keep in sync with `iop/main.c`.

## Not verified yet

Real PS2, OPL with the result, FTP via wLaunchELF R3Z.
Done: CI green; PCSX2 2026-11-21 (40 GiB image formatted, fsck clean,
held-X-during-load correctly ignored). EE side has no automated tests.
