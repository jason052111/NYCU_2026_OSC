#ifndef UART_BASE
    #define UART_BASE 0x10000000UL 
#endif

#ifndef UART_STRIDE4
  #define UART_STRIDE4 0
#endif

#if UART_STRIDE4
  #define UART_OFF(x) ((x) << 2)  
#else
  #define UART_OFF(x) (x)         
#endif

#define UART_RBR ((unsigned char*)(UART_BASE + UART_OFF(0x0)))  // Receive Buffer Register (read). Offset 0x0 from UART base
#define UART_THR ((unsigned char*)(UART_BASE + UART_OFF(0x0)))  // Transmit Holding Register (write). Same offset 0x0
#define UART_LSR ((unsigned char*)(UART_BASE + UART_OFF(0x5)))  // Line Status Register. Offset 0x5 (or 0x5*4 if stride4)
#define LSR_DR    (1 << 0)                                      // LSR bit0: DR (Data Ready) = 1 when RX has data to read
#define LSR_TDRQ  (1 << 5)                                      // LSR bit5: THR empty / TX ready (can transmit a new byte).

char uart_getc() {
    while ((*UART_LSR & LSR_DR) == 0) ;                         // Poll until DR=1
    char c = (char)(*UART_RBR);                                 // Read the received byte from RBR
    return c == '\r' ? '\n' : c;                                // Many terminals send Enter as '\r', normalize it to '\n' for easier line handling
}

void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');                             // Convert '\n' to "\r\n" so the cursor returns to column 0 on many serial terminals.

    while ((*UART_LSR & LSR_TDRQ) == 0) ;                       // Poll until TX is ready
    *UART_THR = c;                                              // Write one byte to THR to transmit it
}

void uart_puts(const char* s) {
    while (*s)                                                  // Loop until end of C-string ('\0')
        uart_putc(*s++);                                        // Send each character via uart_putc
}

void uart_hex(unsigned long h) {                                // Print an unsigned long value in hexadecimal over UART
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}