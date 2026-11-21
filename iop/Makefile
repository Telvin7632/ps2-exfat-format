# exfatfmt.irx - IOP module that runs FatFs f_mkfs() on the internal HDD.
# FatFs sources (ff.c, ff.h, ffconf.h, diskio.h, ffunicode.c) are put here by
# ../tools/get_fatfs.sh (run "make fatfs" in the project root).

IOP_BIN  = exfatfmt.irx
IOP_OBJS = main.o diskio.o ff.o ffunicode.o imports.o

IOP_CFLAGS += -Wall
# 64-bit division helpers (__udivdi3 etc.) used by FatFs with FF_LBA64 = 1
IOP_LIBS   += -lgcc

all: $(IOP_BIN)

clean:
	rm -rf obj $(IOP_BIN) *.notiopmod*.elf

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
