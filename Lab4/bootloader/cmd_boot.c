#ifndef OPIORQEMU
  #define OPIORQEMU 0
#endif

#if OPIORQEMU
  #define ADDRESS 0x00200000
#else
  #define ADDRESS 0x80200000         
#endif

extern char uart_getb(void);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern int strcmp(const char *s1, const char *s2);

void prompt(void) {
    uart_puts("opi-rv2> ");
}

void cmd_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  load  - load kernel.\n");
    uart_puts("  help  - show all commands.\n");
}

void cmd_load(unsigned long boot_hartid, unsigned long boot_dtb) {
    uart_puts("Waiting for kernel...\n");
/* 
 * Wait and synchronize with the sender by finding the magic number "BOOT" (0x544F4F42).
 * This uses a 4-byte sliding window: it shifts out the oldest byte (>> 8) 
 * and shifts in the newest byte from UART at the highest 8 bits (<< 24).
 */
    unsigned int magic = 0;
    while (magic != 0x544F4F42) {
        magic = (magic >> 8) | (uart_getb() << 24);
    }

    int kernel_size = 0;
/* 
 * Read the next 4 bytes from UART to get the total size of the kernel.
 * The bytes are assembled into a 32-bit integer in Little-Endian format 
 * (the first byte received is the lowest 8 bits).
 */
    for (int i = 0; i < 4; i++) {
        kernel_size |= ((unsigned char)uart_getb()) << (i * 8);
    }

    uart_puts("Receiving kernel size: ");
    uart_hex(kernel_size);
    uart_puts(" bytes\n");
/* 
 * Set the target memory address for the kernel to 0x00200000.
 * Receive the kernel binary byte-by-byte via UART and store it into memory.
 */
    char *kernel_addr = (char *)ADDRESS;
    for (int i = 0; i < kernel_size; i++) {
        kernel_addr[i] = uart_getb(); 
    }


/*
 * The "fence.i" is a hardware-level RISC-V instruction used to 
 * synchronize the Instruction Cache (I-Cache) with the Data Cache (D-Cache).
 * * Since the CPU loads the kernel via data writes (D-Cache), 
 * the "fence.i" ensures that the Instruction Fetch unit sees the 
 * new code instead of stale data before jumping to the kernel.
 * * The ".option" directives are used to temporarily enable the 
 * "zifencei" extension so the compiler recognizes the "fence.i" command.
 */
    asm volatile (
        ".option push\n\t"
        ".option arch, +zifencei\n\t"
        "fence.i\n\t"
        ".option pop"
        ::: "memory"
    );
// Define a function pointer to the kernel's entry address (0x00200000).
    void (*jump_to_kernel)(unsigned long, unsigned long) = (void *)ADDRESS;
// Transfer control to the kernel, passing the Hart ID (a0) and DTB address (a1).
    jump_to_kernel(boot_hartid, boot_dtb);
}

void run_command(const char* cmd, unsigned long boot_hartid, unsigned long boot_dtb) {
    if (cmd[0] == '\0') return;

    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "load") == 0) cmd_load(boot_hartid, boot_dtb);
    else {
        uart_puts("Unknown command: ");
        uart_puts(cmd);
        uart_puts("\n");
        uart_puts("Use help to gat commands.\n");
    }
}