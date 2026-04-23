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

#define UART_RBR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x0))) // Receive Buffer Register (read). Offset 0x0 from UART base
#define UART_THR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x0))) // Transmit Holding Register (write). Same offset 0x0
#define UART_IER ((volatile unsigned char*)(UART_BASE + UART_OFF(0x1)))
#define UART_IIR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x2)))
#define UART_MCR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x4)))
#define UART_LSR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x5))) // Line Status Register. Offset 0x5 (or 0x5*4 if stride4)
#define LSR_DR   (1 << 0)                                      // LSR bit0: DR (Data Ready) = 1 when RX has data to read
#define LSR_TDRQ (1 << 5)                                      // LSR bit5: THR empty / TX ready (can transmit a new byte).


#define PLIC_PRIORITY(irq)   ((volatile unsigned int*)(PLIC_BASE + (irq) * 4))
#define PLIC_ENABLE(hart)    ((volatile unsigned int*)(PLIC_BASE + 0x002080 + (hart) * 0x0100))
#define PLIC_THRESHOLD(hart) ((volatile unsigned int*)(PLIC_BASE + 0x201000 + (hart) * 0x2000))
#define PLIC_CLAIM(hart)     ((volatile unsigned int*)(PLIC_BASE + 0x201004 + (hart) * 0x2000))
#define UART_BUF_SIZE 128

extern long get_address(const void* dtb_addr, const char *node_path, const char *prop_name);
extern void enable_external_interrupt();
extern size_t strlen(const char *s);
extern unsigned long get_uart_irq(const void * dtb_addr);

static char uart_buf[UART_BUF_SIZE];
static volatile unsigned int uart_buf_r = 0;
static volatile unsigned int uart_buf_w = 0;

unsigned long UART_IRQ = 0xFFFFFFFF;
unsigned long UART_BASE = 0xFFFFFFFF;
unsigned long PLIC_BASE = 0xFFFFFFFF;

volatile unsigned char* uart_rbr_addr(void) {
    return UART_RBR;
}
/*
 * Read a raw byte from the UART.
 * This function is used for binary data transfer.
 * It returns the exact byte received without any modification.
 */

int uart_buf_is_empty(void) {
    return uart_buf_r == uart_buf_w;
}

int uart_buf_is_full(void) {
    return ((uart_buf_w + 1) % UART_BUF_SIZE) == uart_buf_r;
}

void uart_buf_push(char c) {
    if (uart_buf_is_full()) {
        return;
    }
    uart_buf[uart_buf_w] = c;
    uart_buf_w = (uart_buf_w + 1) % UART_BUF_SIZE;
}

char uart_buf_pop(void) {
    while (uart_buf_is_empty()) {
        ;
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

    UART_IRQ  = get_uart_irq(fdt);
    UART_BASE = get_address(fdt, "/soc/serial", "reg");
    PLIC_BASE = get_address(fdt, "/soc/interrupt-controller", "reg");

    *UART_IER |= 1;
    *UART_MCR |= (1 << 3);
}

void plic_init(unsigned long hartid) {
    // (1) Set UART interrupt priority: 優先權設為 1 (只要大於 0 就會觸發)
    *PLIC_PRIORITY(UART_IRQ) = 1;

    // (2) Set UART interrupt enable: 幫目前的 Hart 開啟 UART 的接收開關
    // 因為 UART_IRQ 是 10，所以我們要把第 10 個 bit 設為 1
    volatile  unsigned int* enable = PLIC_ENABLE(hartid);
    unsigned int word_index = UART_IRQ / 32;
    unsigned int bit_index  = UART_IRQ % 32;

    enable[word_index] |= (1U << bit_index);

    // (3) Set threshold: 門檻設為 0，代表只要有中斷就進來，不攔截
    *PLIC_THRESHOLD(hartid) = 0;

    // (4) Enable external interrupts: 讓 CPU 準備好接收外部訊號
    enable_external_interrupt();
}

int plic_claim(unsigned long hartid) {
    // 讀取這個位址，PLIC 會告訴你現在是哪個 IRQ 編號在找你
    return *PLIC_CLAIM(hartid);
}

void plic_complete(int irq, unsigned long hartid) {
    // 把剛才領取的 IRQ 編號寫回去，告訴 PLIC：「我處理完這件事了，可以結案了」
    *PLIC_CLAIM(hartid) = irq;
}

