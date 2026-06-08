#include "syscall.h"
#include "thread.h"
#include "trap.h"
#include "uart.h"
#include "video.h"
#include "vfs.h"

#define SSTATUS_SUM (1UL << 18)

static inline void enable_sum(void)
{
    asm volatile("csrs sstatus, %0" :: "r"(SSTATUS_SUM) : "memory");
}

static inline void disable_sum(void)
{
    asm volatile("csrc sstatus, %0" :: "r"(SSTATUS_SUM) : "memory");
}
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
        // enable_sum();
        uart_putc(buf[i]);
        // disable_sum();
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
        char c = uart_getc();
        // enable_sum();
        buf[i] = c;
        // disable_sum();
    }

    return count;
}

static int alloc_fd(struct task_struct* task, struct file* file)
{
    for (int i = 0; i < MAX_FD; i++) {
        if (task->fd_table[i] == 0) {
            task->fd_table[i] = file;
            return i;
        }
    }

    return -1;
}

static struct file* get_file_from_fd(struct task_struct* task, int fd)
{
    if (task == 0 || fd < 0 || fd >= MAX_FD) {
        return 0;
    }

    return task->fd_table[fd];
}

static long sys_open(const char* pathname, int flags)
{
    struct task_struct* current = get_current();
    struct file* file = 0;

    if (current == 0) return -1;

    if (vfs_open(pathname, flags, &file) != 0) return -1;

    int fd = alloc_fd(current, file);

    if (fd < 0) {
        vfs_close(file);
        return -1;
    }

    return fd;
}

static long sys_close(int fd)
{
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;

    current->fd_table[fd] = 0;
    return vfs_close(file);
}

static long sys_read(int fd, void* buf, unsigned long count)
{
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;

    return vfs_read(file, buf, count);
}

static long sys_write(int fd, const void* buf, unsigned long count)
{
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;

    return vfs_write(file, buf, count);
}

static long sys_mkdir(const char* pathname, unsigned int mode)
{
    (void)mode;
    return vfs_mkdir(pathname);
}

static long sys_mount(const char* src,
                      const char* target,
                      const char* filesystem,
                      unsigned long flags,
                      const void* data)
{
    (void)src;
    (void)flags;
    (void)data;

    return vfs_mount(target, filesystem);
}

static long sys_chdir(const char* pathname)
{
    return vfs_chdir(pathname);
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
            regs->a0 = (process_exec((const char*)regs->a0, regs) < 0) ? -1 : 0;
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
        case SYS_MMAP:
            regs->a0 = (unsigned long)process_mmap((void*)regs->a0,
                                                regs->a1,
                                                (int)regs->a2,
                                                (int)regs->a3);
            break;
        case SYS_OPEN:
            regs->a0 = sys_open((const char*)regs->a0, (int)regs->a1);
            break;

        case SYS_CLOSE:
            regs->a0 = sys_close((int)regs->a0);
            break;

        case SYS_READ:
            regs->a0 = sys_read((int)regs->a0, (void*)regs->a1, regs->a2);
            break;

        case SYS_WRITE:
            regs->a0 = sys_write((int)regs->a0, (const void*)regs->a1, regs->a2);
            break;

        case SYS_MKDIR:
            regs->a0 = sys_mkdir((const char*)regs->a0,
                                (unsigned int)regs->a1);
            break;

        case SYS_MOUNT:
            regs->a0 = sys_mount((const char*)regs->a0,
                                (const char*)regs->a1,
                                (const char*)regs->a2,
                                regs->a3,
                                (const void*)regs->a4);
            break;

        case SYS_CHDIR:
            regs->a0 = sys_chdir((const char*)regs->a0);
            break;

        default:
            uart_puts("[syscall] unknown syscall: ");
            uart_hex(regs->a7);
            uart_puts("\n");

            regs->a0 = -1;
            break;
    }
}