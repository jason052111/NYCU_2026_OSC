#include <stdint.h>
#include <stddef.h>

#ifndef OPIORQEMU
  #define OPIORQEMU 0
#endif

#if OPIORQEMU
  #define UART_OFF(x) ((x) << 2)  
#else
  #define UART_OFF(x) (x)         
#endif

#define UART_RBR ((unsigned char*)(UART_BASE + UART_OFF(0x0)))  // Receive Buffer Register (read). Offset 0x0 from UART base
#define UART_THR ((unsigned char*)(UART_BASE + UART_OFF(0x0)))  // Transmit Holding Register (write). Same offset 0x0
#define UART_LSR ((unsigned char*)(UART_BASE + UART_OFF(0x5)))  // Line Status Register. Offset 0x5 (or 0x5*4 if stride4)
#define LSR_DR    (1 << 0)                                      // LSR bit0: DR (Data Ready) = 1 when RX has data to read
#define LSR_TDRQ  (1 << 5)                                      // LSR bit5: THR empty / TX ready (can transmit a new byte).

extern long get_address(const void* dtb_addr, const char *node_path, const char *prop_name);
extern size_t strlen(const char *s);

unsigned long UART_BASE = 0xFFFFFFFF;
/*
 * Read a raw byte from the UART.
 * This function is used for binary data transfer.
 * It returns the exact byte received without any modification.
 */
char uart_getb() {
    while ((*UART_LSR & LSR_DR) == 0) ;                         // Poll until DR=1
    return (char)(*UART_RBR);                                   // Read the received byte from RBR
}
/*
 * Read a character from the UART for terminal interaction.
 * This function is designed for human input. It automatically normalizes 
 * the 'Enter' key to a standard newline character.
 */
char uart_getc() {
    while ((*UART_LSR & LSR_DR) == 0) ;                         // Poll until DR=1
    char c = (char)(*UART_RBR);                                 // Read the received byte from RBR
    return c == '\r' ? '\n' : c;                                // Many terminals send Enter as '\r', normalize it to '\n' for easier line handling
}
/*
 * Transmit a single character over UART.
 */
void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');                             // Convert '\n' to "\r\n" so the cursor returns to column 0 on many serial terminals.

    while ((*UART_LSR & LSR_TDRQ) == 0) ;                       // Poll until TX is ready
    *UART_THR = c;                                              // Write one byte to THR to transmit it
}
/*
 * Transmit a null-terminated C-string over UART.
 */
void uart_puts(const char* s) {
    while (*s)                                                  // Loop until end of C-string ('\0')
        uart_putc(*s++);                                        // Send each character via uart_putc
}
/*
 * Transmit a string and pad it with trailing spaces for left alignment.
 * Useful for printing clean, tabular boot logs.
 */
void uart_puts_left_aligned(const char *data, int width) {     
    int len = (int)strlen(data);
    uart_puts(data);
    // Pad the remaining width with spaces
    for (int i = 0; i < width - len; i++) {
        uart_putc(' ');
    }
}
/*
 * Print a 64-bit unsigned integer in hexadecimal format (e.g., 0x1a2b...).
 */
void uart_hex(unsigned long h) {                                // Print an unsigned long value in hexadecimal over UART
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}
/*
 * Print a signed integer in decimal format.
 */
void uart_int(int num) {                                        // output int
    if (num == 0) {
        uart_putc('0');
        return;
    }

    unsigned int unum;
    if (num < 0) {
        uart_putc('-');
        unum = (unsigned int)(-num); 
    } else {
        unum = (unsigned int)num;
    }

    char buf[32]; 
    int len = 0;

    while (unum > 0) {
        buf[len++] = (unum % 10) + '0';
        unum /= 10;
    }

    while (len > 0) {
        len--;
        uart_putc(buf[len]);
    }
}
/*
 * Initialize UART dynamically by parsing the Device Tree.
 * This prevents hardcoding the memory address, making the bootloader portable.
 */
void uart_init(unsigned long dtb_addr) {
    const void *fdt = (const void *)dtb_addr;
    UART_BASE = get_address(fdt, "/soc/serial", "reg");
}
