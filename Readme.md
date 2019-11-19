# Hawk VMM

Hawk VMM is simple virtual machine monitor which can run a guest in protected mode, it has three virtual devices it can provide the guest with,

1) Keyboard
2) Console
3) Timer

## Building and Running

A makefile is provided to build and run the VMM and guest program, by running the following commands, you should be able to run the guest in the VMM,

```shell
make
sudo ./vmm -b guest.img
```

if you wish to run your own binary, you have to compile the guest C program in the following way : (assuming you guest program is called guest.c)

```
gcc -m32 -ffreestanding -fno-pic -c -o guest.o guest.c
ld -T guest.ld -m elf_i386 guest.o -o guest.img.o
ld -b binary -r guest.img.o -o guest.img
```

The above commands will generate a guest.img file to load it into the VMM.

Thank you!