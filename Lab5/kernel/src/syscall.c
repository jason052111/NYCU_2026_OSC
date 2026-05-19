#include "syscall.h"
#include "thread.h"
#include "trap.h"
#include "uart.h"
#include "video.h"
/*
 * Return the current process ID.
 */
static long sys_getpid(void) {
    struct task_struct* current = get_current();
    if (current == 0) return -1;
    return current->pid;
}
/*
 * Write data from a user buffer to UART.
 * Returns the number of bytes written.
 */
static long sys_uart_write(const char* buf, unsigned long count) {
    if (buf == 0) return -1;
    for (unsigned long i = 0; i < count; i++) {
        uart_putc(buf[i]);
    }

    return count;
}
/*
 * Read characters from UART into a user buffer.
 * Returns the number of bytes read.
 */
static long sys_uart_read(char* buf, unsigned long count) {
    if (buf == 0) return -1;
    for (unsigned long i = 0; i < count; i++) {
        buf[i] = uart_getc();
    }

    return count;
}
/*
 * Dispatch system calls from user programs.
 *
 * The syscall number is stored in a7.
 * Arguments are stored in a0, a1, a2, ...
 * The return value is written back to a0.
 */
void syscall_handler(struct trap_frame* regs) {
    if (regs == 0) {
        return;
    }

    switch (regs->a7) {
        case SYS_GETPID:
            regs->a0 = sys_getpid();
            break;

        case SYS_UART_READ:
            regs->a0 = sys_uart_read((char*)regs->a0,
                                     regs->a1);
            break;

        case SYS_UART_WRITE:
            regs->a0 = sys_uart_write((const char*)regs->a0,
                                      regs->a1);
            break;

        case SYS_EXEC:
            int pid = process_exec((const char*)regs->a0, regs);
            regs->a0 = (pid < 0) ? -1 : 0;
            break;

        case SYS_FORK:
            regs->a0 = process_fork(regs);
            break;

        case SYS_WAITPID:
            regs->a0 = process_waitpid((int)regs->a0);
            break;

        case SYS_EXIT:
            process_exit((int)regs->a0);
            regs->a0 = 0;
            break;

        case SYS_STOP:
            regs->a0 = process_stop((int)regs->a0);
            break;

        case SYS_DISPLAY:
            video_bmp_display((unsigned int*)regs->a0,
                            (int)regs->a1,
                            (int)regs->a2);
            regs->a0 = 0;
            break;

        case SYS_USLEEP:
            regs->a0 = process_usleep((unsigned long)regs->a0);
            break;

        case SYS_SIGNAL:
            regs->a0 = process_signal((int)regs->a0,
                                    (void (*)(void))regs->a1);
            break;

        case SYS_SIGRETURN:
            process_sigreturn(regs);
            break;

        case SYS_KILL:
            regs->a0 = process_kill((int)regs->a0,
                                    (int)regs->a1);
            break;
            
        default:
            uart_puts("[syscall] unknown syscall: ");
            uart_hex(regs->a7);
            uart_puts("\n");

            regs->a0 = -1;
            break;
    }
}