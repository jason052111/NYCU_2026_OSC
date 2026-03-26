RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall
QEMU ?= qemu-system-riscv64
TARGET = kernel

build: clean
	$(CC) $(CFLAGS) -c *.S *.c -DUART_BASE=0x10000000UL -DUART_STRIDE4=0
	$(LD) -T link.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET) 

build-opi-rv2: clean
	$(CC) $(CFLAGS) -c *.S *.c -DUART_BASE=0xD4017000UL -DUART_STRIDE4=1
	$(LD) -T link.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	mkimage -f $(TARGET).its $(TARGET).fit

run: build $(TARGET)
	$(QEMU) -M virt -m 8G -kernel $(TARGET) -display none -serial stdio

clean:
	rm -f $(TARGET) $(TARGET).bin $(TARGET).elf *.o *.fit