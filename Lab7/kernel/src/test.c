#include "vfs.h"
#include "uart.h"
#include "tool.h"

void test_vfs_basic1(void)
{
    struct file* file = 0;
    char buf[32];

    mem_zero(buf, sizeof(buf));

    if (vfs_open("/file.txt", O_CREAT, &file) != 0) {
        uart_puts("vfs_open create failed\n");
        return;
    }

    if (vfs_write(file, "abc", 3) != 3) {
        uart_puts("vfs_write failed\n");
        return;
    }

    vfs_close(file);

    if (vfs_open("/file.txt", 0, &file) != 0) {
        uart_puts("vfs_open read failed\n");
        return;
    }

    if (vfs_read(file, buf, 3) != 3) {
        uart_puts("vfs_read failed\n");
        return;
    }

    vfs_close(file);

    if (strcmp(buf, "abc") == 0) {
        uart_puts("Basic1 VFS test passed\n");
    } else {
        uart_puts("Basic1 VFS test failed\n");
    }
}

void test_vfs_basic2_mkdir(void)
{
    struct file* file = 0;
    char buf[32];

    mem_zero(buf, sizeof(buf));

    if (vfs_mkdir("/dir") != 0) {
        uart_puts("vfs_mkdir failed\n");
        return;
    }

    if (vfs_open("/dir/file.txt", O_CREAT, &file) != 0) {
        uart_puts("vfs_open /dir/file.txt failed\n");
        return;
    }

    if (vfs_write(file, "xyz", 3) != 3) {
        uart_puts("vfs_write /dir/file.txt failed\n");
        return;
    }

    vfs_close(file);

    if (vfs_open("/dir/file.txt", 0, &file) != 0) {
        uart_puts("vfs_reopen /dir/file.txt failed\n");
        return;
    }

    if (vfs_read(file, buf, 3) != 3) {
        uart_puts("vfs_read /dir/file.txt failed\n");
        return;
    }

    vfs_close(file);

    if (strcmp(buf, "xyz") == 0) {
        uart_puts("Basic2 mkdir test passed\n");
    } else {
        uart_puts("Basic2 mkdir test failed\n");
    }
}

void test_vfs_basic2_mount(void)
{
    struct file* file = 0;
    char buf[32];

    mem_zero(buf, sizeof(buf));

    if (vfs_mkdir("/mnt") != 0) {
        uart_puts("vfs_mkdir /mnt failed\n");
        return;
    }

    if (vfs_mount("/mnt", "tmpfs") != 0) {
        uart_puts("vfs_mount failed\n");
        return;
    }

    if (vfs_open("/mnt/file.txt", O_CREAT, &file) != 0) {
        uart_puts("vfs_open /mnt/file.txt failed\n");
        return;
    }

    if (vfs_write(file, "mnt", 3) != 3) {
        uart_puts("vfs_write /mnt/file.txt failed\n");
        return;
    }

    vfs_close(file);

    mem_zero(buf, sizeof(buf));

    if (vfs_open("/mnt/file.txt", 0, &file) != 0) {
        uart_puts("vfs_reopen /mnt/file.txt failed\n");
        return;
    }

    if (vfs_read(file, buf, 3) != 3) {
        uart_puts("vfs_read /mnt/file.txt failed\n");
        return;
    }

    vfs_close(file);

    if (strcmp(buf, "mnt") == 0) {
        uart_puts("Basic2 mount test passed\n");
    } else {
        uart_puts("Basic2 mount test failed\n");
    }
}

void test_vfs_basic2_mount_isolation(void)
{
    struct file* file = 0;
    char buf[32];

    mem_zero(buf, sizeof(buf));

    if (vfs_mkdir("/iso") != 0) {
        uart_puts("iso mkdir failed\n");
        return;
    }

    if (vfs_open("/iso/hidden.txt", O_CREAT, &file) != 0) {
        uart_puts("iso create hidden failed\n");
        return;
    }

    vfs_write(file, "hidden", 6);
    vfs_close(file);

    if (vfs_mount("/iso", "tmpfs") != 0) {
        uart_puts("iso mount failed\n");
        return;
    }

    if (vfs_open("/iso/hidden.txt", 0, &file) == 0) {
        uart_puts("mount isolation failed: hidden file visible\n");
        vfs_close(file);
        return;
    }

    if (vfs_open("/file.txt", O_CREAT, &file) != 0) {
        uart_puts("root file create failed\n");
        return;
    }

    vfs_write(file, "root", 4);
    vfs_close(file);

    if (vfs_open("/iso/file.txt", O_CREAT, &file) != 0) {
        uart_puts("mounted file create failed\n");
        return;
    }

    vfs_write(file, "mnt", 3);
    vfs_close(file);

    if (vfs_open("/file.txt", 0, &file) != 0) {
        uart_puts("root file reopen failed\n");
        return;
    }

    mem_zero(buf, sizeof(buf));
    vfs_read(file, buf, 4);
    vfs_close(file);

    if (strcmp(buf, "root") != 0) {
        uart_puts("mount isolation failed: root file changed\n");
        return;
    }

    if (vfs_open("/iso/file.txt", 0, &file) != 0) {
        uart_puts("mounted file reopen failed\n");
        return;
    }

    mem_zero(buf, sizeof(buf));
    vfs_read(file, buf, 3);
    vfs_close(file);

    if (strcmp(buf, "mnt") != 0) {
        uart_puts("mount isolation failed: mounted file changed\n");
        return;
    }

    uart_puts("Basic2 mount isolation test passed\n");
}