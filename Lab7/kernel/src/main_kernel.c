#include "buddy.h"
#include "initrd.h"
#include "trap.h"
#include "thread.h"
#include "uart.h"
#include "cmd_kernel.h"
#include "vm.h"
#include "vfs.h"

extern void test_vfs_basic1(void);
extern void test_vfs_basic2_mkdir(void);
extern void test_vfs_basic2_mount(void);
extern void test_vfs_basic2_mount_isolation(void);
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
unsigned long BOOT_DTB_PA = 0;
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
    BOOT_DTB_PA = dtb;
    BOOT_DTB = PA2VA(dtb);
    uart_init(BOOT_DTB);
    plic_init(BOOT_HARTID);
    mem_init(BOOT_DTB);
    setup_final_vm(BOOT_DTB);   
    asm volatile("csrs sstatus, %0" :: "r"(1<<18) : "memory");
    set_initrd_address(BOOT_DTB);
    set_time_base_freq(BOOT_DTB);
    enable_timer_interrupt();
    irq_enable();
    timer_init();  
    if (vfs_init() != 0) {
        uart_puts("vfs_init failed\n");
        while (1);
    }   

    thread_init();  
    if (vfs_mkdir("/ramfs") != 0) {
        uart_puts("create /ramfs failed\n");
        while (1);
    }

    if (vfs_mount("/ramfs", "ramfs") != 0) {
        uart_puts("mount ramfs failed\n");
        while (1);
    }
    kthread_create(shell_thread); 


    uart_puts("\nStarting kernel at : ");
    uart_hex((unsigned long)start_kernel);
    uart_puts("\n");

    test_vfs_basic1();
    test_vfs_basic2_mkdir();
    test_vfs_basic2_mount();
    test_vfs_basic2_mount_isolation();
    idle();                   
}