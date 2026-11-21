# Top-level build: FatFs -> IOP module -> EE program
.PHONY: all iop ee fatfs clean distclean test test-clean

all: ee

fatfs: iop/ff.c

iop/ff.c:
	bash tools/get_fatfs.sh

iop: iop/ff.c
	$(MAKE) -C iop

ee: iop
	$(MAKE) -C ee

# host (PC) tests: real iop/main.c + diskio.c + FatFs on sparse images in /tmp
test: iop/ff.c
	$(MAKE) -C tests/host

# removes the host test build and the sparse images in /tmp/ps2-exfat-test
test-clean:
	$(MAKE) -C tests/host clean
	rm -rf /tmp/ps2-exfat-test

clean:
	$(MAKE) -C iop clean
	$(MAKE) -C ee clean
	$(MAKE) -C tests/host clean

# also removes the downloaded FatFs sources
distclean: clean
	rm -f iop/ff.c iop/ff.h iop/ffconf.h iop/diskio.h iop/ffunicode.c
