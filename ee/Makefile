# exfat-format.elf - EE program. All IRX drivers are embedded into the ELF.

EE_BIN  = exfat-format.elf

# Drivers from the SDK ($PS2SDK/iop/irx) + our own module
IRX_SDK = iomanX poweroff ps2dev9 bdm ata_bd
IRX_C   = $(addsuffix _irx.c,$(IRX_SDK)) exfatfmt_irx.c

EE_OBJS = main.o $(IRX_C:.c=.o)
EE_LIBS = -ldebug -lpad -lpatches
EE_CFLAGS += -Wall

BIN2C = $(PS2SDK)/bin/bin2c

all: $(EE_BIN)

%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(BIN2C) $< $@ $*_irx

exfatfmt_irx.c: ../iop/exfatfmt.irx
	$(BIN2C) $< $@ exfatfmt_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(IRX_C)

.SECONDARY: $(IRX_C)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
