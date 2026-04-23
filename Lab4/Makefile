RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall
QEMU ?= qemu-system-riscv64

BOOT_TARGET = bootloader
KERNEL_TARGET = kernel

build-qemu-boot: clean-boot
	cd bootloader && $(CC) $(CFLAGS) -c *.c *.S  -D OPIORQEMU=0
	cd bootloader && $(LD) -T link_boot.ld -o $(BOOT_TARGET).elf *.o
	cd bootloader && $(OBJCOPY) -O binary $(BOOT_TARGET).elf $(BOOT_TARGET) 

build-qemu-kernel: clean-kernel
	cd kernel && $(CC) $(CFLAGS) -c *.c *.S -D OPIORQEMU=0
	cd kernel && $(LD) -T link_kernel.ld -o $(KERNEL_TARGET).elf *.o
	cd kernel && $(OBJCOPY) -O binary $(KERNEL_TARGET).elf $(KERNEL_TARGET).bin

build-opi-boot: clean-boot
	cd bootloader && $(CC) $(CFLAGS) -c *.c *.S -D OPIORQEMU=1
	cd bootloader && $(LD) -T link_boot.ld -o $(BOOT_TARGET).elf *.o
	cd bootloader && $(OBJCOPY) -O binary $(BOOT_TARGET).elf $(BOOT_TARGET).bin
	cd bootloader && mkimage -f $(KERNEL_TARGET).its $(KERNEL_TARGET).fit

build-opi-kernel: clean-kernel
	cd kernel && $(CC) $(CFLAGS) -c *.c *.S -D OPIORQEMU=1
	cd kernel && $(LD) -T link_kernel.ld -o $(KERNEL_TARGET).elf *.o
	cd kernel && $(OBJCOPY) -O binary $(KERNEL_TARGET).elf $(KERNEL_TARGET).bin

run-qemu: build-qemu-boot clean-kernel
	cd bootloader && $(QEMU) -M virt -m 8G -kernel $(BOOT_TARGET) -initrd initramfs.cpio -display none -serial pty

run-qemu-debug: build-qemu-boot build-qemu-kernel
	cd bootloader && $(QEMU) -M virt -m 8G -kernel $(BOOT_TARGET) -display none -serial pty -S -s

clean-boot:
	cd bootloader && rm -f $(BOOT_TARGET) *.bin *.elf *.o *.fit

clean-kernel:
	cd kernel && rm -f $(KERNEL_TARGET).bin *.elf *.o *.fit

clean: clean-boot clean-kernel
