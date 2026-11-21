# Host (PC) tests: the real iop/main.c + iop/diskio.c + FatFs, built with the
# PC compiler against the stubs in this directory. Run from the project root
# with "make test" (it downloads FatFs first if needed).
#
# The IOP sources are COPIED into build/src/ before compiling: a #include "..."
# is looked up in the including file's own directory first, so compiling them
# in iop/ would pick iop/irx_imports.h (PS2SDK) instead of the stub here.

IOP     := ../../iop
BUILD   := build
SRC     := $(BUILD)/src
IOP_SRC := main.c diskio.c ff.c ff.h ffconf.h diskio.h ffunicode.c

HOSTCC   ?= gcc
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS   := -O1 -g -Wall -Werror $(SANITIZE)
CPPFLAGS := -I. -I$(SRC)

OBJS := $(BUILD)/test_format.o $(BUILD)/iop_stubs.o \
        $(BUILD)/main.o $(BUILD)/diskio.o $(BUILD)/ff.o $(BUILD)/ffunicode.o

.PHONY: all run clean
all: run

run: $(BUILD)/test_format
	./check_bd_struct.sh
	$(BUILD)/test_format

$(BUILD)/test_format: $(OBJS)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(SRC)/%: $(IOP)/%
	@mkdir -p $(SRC)
	cp $< $@

$(BUILD)/%.o: %.c $(wildcard *.h) $(addprefix $(SRC)/,$(IOP_SRC))
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# iop/main.c defines _start (the IRX entry point); rename it so it does not
# clash with the C runtime's _start on the PC.
$(BUILD)/main.o: $(SRC)/main.c $(wildcard *.h) $(addprefix $(SRC)/,$(IOP_SRC))
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) -D_start=exfatfmt_start -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c $(wildcard *.h) $(addprefix $(SRC)/,$(IOP_SRC))
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
