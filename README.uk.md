# ps2-exfat-format

[English version](README.md)

Homebrew-програма для PlayStation 2, яка прямо на консолі форматує
**внутрішній HDD** (ATA-диск на мережевому адаптері) в **exFAT**. Результат:
MBR з одним розділом exFAT. Такий диск Open PS2 Loader бачить у режимі BDM HDD
(ATA), а ПК може його читати й записувати.

> [!WARNING]
> **Усі дані на HDD буде стерто.** Зокрема всі APA-розділи, встановлені ігри,
> HDD-OSD (Browser 2.0) і FMCB/PS2BBL, встановлені на HDD.
> Запускайте програму з карти пам'яті або USB-флешки, а не з HDD.
> **POPStarter з exFAT-HDD не працює** (йому потрібен APA), тож PS1-ігри,
> встановлені на HDD для POPStarter, перестануть запускатися.

## Що виходить

* Таблиця розділів MBR з одним розділом типу `0x07`, що починається з LBA 63.
* Усередині exFAT. Розмір кластера обирає FatFs. У тестах на ПК він був
  128 КіБ для всіх перевірених розмірів (від 40 ГБ до 2 ТіБ). Область кластерів
  (даних) починається на межі 1 МіБ від початку диска.
* **Диски понад 2 ТіБ: використовуються лише перші 2 ТіБ.** Драйвер `ata_bd`
  з PS2SDK зберігає розмір диска в 32-бітному полі й повідомляє щонайбільше
  2^32 − 1 секторів. FatFs перейшов би на GPT лише від 2^32 секторів, тому на
  консолі завжди виходить MBR (див. `ps2atad.c` у PS2SDK:
  `total_sectors_lba48 = 0xffffffff`).

## Вимоги

**На консолі**

* «Товста» PS2 з мережевим адаптером, що має роз'єм для HDD, і підключений до
  нього ATA-диск (IDE або SATA через перехідник). Програма обирає тільки
  master-пристрій (`ata0`).
* Спосіб запустити ELF без HDD, наприклад wLaunchELF з карти пам'яті чи
  USB-флешки (FMCB/PS2BBL на карті пам'яті).
* Геймпад у порту 1.
* Не підтримуються: PSX (DESR), бо там на HDD зберігаються ще й дані
  DVR; диски з логічним сектором 4 КіБ (ata_bd завжди повідомляє 512 байт).

**Для збірки**

* Linux або WSL (Ubuntu) з `make`, `curl`, `unzip` і `git`.
* Тулчейн PS2DEV (EE `mips64r5900el-ps2-elf-gcc`, IOP `mipsel-none-elf-gcc`, PS2SDK).
* Для тестів на ПК: `gcc`, а для перевірки ФС ще `exfatprogs`.

## Тулчейн у WSL

У PowerShell (від адміністратора), якщо WSL ще немає:

```powershell
wsl --install -d Ubuntu
```

Далі в терміналі Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential git curl unzip exfatprogs

# готовий тулчейн PS2DEV (EE + IOP + PS2SDK)
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

1. Встановіть VS Code на Windows і розширення **WSL** (`ms-vscode-remote.remote-wsl`).
2. Клонуйте проєкт **всередину WSL** (не в `/mnt/c/...`, там збірка значно
   повільніша) і відкрийте його:
   ```bash
   git clone https://github.com/Telvin7632/ps2-exfat-format.git
   cd ps2-exfat-format
   code .
   ```
3. Погодьтеся встановити рекомендовані розширення (C/C++, Makefile Tools).
4. IntelliSense: у рядку стану оберіть конфігурацію **PS2 EE (ee/)** або
   **PS2 IOP (iop/)** залежно від файлу, який редагуєте (`.vscode/c_cpp_properties.json`).
5. `Ctrl+Shift+B` запускає задачу **Build**. Задачі самі задають `PS2DEV`,
   `PS2SDK` і `PATH` (`.vscode/tasks.json`).

## Збірка

```bash
make            # перший раз завантажить FatFs у iop/ (tools/get_fatfs.sh)
make clean      # прибрати результати збірки
make distclean  # і видалити завантажений FatFs
```

Результат: `ee/exfat-format.elf` (≈1,4 МБ, усі IOP-драйвери всередині).
Збірка проходить без попереджень компілятора, а в `exfatfmt.irx` немає
нерозв'язаних символів (`mipsel-none-elf-nm iop/exfatfmt.notiopmod.elf | grep " U "`
нічого не виводить).

FatFs береться з elm-chan.org (`ff16.zip`, запасні варіанти `ff15a` і `ff15`),
а якщо сайт недоступний, то з дзеркала на GitHub `abbrev/fatfs`. Зараз у
дзеркалі лежить «R0.16 w/patch 2», яка трохи відрізняється від звичайного
архіву R0.16.

## Тести на ПК

```bash
make test                       # зібрати й запустити тести
sudo tests/host/fsck_images.sh  # fsck.exfat -n для кожного створеного тому
make test-clean                 # прибрати збірку тестів і образи
```

`make test` компілює для ПК **справжні** `iop/main.c` і `iop/diskio.c` з тим
самим конфігом FatFs. Заглушками замінено лише сервіси IOP (потоки, семафори,
пам'ять, `bdm_get_bd`), а `_start` перейменовано, щоб тест міг його викликати.
Тест перевіряє:

* що `struct block_device` у заглушці збігається з `$PS2SDK/iop/include/bdm.h`;
* протокол модуля: неправильні аргументи, відсутність `ata0`, пристрій із
  сектором не 512 байт, а також що `_start` чекає, поки робочий потік можна
  буде видалити;
* що використовується тільки `ata0` (unit 0, увесь диск). Пристрої-приманки
  `mass0`, `ata1` і `ata0p1` валять тест, якщо їх читали чи в них писали;
* розріджені образи в `/tmp/ps2-exfat-test/`: 40 ГБ і 500 ГБ (MBR), диск
  3 ТБ таким, яким його бачить `ata_bd` (2^32 − 1 секторів, MBR), і повний
  3 ТБ (GPT; на консолі сьогодні недосяжно). Для кожного: розмір з INFO,
  результат FORMAT, таблиця розділів, завантажувальний сектор exFAT,
  вирівнювання на 1 МіБ, потім `f_mount`, запис і зчитування файлу понад 4 ГіБ.

`fsck_images.sh` підключає лише звичайні файли з `/tmp/ps2-exfat-test/`,
тільки на читання (`losetup -r -o <зсув> --sizelimit <розмір>`), і запускає
на них `fsck.exfat -n`. Справжніх дисків скрипт не чіпає.

## Тест у PCSX2

1. PCSX2 → Settings → **Network & HDD**: увімкніть мережевий адаптер і HDD,
   створіть образ HDD (наприклад, 40 ГБ). PCSX2 зберігає його як сирий
   образ диска (типова назва `DEV9hdd.raw`).
2. Запустіть `ee/exfat-format.elf` (System → Start File або задача VS Code
   **Run in PCSX2**; шлях до `pcsx2-qt.exe` змініть у `.vscode/tasks.json`).
3. Натисніть **X**, потім **START**. Дочекайтеся `DONE` і закрийте PCSX2.
4. Перевірте образ у WSL:
   ```bash
   IMG="/mnt/c/Users/<ви>/Documents/PCSX2/DEV9hdd.raw"   # шлях до вашого образу
   LOOP=$(sudo losetup -f --show -r -P "$IMG")
   sudo fsck.exfat -n "${LOOP}p1"
   sudo losetup -d "$LOOP"
   ```
   Якщо `losetup` не може підключити файл з `/mnt/c`, спершу скопіюйте його
   у WSL (`cp --sparse=always`).

## Запуск на консолі

1. Скопіюйте `exfat-format.elf` на флешку з FAT32.
2. Запустіть його з wLaunchELF (завантаженого з карти пам'яті, а не з HDD).
3. Програма завантажить драйвери й покаже розмір внутрішнього HDD.
4. **X** — продовжити, потім **START** — форматувати. Будь-яка інша кнопка
   скасовує, і нічого не записується. Рахуються тільки натискання після
   появи запиту; кнопку, яку вже тримають, треба спершу відпустити.
5. Після `DONE` вимкніть і знову ввімкніть консоль.

### Використання з OPL

Диск виходить звичайним: MBR + exFAT. У збірці OPL з підтримкою BDM увімкніть
BDM-пристрій HDD (ATA) і кладіть ігри в папки `DVD/` та `CD/` на диску, як і
для інших BDM-пристроїв. З дисками, відформатованими цією програмою, це ще
не перевірялося.

## Як це працює

```
EE: exfat-format.elf                         IOP
──────────────────────────                   ─────────────────────────────────────────
SifIopReset, rom0:SIO2MAN, rom0:PADMAN
SifExecModuleBuffer x5   ───────────────►    iomanX, poweroff, ps2dev9, bdm, ata_bd
                                             (ata_bd реєструє пристрої "ata" у BDM)
SifExecModuleBuffer(exfatfmt.irx, "INFO") ►  _start → робочий потік → bdm_get_bd()
                                             → name "ata", devNr 0, parNr 0
              ◄── (розмір_МіБ << 2) | 1 ──   модуль сам вивантажується
X, потім START
SifExecModuleBuffer(exfatfmt.irx, "FORMAT")► потік: f_mkfs("0:", FM_EXFAT, au_size 0,
                                             робочий буфер 64 КіБ); diskio.c →
                                             bd->read/write (≤128 секторів), bd->flush
              ◄── (результат << 2) | 1 ───   модуль сам вивантажується
```

* `ee/main.c`: екран, геймпад, завантаження драйверів (IRX вбудовані через
  `bin2c`), два підтвердження.
* `iop/main.c`: модуль `exfatfmt.irx`. `_start` виконується в потоці MODLOAD,
  тому робота йде в окремому потоці зі стеком 16 КіБ. Перед поверненням
  `_start` чекає, поки цей потік стане dormant: MODLOAD звільняє пам'ять
  модуля одразу, щойно `_start` повертає `MODULE_NO_RESIDENT_END`.
* Результат передається через код повернення модуля: `(значення << 2) | 1`.
  MODLOAD дивиться лише на 2 молодші біти (`1` = вивантажити) і віддає EE
  повне значення, а EE читає `значення = result >> 2`.
* `iop/diskio.c`: зв'язка FatFs → BDM. `GET_BLOCK_SIZE` = 2048 секторів дає
  вирівнювання області даних на 1 МіБ.
* `tools/get_fatfs.sh`: завантажує FatFs і вмикає `FF_USE_MKFS`, `FF_FS_EXFAT`,
  `FF_USE_LFN 1`, `FF_LBA64`, `FF_MIN_GPT 0x100000000`, `FF_CODE_PAGE 437`,
  `FF_FS_NORTC`.

## Коди помилок

| Повідомлення на екрані | Що означає |
|---|---|
| `<драйвер> FAIL (id=…, ret=…)`, потім `Driver error…` | Драйвер не лишився резидентним. `ps2dev9` виходить, якщо не знайшов придатного DEV9 (мережевий адаптер / expansion bay). `ata_bd` виходить із повідомленням «HDD is not connected», якщо в DEV9 немає ATA-інтерфейсу або на ньому немає диска. |
| `Internal HDD (ata0) not found.` | `ata0` не з'явився приблизно за 10 с, або модуль не вдалося запустити. |
| `ERROR: f_mkfs failed: FR_… (n)` | Помилка FatFs 1–19. Найімовірніші: `FR_DISK_ERR` (1, помилка читання/запису на диску), `FR_NOT_READY` (3), `FR_MKFS_ABORTED` (14, непридатний розмір). |
| `formatter returned 100` | `ata0` зник між INFO і FORMAT. |
| `formatter returned 101` | Забракло пам'яті IOP на робочий буфер 64 КіБ. |
| `formatter returned 102` | Неправильні аргументи модуля (внутрішня помилка). |
| `formatter returned 103` | Не вдалося створити потік або семафор IOP. |
| `formatter returned 104` | Розмір сектора не 512 байт. |
| `formatter returned -1` | Модуль не завантажився або не вивантажився сам. |

Коди 100–104 і FRESULT 1–19 взято з `iop/main.c` і `ff.h` FatFs.

## Відомі обмеження

* Диски понад 2 ТіБ: використовуються лише 2 ТіБ (див. вище).
* INFO повертає розмір і помилки одним каналом. Якщо під час INFO модуль не
  зможе створити потік чи семафор, EE покаже «диск» на 100–104 МіБ замість
  помилки. Наступний FORMAT зламається так само й покаже код помилки.
* Індикатора прогресу немає. `f_mkfs` пише лише метадані (у тестах
  щонайбільше ≈1500 викликів запису по ≤64 КіБ), тож це має бути швидко.

## Статус тестування

| Що | Статус |
|---|---|
| Збірка з актуальним PS2DEV без попереджень і без нерозв'язаних символів в IRX | ✅ перевірено |
| `imports.lst` відповідає функціям, які викликає IRX | ✅ перевірено (`nm -u` по об'єктниках) |
| Тести на ПК (`make test`): протокол, вибір пристрою, образи 40 ГБ / 500 ГБ / 3 ТБ, запис і читання файлу > 4 ГіБ | ✅ проходять |
| `fsck.exfat -n` (exfatprogs 1.2.2) на образах `make test` | ✅ clean: 40 ГБ, 500 ГБ, 3 ТБ так, як його бачить ata_bd (MBR), 3 ТБ повністю (GPT) |
| Workflow GitHub Actions | написано, ще не запускався |
| PCSX2 | ❌ ще не запускалося |
| Справжня PS2 | ❌ ще не запускалося |
| OPL з диском від цієї програми | ❌ ще не перевірялося |

Доки програму не перевірено в PCSX2 і на залізі, використовуйте її тільки з
диском, дані на якому не шкода втратити.

## Подяки

* [PS2SDK / PS2DEV](https://github.com/ps2dev/ps2sdk): тулчейн, IOP-драйвери
  (iomanX, poweroff, ps2dev9, bdm, ata_bd) і бібліотеки EE. AFL-2.0.
* [FatFs](https://elm-chan.org/fsw/ff/) від ChaN: форматування в exFAT
  (`f_mkfs`). Ліцензія FatFs (BSD-подібна, одна умова).
* [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader): режим
  BDM HDD, для якого й призначено цей формат.

## Ліцензія

Код у цьому репозиторії поширюється за [ліцензією MIT](LICENSE).
`exfat-format.elf` також містить IRX-модулі PS2SDK (AFL-2.0) і FatFs
(© ChaN, ліцензія FatFs), на ці частини поширюються їхні умови. FatFs у
репозиторії не зберігається: його завантажує `tools/get_fatfs.sh`.
