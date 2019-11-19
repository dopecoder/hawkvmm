CFLAGS = -Wall -Wextra -Werror -O2

.PHONY: run
run: vmm

vmm: vmm.o guest.img.o
	$(CC) $^ -o $@

guest.o: guest.c
	$(CC) $(CFLAGS) -m32 -ffreestanding -fno-pic -c -o $@ $^

guest.img: guest.o
	$(LD) -T guest.ld -m elf_i386 $^ -o $@

%.img.o: %.img
	$(LD) -b binary -r $^ -o $@

.PHONY: clean
clean:
	$(RM) vmm vmm.o \
		guest.o guest.img guest.img.o \
