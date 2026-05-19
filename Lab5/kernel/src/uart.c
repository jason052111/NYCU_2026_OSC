#include "uart.h"
#include "fdt.h"
#include "tool.h"
#include "thread.h"
#include "trap.h"

static char uart_buf[UART_BUF_SIZE];
static volatile unsigned int uart_buf_r = 0;
static volatile unsigned int uart_buf_w = 0;

unsigned long UART_IRQ = 0xFFFFFFFF;
unsigned long UART_BASE = 0xFFFFFFFF;
unsigned long PLIC_BASE = 0xFFFFFFFF;

/*
 * Return the UART receiver buffer register address.
 * The interrupt handler uses this address to read incoming characters.
 */
volatile unsigned char* uart_rbr_addr(void) {
    return UART_RBR;
}
/*
 * Check whether the UART ring buffer is empty.
 * Empty means read index and write index are the same.
 */
int uart_buf_is_empty(void) {
    return uart_buf_r == uart_buf_w;
}
/*
 * Check whether the UART ring buffer is full.
 * One slot is kept unused to distinguish full from empty.
 */
int uart_buf_is_full(void) {
    return ((uart_buf_w + 1) % UART_BUF_SIZE) == uart_buf_r;
}
/*
 * Push one character into the UART ring buffer.
 * If the buffer is full, the character is dropped.
 */
void uart_buf_push(char c) {
    if (uart_buf_is_full()) {
        return;
    }
    uart_buf[uart_buf_w] = c;
    uart_buf_w = (uart_buf_w + 1) % UART_BUF_SIZE;
}
/*
 * Pop one character from the UART ring buffer.
 * If the buffer is empty, yield the CPU until input arrives.
 */
char uart_buf_pop(void) {
    while (uart_buf_is_empty()) {
       schedule();
    }
    char c = uart_buf[uart_buf_r];
    uart_buf_r = (uart_buf_r + 1) % UART_BUF_SIZE;
    return c;
}
/*
 * Read a character from the UART for terminal interaction.
 * This function is designed for human input. It automatically normalizes 
 * the 'Enter' key to a standard newline character.
 */
char uart_getc() {
    char c = uart_buf_pop();                                    
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

    UART_IRQ  = get_dtb_prop_u32(fdt, "/soc/serial", "interrupts");
    UART_BASE = get_address(fdt, "/soc/serial", "reg");
    PLIC_BASE = get_address(fdt, "/soc/interrupt-controller", "reg");

    *UART_IER |= 1;
    *UART_MCR |= (1 << 3);
}
/*
 * Initialize the PLIC for UART interrupts.
 * This function sets the UART interrupt priority, enables the UART IRQ
 * for the current hart, sets the interrupt threshold, and enables
 * external interrupts on the CPU.
 */
void plic_init(unsigned long hartid) {
    // Set UART interrupt priority to 1.
    // The priority must be greater than 0 to trigger the interrupt.
    *PLIC_PRIORITY(UART_IRQ) = 1;

    // Enable the UART interrupt for the current hart.
    // UART_IRQ is the interrupt source number, so set its bit in the enable register.
    volatile  unsigned int* enable = PLIC_ENABLE(hartid);
    unsigned int word_index = UART_IRQ / 32;
    unsigned int bit_index  = UART_IRQ % 32;

    enable[word_index] |= (1U << bit_index);

    // Set the threshold to 0.
    // This allows all interrupts with priority greater than 0.
    *PLIC_THRESHOLD(hartid) = 0;

    // Enable external interrupts on the CPU.
    enable_external_interrupt();
}
/*
 * Claim a pending interrupt from the PLIC.
 * The returned value is the IRQ number that should be handled.
 */
int plic_claim(unsigned long hartid) {
    return *PLIC_CLAIM(hartid);
}
/*
 * Complete the interrupt handling.
 * Writing the IRQ number back tells the PLIC that the interrupt is done.
 */
void plic_complete(int irq, unsigned long hartid) {
    *PLIC_CLAIM(hartid) = irq;
}

