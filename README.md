# ps2-exfat-format

[Українська версія](README.uk.md)

A PlayStation 2 homebrew program that formats the console's **internal HDD**
(an ATA drive on the network adapter) as **exFAT**, directly on the console.
The result is an MBR with a single exFAT partition, which Open PS2 Loader can
use through its BDM HDD (ATA) mode and which a PC can read and write.

> [!WARNING]
> **Everything on the HDD is erased.** That includes all APA partitions,
> installed games, HDD-OSD (Browser 2.0), and FMCB/PS2BBL installed on the HDD.
> Start this program from a memory card or USB stick, not from the HDD.
> **POPStarter does not work from an exFAT HDD** (it needs APA), so PS1 games
> installed for POPStarter on the HDD stop working.

## What you get

* MBR partition table with one partition of type `0x07`, starting at LBA 63.
* exFAT inside it. Cluster size is chosen by FatFs. In the host tests it was
  128 KiB for every size tested (40 GB to 2 TiB). The cluster heap (data area)
  starts on a 1 MiB boundary of the disk.
* **Disks larger than 2 TiB: only the first 2 TiB are used.** PS2SDK's `ata_bd`
  driver stores the drive size in a 32-bit field and reports at most
  2^32 − 1 sectors. FatFs would create GPT only at 2^32 sectors and above, so
  on the console the result is always MBR (see `ps2atad.c` in PS2SDK:
  `total_sectors_lba48 = 0xffffffff`).

## Requirements

**On the console**

* A "fat" PS2 with a network adapter that has an HDD connector, and an ATA
  hard drive (IDE, or SATA through an adapter) attached to it. Only the master
  device (`ata0`) is ever selected.
* A way to start an ELF that does not depend on the HDD, e.g. wLaunchELF from
  a memory card or USB stick (FMCB/PS2BBL on the memory card).
* A controller in port 1.
* Not supported: PSX (DESR) units, whose HDD also holds the DVR's data; drives
  with 4 KiB logical sectors (ata_bd always reports 512-byte sectors).

**For building**

* Linux or WSL (Ubuntu), with `make`, `curl`, `unzip` and `git`.
* The PS2DEV toolchain (EE `mips64r5900el-ps2-elf-gcc`, IOP `mipsel-none-elf-gcc`, PS2SDK).
* For the PC tests: `gcc` and, for the filesystem check, `exfatprogs`.

## Toolchain in WSL

In PowerShell (as administrator), if WSL is not installed yet:

```powershell
wsl --install -d Ubuntu
```

Then in the Ubuntu terminal:

```bash
sudo apt update
sudo apt install -y build-essential git curl unzip exfatprogs

# prebuilt PS2DEV toolchain (EE + IOP + PS2SDK)
sudo mkdir -p /usr/local/ps2dev
sudo chown -R "$USER": /usr/local/ps2dev
curl -L -o /tmp/ps2dev.tar.gz \
  https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-latest.tar.gz
tar -xf /tmp/ps2dev.tar.gz --strip-components 1 -C /usr/local/ps2dev

cat >> ~/.bashrc <<'EOF'
export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin
EOF
source ~/.bashrc

mips64r5900el-ps2-elf-gcc --version
```

## VS Code

1. Install VS Code on Windows and the **WSL** extension (`ms-vscode-remote.remote-wsl`).
2. Clone the project **inside WSL** (not under `/mnt/c/...`, which builds much slower) and open it:
   ```bash
   git clone https://github.com/Telvin7632/ps2-exfat-format.git
   cd ps2-exfat-format
   code .
   ```
3. Accept the recommended extensions (C/C++, Makefile Tools).
4. IntelliSense: in the status bar pick the configuration **PS2 EE (ee/)** or
   **PS2 IOP (iop/)** depending on which file you edit (`.vscode/c_cpp_properties.json`).
5. `Ctrl+Shift+B` runs the **Build** task. The tasks set `PS2DEV`, `PS2SDK` and
   `PATH` themselves (`.vscode/tasks.json`).

## Building

```bash
make            # first run downloads FatFs into iop/ (tools/get_fatfs.sh)
make clean      # remove build output
make distclean  # also remove the downloaded FatFs
```

Output: `ee/exfat-format.elf` (about 1.4 MB, all IOP drivers embedded).
The build produces no compiler warnings, and `exfatfmt.irx` has no unresolved
symbols (`mipsel-none-elf-nm iop/exfatfmt.notiopmod.elf | grep " U "` prints nothing).

FatFs comes from elm-chan.org (`ff16.zip`, falling back to `ff15a`, `ff15`)
or, if that site is unreachable, from the GitHub mirror `abbrev/fatfs`. The
mirror currently carries "R0.16 w/patch 2", which differs slightly from the
plain R0.16 zip.

## Tests on the PC

```bash
make test                       # build + run the host tests
sudo tests/host/fsck_images.sh  # fsck.exfat -n on every created volume
make test-clean                 # remove the test build and the images
```

`make test` compiles the **real** `iop/main.c` and `iop/diskio.c` together
with the same FatFs configuration for the PC. Only the IOP services (threads,
semaphores, memory, `bdm_get_bd`) are replaced by stubs, and `_start` is
renamed so it can be called from the test. The test checks:

* `struct block_device` in the stub matches `$PS2SDK/iop/include/bdm.h`;
* the module protocol: bad arguments, no `ata0`, a non-512-byte-sector
  device, and that `_start` waits until the worker thread can be deleted;
* that only `ata0` (unit 0, whole disk) is used. Decoy devices `mass0`,
  `ata1` and `ata0p1` fail the test if they are read or written;
* sparse images in `/tmp/ps2-exfat-test/`: 40 GB and 500 GB (MBR), a 3 TB
  disk as `ata_bd` reports it (2^32 − 1 sectors, MBR), and the full 3 TB
  (GPT; not reachable on the console today). For each one: INFO size, FORMAT
  result, partition table, exFAT boot sector, 1 MiB alignment, then
  `f_mount`, writing and reading back a file larger than 4 GiB.

`fsck_images.sh` attaches only regular files from `/tmp/ps2-exfat-test/`,
read-only (`losetup -r -o <offset> --sizelimit <size>`), and runs
`fsck.exfat -n` on them. It never touches a real disk.

## Testing in PCSX2

1. PCSX2 → Settings → **Network & HDD**: enable the network adapter and the
   HDD, and create an HDD image (for example 40 GB). PCSX2 stores it as a raw
   disk image. If the setting is a bare file name (default `DEV9hdd.raw`),
   PCSX2 puts the file in its `inis` folder, e.g.
   `Documents\PCSX2\inis\DEV9hdd.raw`.
2. Start `ee/exfat-format.elf` (System → Start File, or the VS Code task
   **Run in PCSX2**; adjust the path to `pcsx2-qt.exe` in `.vscode/tasks.json`).
3. Press **X**, then **START**. Wait for `DONE`, then close PCSX2.
4. Check the image in WSL:
   ```bash
   IMG="/mnt/c/Users/<you>/Documents/PCSX2/inis/DEV9hdd.raw"   # your image path
   LOOP=$(sudo losetup -f --show -r -P "$IMG")
   sudo fsck.exfat -n "${LOOP}p1"
   sudo losetup -d "$LOOP"
   ```
   If `losetup` cannot attach a file on `/mnt/c`, copy it into WSL first
   (`cp --sparse=always`).

## Running on the console

1. Copy `exfat-format.elf` to a FAT32 USB stick.
2. Start it from wLaunchELF (booted from the memory card, not from the HDD).
3. The program loads the drivers and shows the size of the internal HDD.
4. Press **X** to continue, then **START** to format. Any other button
   cancels, and nothing is written. Only presses made after a prompt appears
   count; a button that is already held must be released first.
5. After `DONE`, power-cycle the console.

### Using the disk with OPL

The disk is a normal MBR + exFAT disk. In an OPL build with BDM support,
enable the HDD (ATA) BDM device and put games into `DVD/` and `CD/` on the
disk, as for other BDM devices.

Tested with OPL Beta 1.2.0 2271 (b3a2199): from this HDD OPL lists only ISOs
with the old naming `SLUS_205.52.Name.iso` (game ID, dot, name); those boot.
ISOs with plain names (`Name.iso`) are not listed from the HDD, while the same
names are listed from a USB stick. Reported to OPL as
[issue #1805](https://github.com/ps2homebrew/Open-PS2-Loader/issues/1805).
The BDM HDD icon in OPL is labelled "GPT" for MBR disks too; it does not mean
the disk is GPT.

## How it works

```
EE: exfat-format.elf                         IOP
──────────────────────────                   ─────────────────────────────────────────
SifIopReset, rom0:SIO2MAN, rom0:PADMAN
SifExecModuleBuffer x5   ───────────────►    iomanX, poweroff, ps2dev9, bdm, ata_bd
                                             (ata_bd registers "ata" units with BDM)
SifExecModuleBuffer(exfatfmt.irx, "INFO") ►  _start → worker thread → bdm_get_bd()
                                             → name "ata", devNr 0, parNr 0
              ◄── (size_MiB << 2) | 1 ────   module unloads itself
X, then START
SifExecModuleBuffer(exfatfmt.irx, "FORMAT")► worker: f_mkfs("0:", FM_EXFAT, au_size 0,
                                             64 KiB work buffer); diskio.c →
                                             bd->read/write (≤128 sectors), bd->flush
              ◄── (result << 2) | 1 ──────   module unloads itself
```

* `ee/main.c`: screen, controller, driver loading (IRX embedded with `bin2c`),
  the two confirmations.
* `iop/main.c`: the `exfatfmt.irx` module. `_start` runs on MODLOAD's
  thread, so the work runs on its own thread with a 16 KiB stack. `_start`
  waits until that thread is dormant before returning, because MODLOAD frees
  the module as soon as `_start` returns with `MODULE_NO_RESIDENT_END`.
* The result goes back in the module's return value: `(value << 2) | 1`.
  MODLOAD only looks at the low 2 bits (`1` = unload) and passes the whole
  value to the EE, which reads `value = result >> 2`.
* `iop/diskio.c`: FatFs → BDM glue. `GET_BLOCK_SIZE` = 2048 sectors gives
  the 1 MiB data-area alignment.
* `tools/get_fatfs.sh`: downloads FatFs and sets `FF_USE_MKFS`, `FF_FS_EXFAT`,
  `FF_USE_LFN 1`, `FF_LBA64`, `FF_MIN_GPT 0x100000000`, `FF_CODE_PAGE 437`,
  `FF_FS_NORTC`.

## Error codes

| Shown on screen | Meaning |
|---|---|
| `<driver> FAIL (id=…, ret=…)` then `Driver error…` | A driver did not stay resident. `ps2dev9` exits when it finds no usable DEV9 hardware (network adapter / expansion bay). `ata_bd` exits with "HDD is not connected" when the DEV9 has no ATA interface or no drive on it. |
| `Internal HDD (ata0) not found.` | No `ata0` showed up within about 10 s, or the module could not be started. |
| `ERROR: f_mkfs failed: FR_… (n)` | FatFs error 1–19. The most likely ones: `FR_DISK_ERR` (1, read/write error from the drive), `FR_NOT_READY` (3), `FR_MKFS_ABORTED` (14, unsuitable size). |
| `formatter returned 100` | `ata0` disappeared between INFO and FORMAT. |
| `formatter returned 101` | Not enough IOP memory for the 64 KiB work buffer. |
| `formatter returned 102` | Bad module arguments (internal error). |
| `formatter returned 103` | Could not create the IOP thread or semaphore. |
| `formatter returned 104` | Sector size is not 512 bytes. |
| `formatter returned -1` | The module failed to load or did not unload itself. |

Codes 100–104 and FRESULT 1–19 come from `iop/main.c` and FatFs `ff.h`.

## Known limitations

* Disks larger than 2 TiB: only 2 TiB are used (see above).
* INFO uses the same return channel for size and errors. If the module
  cannot create its thread or semaphore during INFO, the EE shows a
  100–104 MiB "disk" instead of an error. The FORMAT that follows fails in the
  same way and reports the error code.
* There is no progress indicator. `f_mkfs` writes only the metadata (in the
  tests, at most about 1,500 write calls of ≤64 KiB each), so it should be quick.

## Testing status

| What | Status |
|---|---|
| Build with the current PS2DEV, no warnings, no unresolved IRX symbols | ✅ verified |
| `imports.lst` matches the functions the IRX calls | ✅ verified (`nm -u` of the objects) |
| Host tests (`make test`): protocol, device selection, 40 GB / 500 GB / 3 TB images, > 4 GiB file round-trip | ✅ pass |
| `fsck.exfat -n` (exfatprogs 1.2.2) on the `make test` images | ✅ clean: 40 GB, 500 GB, 3 TB as ata_bd sees it (MBR), 3 TB raw (GPT) |
| GitHub Actions workflow (PS2 build in `ps2dev/ps2dev:latest`, host tests + fsck) | ✅ passes |
| PCSX2, 40 GiB HDD image | ✅ formatted: MBR, exFAT at LBA 63, cluster heap at LBA 4096, `fsck.exfat -n` clean. X held while drivers load did not confirm |
| Real PS2: SCPH-50008, Seagate ST3320413CS 320 GB (UDMA 4), Sony network adapter with a SATA upgrade | ✅ formatted: MBR, one 0x07 partition at LBA 63, exFAT, 128 KiB clusters |
| OPL Beta 1.2.0 2271 (b3a2199) with that disk | ✅ disk detected, OPL creates its folders; ISOs named `SLUS_xxx.xx.Name.iso` are listed and boot. ⚠️ Plain-named ISOs are not listed ([OPL #1805](https://github.com/ps2homebrew/Open-PS2-Loader/issues/1805)) |

Tested on one console with one disk so far. Formatting erases the disk: keep
a copy of anything you need.

## Credits

* [PS2SDK / PS2DEV](https://github.com/ps2dev/ps2sdk): toolchain, IOP
  drivers (iomanX, poweroff, ps2dev9, bdm, ata_bd) and EE libraries.
  AFL-2.0.
* [FatFs](https://elm-chan.org/fsw/ff/) by ChaN: the exFAT formatter
  (`f_mkfs`). FatFs license (BSD-style, one condition).
* [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader): the
  BDM HDD mode this format is meant for.

## License

The code in this repository is under the [MIT License](LICENSE).
`exfat-format.elf` also contains PS2SDK IRX modules (AFL-2.0) and FatFs
(© ChaN, FatFs license). Their notices apply to those parts. FatFs is not
stored in this repository; `tools/get_fatfs.sh` downloads it.
