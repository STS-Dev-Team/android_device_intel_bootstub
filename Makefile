OBJ=bootstub.o spi-uart.o head.o sfi.o
CMDLINE_SIZE ?= 0x400
CFLAGS=-m32 -ffreestanding -Wall -DCMDLINE_SIZE=${CMDLINE_SIZE}
CC ?= gcc

all: bootstub

bootstub:bootstub.bin
	cat bootstub.bin /dev/zero | dd bs=4096 count=1 > $@

bootstub.bin:bootstub.elf
	objcopy -O binary -R .note -R .comment -S $< $@

bootstub.elf:bootstub.lds $(OBJ)
	ld -m elf_i386 -T bootstub.lds $(OBJ) -o $@

bootstub.o:bootstub.c bootstub.h
	${CC} $(CFLAGS) -c bootstub.c

spi-uart.o:spi-uart.c spi-uart.h
	${CC} $(CFLAGS) -c spi-uart.c

sfi.o:sfi.c
	${CC} $(CFLAGS) -c sfi.c

head.o:head.S bootstub.h
	${CC} $(CFLAGS) -c head.S

clean:
	rm -rf *.o *.bin *.elf *.bz2 *.rpm

source:bootstub.c head.S VERSION
	git archive --prefix=bootstub-`head -n 1 VERSION | awk '{print $$1}'`/ --format=tar HEAD | bzip2 -c > bootstub-`head -n 1 VERSION | awk '{print $$1}'`.tar.bz2

rpm:source
	cp bootstub.spec /usr/src/redhat/SPECS/
	cp *.tar.bz2 /usr/src/redhat/SOURCES/
	rpmbuild -ba /usr/src/redhat/SPECS/bootstub.spec
	cp /usr/src/redhat/RPMS/i386/bootstub*.rpm ./
.PHONY: all clean source rpm
