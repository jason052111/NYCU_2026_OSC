# Basic Exercise 1 (`start.S`, `link.ld`)

In `_start` (`start.S`), I first read `__bss_start` and `__bss_stop` provided by the linker. Then, I use a loop with `sd zero, (a3)` to clear 8 bytes at a time, zeroing out the `.bss` section to ensure uninitialized global/static variables comply with the C standard. Since the stack grows from high to low addresses, the linker pre-allocates a stack space (16KB) in the memory layout and sets `_end` at the highest address of this reserved area. During initialization, I set the stack pointer (`sp`) to `_end`, ensuring that as the stack grows downwards, it remains within this reserved 16KB space. Finally, the code jumps to `start_kernel()` to enter the C program.

# Basic Exercise 2 (`uart.c`, `Makefile`)

This exercise implements a UART driver using MMIO and polling to enable basic serial I/O for the kernel. 
* **Receiving Data (`uart_getc`)**: It continuously polls the DR (bit 0) of the LSR until data is received, reads the RBR, and converts the commonly sent `\r` from the terminal to `\n`. 
* **Transmitting Data (`uart_putc`)**: It first converts `\n` to `\r\n` to prevent misaligned line breaks. It then polls the TDRQ (bit 5) of the LSR until it is ready to transmit before writing to the THR. 

Functions like `uart_puts()` for string output and `uart_hex()` for hexadecimal debug info are also provided. To support both QEMU and the OrangePi RV2 physical board with the same codebase, I parameterized the register addresses and offset spacing (whether a `<< 2` shift is needed) using `UART_BASE` and `UART_STRIDE4` in `uart.c`. I then used `-D` in the `Makefile` to specify different UART bases/strides for the QEMU and physical board targets. This allows the UART driver to work correctly on both platforms without modifying the C code.

# Basic Exercise 3 (`main.c`)

I upgraded the original character-by-character UART echo into an interactive command-line shell. First, `prompt()` prints the prompt `opi-rv2> `. Then, within `start_kernel()`, `buf[CMD_BUF_LEN]` accumulates each character typed by the user while immediately echoing it back. 

* **Command Execution**: When an Enter (`\n`) is read, a null terminator (`\0`) is appended to the string. The buffer is then passed to `run_command()` for parsing and execution. After execution, the input is cleared, and the prompt is reprinted. 
* **Backspace Support**: Backspace (`0x08` / `0x7f`) is supported by decrementing the buffer length and using the sequence `\b` (backspace), space, and `\b` to visually erase the character from the terminal screen. 
* **Built-in Commands**: Command parsing uses `streq()` for string comparison. Currently, `run_command()` supports:
    * `help`: Lists available commands.
    * `hello`: Prints a greeting.
    * `info`: Calls `cmd_info()` to display system information. 

Users can interact continuously within `start_kernel()`.

# Basic Exercise 4 (`main.c`)

I used an SBI `ecall` to retrieve system information from the underlying firmware via the OpenSBI Base Extension (EID = `0x10`). Specifically, I defined `struct sbiret { error, value }` and `sbi_ecall()` in `main.c`. Using inline assembly, I loaded the parameters into registers `a0`~`a7`, executed `ecall`, and retrieved the returned `a0` and `a1` as `error` and `value` respectively. 

Based on the Base Extension function IDs (`GET_SPEC_VERSION`, `GET_IMP_ID`, `GET_IMP_VERSION`), I implemented `sbi_get_spec_version()`, `sbi_get_impl_id()`, and `sbi_get_impl_version()`. These all utilize `sbi_ecall(SBI_EXT_BASE, fid, 0...)` to obtain and return `ret.value`. Finally, the `info` command in the shell calls `cmd_info()` to print the OpenSBI spec version, implementation ID, and implementation version, allowing users to query system info directly on the board.

# Illustrate

## Compiler Flags

| Flag | Description |
|---|---|
| `-mcmodel=medany` | Programs will generate PC-relative addressing. |
| `-ffreestanding` | Tells the compiler this is not a standard user-space program with an OS. Do not assume a standard environment exists. |
| `-nostdlib` | Do not link the standard C library. |
| `-g` | Include debug symbols. |
| `-Wall` | Enable common warnings. |
| `-c` | Compile and assemble, but do not link. |
| `-T` | Specify a linker script. |
| `-M virt` | Use the standard RISC-V virtual machine board provided by QEMU (`virt`). |
| `-m` | Specify virtual machine memory size. |

## Build Commands

Compile and assemble to object files:
```bash
riscv64-unknown-elf-gcc $(CFLAGS) -c *.S *.c
```

Link all `.o` files into an ELF executable:
```bash
riscv64-unknown-elf-ld -T link.ld -o $(TARGET).elf *.o
```

Convert the ELF file into a raw binary image:
```bash
riscv64-unknown-elf-objcopy -O binary $(TARGET).elf $(TARGET)
```

## Platform Architecture Notes

### QEMU vs. OrangePi RV2
* **QEMU (`virt` machine)**: The UART MMIO address and register layout are simulated (typically base `0x10000000`, offset in bytes). I compile the Makefile target with `-DUART_BASE=0x10000000UL -DUART_STRIDE4=0`.
* **OrangePi RV2**: The UART comes from a physical SoC. The base address differs, and registers are often 32-bit aligned (offsets require `<< 2`). The `build-opi-rv2` Makefile target uses `-DUART_BASE=0xD4017000UL -DUART_STRIDE4=1`. It additionally uses `mkimage` to package `kernel.bin` into a FIT (`.fit`) image for U-Boot to load.
* **Compatibility**: `UART_BASE` and `UART_STRIDE4` can be overridden by compilation flags. The macro `UART_OFF(x)` automatically shifts the offset (`x << 2`) when `UART_STRIDE4=1`; otherwise, it retains the byte offset. This maps the same UART driver logic (reading/writing `UART_RBR`/`THR`/`LSR`) to the correct registers on both platforms.

### File Formats & Linker Concepts
* **`.elf`**: The linked output containing complete addresses, symbols, and debug info. Used for inspection and debugging.
* **`.bin`**: A raw binary extracted from the `.elf`, designed strictly for the board to load and execute. 
* **Linker Script (`link.ld`)**: The linker calculates and fixes absolute symbol addresses (e.g., `__bss_start`, `__bss_stop`). These become the "absolute addresses" used in the program. Therefore, the start address defined in `link.ld` **must match** the actual load address specified in the `.its` file for the kernel load, otherwise the program will use incorrect memory addresses. 
* **`.its` (Image Tree Source)**: The `entry` specifies where U-Boot should jump after loading. This is typically the address where `_start` is located (the first instruction when `kernel.bin` is placed in RAM). This assumes `kernel.bin` begins exactly at `_start` without extra headers.
* **`kernel.bin`**: Can be thought of as the linker arranging the sections (`.text`/`.rodata`/`.data`...) of each `.o` file according to `link.ld`, and extracting "the content that needs to be loaded into memory" into a raw binary file. Unlike an ELF, it doesn't carry section tables or entry information, so it relies on the `.its` (or U-Boot commands) to tell the bootloader where to load it and where to start executing.

### U-Boot `.fit` Boot Sequence
1. Parse the configuration (e.g., `config-1`) to confirm which kernel and FDT to use.
2. Load the kernel to `kernel.load`.
3. Load the FDT to `fdt.load`.
4. Jump to `kernel.entry` to start execution (and pass the FDT memory address as an argument to the kernel).

# Instruction

* **QEMU**: 
    1. `make run`
* **OrangePi RV2**: 
    1. `make build-opi-rv2`
    2. Copy `.fit` to SD card
    3. `sudo screen /dev/ttyUSB0 115200`
