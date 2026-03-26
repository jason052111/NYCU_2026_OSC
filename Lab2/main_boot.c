#include <stdint.h>
#include <stddef.h>

extern void prompt(void);
extern void run_command(const char* cmd, unsigned long boot_hartid, unsigned long boot_dtb);
extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern void uart_init(unsigned long dtb_addr);

#define CMD_BUF_LEN       128

void start_kernel(unsigned long hartid, unsigned long dtb) {
    uart_init(dtb);

    unsigned long boot_hartid = hartid; 
    unsigned long boot_dtb = dtb;

    char buf[CMD_BUF_LEN];
    int len = 0;

    extern char _start[];
    unsigned long current_pc; 
    // Get current PC via auipc; "=r" tells the compiler to use any write-only general register for current_pc.
    asm volatile ("auipc %0, 0" : "=r" (current_pc));

    uart_puts("Reality BOOT(PC): ");
    uart_hex(current_pc);
    uart_puts("\nIdeal (Linker _start): ");
    uart_hex((unsigned long)_start);
    uart_puts("\n");
    
    prompt();
    while (1) {
        char c = uart_getc(); 
        if (c == '\n') {
            uart_putc('\n');          
            buf[len] = '\0';          
            run_command(buf, boot_hartid, boot_dtb);         
            len = 0;                 
            prompt();           
            continue;
        }

        if (c == 0x08 || c == 0x7f) {                 // Backspace ( some terminals send 0x08, while others send 0x7f )
            if (len > 0) {
                len--;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
            continue;
        }

        if (len < CMD_BUF_LEN - 1) {                  // Normal character: store it in the buffer and echo it back.
            buf[len++] = c;
            uart_putc(c);
        } 
    }
}