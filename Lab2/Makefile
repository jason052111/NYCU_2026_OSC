RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall
QEMU ?= qemu-system-riscv64

BOOT_TARGET = bootloader
KERNEL_TARGET = kernel

common-qemu:
	$(CC) $(CFLAGS) -c start.S uart.c tool.c fdt.c -DUART_STRIDE4=0

common-opi:
	$(CC) $(CFLAGS) -c start.S uart.c tool.c fdt.c -DUART_STRIDE4=1

build-qemu-boot: clean common-qemu
	$(CC) $(CFLAGS) -c main_boot.c cmd_boot.c
	$(LD) -T link_boot.ld -o $(BOOT_TARGET).elf *.o
	$(OBJCOPY) -O binary $(BOOT_TARGET).elf $(BOOT_TARGET) 

build-qemu-kernel: clean common-qemu
	$(CC) $(CFLAGS) -c main_kernel.c cmd_kernel.c initrd.c sbi.c
	$(LD) -T link_kernel.ld -o $(KERNEL_TARGET).elf *.o
	$(OBJCOPY) -O binary $(KERNEL_TARGET).elf $(KERNEL_TARGET).bin

build-opi-kernel: clean common-opi
	$(CC) $(CFLAGS) -c main_kernel.c cmd_kernel.c initrd.c sbi.c
	$(LD) -T link_kernel.ld -o $(KERNEL_TARGET).elf *.o
	$(OBJCOPY) -O binary $(KERNEL_TARGET).elf $(KERNEL_TARGET).bin

build-opi-boot: clean common-opi
	$(CC) $(CFLAGS) -c main_boot.c cmd_boot.c
	$(LD) -T link_boot.ld -o $(BOOT_TARGET).elf *.o
	$(OBJCOPY) -O binary $(BOOT_TARGET).elf $(BOOT_TARGET).bin
	mkimage -f $(KERNEL_TARGET).its $(KERNEL_TARGET).fit

run-qemu: clean build-qemu-boott
	$(QEMU) -M virt -m 8G -kernel $(BOOT_TARGET) -initrd initramfs.cpio -display none -serial pty

run-qemu-debug: clean build-qemu-boot build-qemu-kernel
	$(QEMU) -M virt -m 8G -kernel $(BOOT_TARGET) -display none -serial pty -S -s

clean:
	rm -f $(BOOT_TARGET) $(KERNEL_TARGET) *.bin *.elf *.o *.fit