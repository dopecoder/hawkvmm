CFLAGS = -Wall -Wextra -Werror -O2

.PHONY: run
run: vmm

vmm: vmm.o
	$(CC) $^ -o $@

.PHONY: clean
clean:
	$(RM) vmm vmm.o \
