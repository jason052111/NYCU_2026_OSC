#include "syscall.h"
#include "thread.h"
#include "trap.h"
#include "uart.h"
#include "video.h"
#include "vfs.h"
#include "uaccess.h"

#define UACCESS_BUFFER_SIZE 512
#define UACCESS_PATH_SIZE 256
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
static long sys_uart_write(const char* user_buf, unsigned long count) {
    if (count == 0) return 0;
    if (user_buf == 0) return -1;

    char kernel_buf[UACCESS_BUFFER_SIZE];
    unsigned long total = 0;
    while (total < count) {
        unsigned long chunk = count - total;
        if (chunk > UACCESS_BUFFER_SIZE) chunk = UACCESS_BUFFER_SIZE;
        if (copy_from_user(kernel_buf, user_buf + total, chunk) < 0) return total > 0 ? (long)total : -1;
        for (unsigned long i = 0; i < chunk; i++) 
            uart_putc(kernel_buf[i]);

        total += chunk;
    }

    return total;
}
/*
 * Read characters from UART into a user buffer.
 * Returns the number of bytes read.
 */
static long sys_uart_read(char* user_buf, unsigned long count) {
    if (count == 0) return 0;
    if (user_buf == 0) return -1;

    char kernel_buf[UACCESS_BUFFER_SIZE];
    unsigned long total = 0;
    while (total < count) {
        unsigned long chunk = count - total;
        if (chunk > UACCESS_BUFFER_SIZE) chunk = UACCESS_BUFFER_SIZE;
        for (unsigned long i = 0; i < chunk; i++)
            kernel_buf[i] = uart_getc();

        if (copy_to_user(user_buf + total, kernel_buf, chunk) < 0) return total > 0 ? (long)total : -1;
        total += chunk;
    }

    return total;
}
/*
 * Allocate the lowest available file descriptor for a task.
 * The file pointer is stored in the task's file descriptor table.
 * Returns the allocated file descriptor, or -1 if the table is full.
 */
static int alloc_fd(struct task_struct* task, struct file* file) {
    for (int i = 0; i < MAX_FD; i++) {
        if (task->fd_table[i] == 0) {
            task->fd_table[i] = file;
            return i;
        }
    }

    return -1;
}
/*
 * Look up an opened file using a task's file descriptor.
 * Returns the associated file pointer, or null if the task or fd is invalid.
 */
static struct file* get_file_from_fd(struct task_struct* task, int fd) {
    if (task == 0 || fd < 0 || fd >= MAX_FD) return 0;

    return task->fd_table[fd];
}
/*
 * Open a file specified by a userspace pathname.
 * The pathname is copied into kernel memory before it is passed to VFS.
 * Returns a new file descriptor on success, or -1 on failure.
 */
static long sys_open(const char* user_pathname, int flags) {
    struct task_struct* current = get_current();
    struct file* file = 0;
    char pathname[UACCESS_PATH_SIZE];

    if (current == 0) return -1;
    if (copy_string_from_user(pathname, user_pathname, sizeof(pathname)) < 0) return -1;
    if (vfs_open(pathname, flags, &file) != 0) return -1;

    int fd = alloc_fd(current, file);

    if (fd < 0) {
        vfs_close(file);
        return -1;
    }

    return fd;
}
/*
 * Close an opened file descriptor.
 * Returns 0 on success, or -1 if the descriptor is invalid.
 */
static long sys_close(int fd) {
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;

    current->fd_table[fd] = 0;
    return vfs_close(file);
}
/*
 * Read data from an opened file into a userspace buffer.
 * Data is first read into a temporary kernel buffer and then copied
 * to userspace. Returns the number of bytes read, or -1 on failure.
 */
static long sys_read(int fd, void* user_buf, unsigned long count) {
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;
    if (count == 0) return 0;
    if (user_buf == 0) return -1;

    char kernel_buf[UACCESS_BUFFER_SIZE];
    unsigned long total = 0;

    while (total < count) {
        unsigned long chunk = count - total;
        if (chunk > UACCESS_BUFFER_SIZE) chunk = UACCESS_BUFFER_SIZE;
        int result = vfs_read(file, kernel_buf, chunk);

        if (result < 0) return total > 0 ? (long)total : result;
        if (result == 0) break;

        if (copy_to_user((char*)user_buf + total, kernel_buf, result) < 0) return total > 0 ? (long)total : -1;
        total += result;
        if ((unsigned long)result < chunk) break;
    }

    return total;
}
/*
 * Write data from a userspace buffer to an opened file.
 * Data is first copied into a temporary kernel buffer and then passed
 * to VFS. Returns the number of bytes written, or -1 on failure.
 */
static long sys_write(int fd, const void* user_buf, unsigned long count) {
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;
    if (count == 0) return 0;
    if (user_buf == 0) return -1;

    char kernel_buf[UACCESS_BUFFER_SIZE];
    unsigned long total = 0;

    while (total < count) {
        unsigned long chunk = count - total;
        if (chunk > UACCESS_BUFFER_SIZE) chunk = UACCESS_BUFFER_SIZE;

        if (copy_from_user(kernel_buf, (const char*)user_buf + total, chunk) < 0) return total > 0 ? (long)total : -1;
        int result = vfs_write(file, kernel_buf, chunk);

        if (result < 0) return total > 0 ? (long)total : result;
        if (result == 0) break;

        total += result;
        if ((unsigned long)result < chunk) break;
    }

    return total;
}
/*
 * Create a directory using a pathname provided by userspace.
 * Returns 0 on success, or -1 on failure.
 */
static long sys_mkdir(const char* user_pathname, unsigned int mode) {
    char pathname[UACCESS_PATH_SIZE];
    if (copy_string_from_user(pathname, user_pathname, sizeof(pathname)) < 0) return -1;
    return vfs_mkdir(pathname);
}
/*
 * Mount a filesystem on a target directory.
 * Returns 0 on success, or -1 on failure.
 */
static long sys_mount(const char* src, const char* user_target, const char* user_filesystem, unsigned long flags, const void* data) {
    char target[UACCESS_PATH_SIZE];
    char filesystem[UACCESS_PATH_SIZE];
    if (copy_string_from_user(target, user_target, sizeof(target)) < 0) return -1;
    if (copy_string_from_user(filesystem, user_filesystem, sizeof(filesystem)) < 0) return -1;
    return vfs_mount(target, filesystem);
}
/*
 * Change the current task's working directory.
 * Returns 0 on success, or -1 on failure.
 */
static long sys_chdir(const char* user_pathname) {
    char pathname[UACCESS_PATH_SIZE];
    if (copy_string_from_user(pathname, user_pathname, sizeof(pathname)) < 0) return -1;
    return vfs_chdir(pathname);
}
/*
 * Change the current read/write position of an opened file.
 * Only positioning modes supported by the underlying file operation
 * can be used. Returns the resulting offset, or -1 on failure.
 */
static long sys_lseek64(int fd, long offset, int whence)
{
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);
    if (file == 0) return -1;
    return vfs_lseek64(file, offset, whence);
}
/*
 * Send a device-specific control request to an opened file.
 * For the framebuffer, request FB_IOCTL_GET_INFO returns its dimensions
 * and bytes per pixel to userspace.
 */
static long sys_ioctl(int fd, unsigned long request, void* user_arg) {
    struct task_struct* current = get_current();
    struct file* file = get_file_from_fd(current, fd);

    if (file == 0) return -1;
    if (request != FB_IOCTL_GET_INFO) return -1;

    struct framebuffer_info info;

    int result = vfs_ioctl(file, request, &info);
    if (result < 0) return result;
    if (copy_to_user(user_arg, &info, sizeof(info)) < 0) return -1;

    return result;
}
/*
 * Replace the current user process with a new executable.
 * Returns the result of process_exec(), or -1 if the filename cannot
 * be copied from userspace.
 */
static long sys_exec(const char* user_filename, struct trap_frame* regs) {
    char filename[UACCESS_PATH_SIZE];
    if (copy_string_from_user(filename, user_filename, sizeof(filename)) < 0) return -1;
    return process_exec(filename, regs);
}
/*
 * Display a userspace image in the center of the framebuffer.
 * The image is copied from userspace in small chunks before being written
 * to the framebuffer. Returns 0 on success, or -1 on failure.
 */
static long sys_display(const unsigned int* user_image, int width, int height) {
    if (user_image == 0) return -1;
    if (width <= 0 || height <= 0) return -1;
    if (width > FB_WIDTH || height > FB_HEIGHT) return -1;

    char kernel_buf[UACCESS_BUFFER_SIZE];

    unsigned long row_size = (unsigned long)width * FB_BPP;

    unsigned long start_x = (FB_WIDTH - width) / 2;
    unsigned long start_y = (FB_HEIGHT - height) / 2;

    for (int y = 0; y < height; y++) {
        unsigned long copied = 0;

        while (copied < row_size) {
            unsigned long chunk = row_size - copied;

            if (chunk > sizeof(kernel_buf)) chunk = sizeof(kernel_buf);

            const char* user_source = (const char*)user_image + (unsigned long)y * row_size + copied;

            if (copy_from_user(kernel_buf, user_source, chunk) < 0) return -1;

            unsigned long fb_offset = (((start_y + y) * FB_WIDTH + start_x) * FB_BPP) + copied;

            int result = video_framebuffer_write(fb_offset, kernel_buf, chunk);

            if (result != (int)chunk) return -1;

            copied += chunk;
        }
    }

    return 0;
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
            regs->a0 = (sys_exec((const char*)regs->a0, regs) < 0) ? -1 : 0;
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
            regs->a0 = sys_display((const unsigned int*)regs->a0, (int)regs->a1, (int)regs->a2);
            break;

        case SYS_USLEEP:
            regs->a0 = process_usleep((unsigned long)regs->a0);
            break;

        case SYS_SIGNAL:
            regs->a0 = process_signal((int)regs->a0, (void (*)(void))regs->a1);
            break;

        case SYS_SIGRETURN:
            process_sigreturn(regs);
            break;

        case SYS_KILL:
            regs->a0 = process_kill((int)regs->a0, (int)regs->a1);
            break;
        case SYS_MMAP:
            regs->a0 = (unsigned long)process_mmap((void*)regs->a0, regs->a1, (int)regs->a2, (int)regs->a3);
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
            regs->a0 = sys_mkdir((const char*)regs->a0, (unsigned int)regs->a1);
            break;

        case SYS_MOUNT:
            regs->a0 = sys_mount((const char*)regs->a0, (const char*)regs->a1, (const char*)regs->a2, regs->a3, (const void*)regs->a4);
            break;

        case SYS_CHDIR:
            regs->a0 = sys_chdir((const char*)regs->a0);
            break;

        case SYS_LSEEK64:
            regs->a0 = sys_lseek64((int)regs->a0, (long)regs->a1, (int)regs->a2);
            break;

        case SYS_IOCTL:
            regs->a0 = sys_ioctl((int)regs->a0, regs->a1, (void*)regs->a2);
            break;
        default:
            uart_puts("[syscall] unknown syscall: ");
            uart_hex(regs->a7);
            uart_puts("\n");

            regs->a0 = -1;
            break;
    }
}