#include "buddy.h"
#include "initrd.h"
#include "trap.h"
#include "thread.h"
#include "uart.h"
#include "cmd_kernel.h"

extern void mem_init(unsigned long dtb_addr);
extern void set_initrd_address(unsigned long dtb_addr);
extern void set_time_base_freq(unsigned long dtb_addr);
extern void irq_enable();
extern void enable_timer_interrupt();
extern void timer_init();
extern void thread_init();
extern void idle();

#define CMD_BUF_LEN       128

unsigned long BOOT_HARTID = 0;
unsigned long BOOT_DTB = 0;

void shell_thread() {
    char buf[CMD_BUF_LEN];
    int len = 0;

    prompt();

    while (1) {
        char c = uart_getc();

        if (c == '\n') {
            uart_putc('\n');
            buf[len] = '\0';
            run_command(buf, BOOT_HARTID, BOOT_DTB);
            len = 0;
            prompt();
            continue;
        }

        if (c == 0x08 || c == 0x7f) {
            if (len > 0) {
                len--;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
            continue;
        }

        if (len < CMD_BUF_LEN - 1) {
            buf[len++] = c;
            uart_putc(c);
        }
    }
}

void start_kernel(unsigned long hartid, unsigned long dtb) {
    BOOT_HARTID = hartid;
    BOOT_DTB = dtb;
    uart_init(BOOT_DTB);
    plic_init(BOOT_HARTID);
    mem_init(BOOT_DTB);
    set_initrd_address(BOOT_DTB);
    set_time_base_freq(BOOT_DTB);
    enable_timer_interrupt();
    irq_enable();
    timer_init();
    thread_init();              
    kthread_create(shell_thread); 

    idle();                   
}