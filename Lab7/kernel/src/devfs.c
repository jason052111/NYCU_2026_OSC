#include "devfs.h"
#include "buddy.h"
#include "tool.h"
#include "uart.h"
#include "video.h"

#define DEVFS_MAX_NAME  16
#define DEVFS_MAX_ENTRY 2
/*
 * Filesystem-specific data associated with one devfs vnode.
 * devfs contains predefined device files such as "uart" and "fb".
 * Directory vnodes use entry[] to store their child device vnodes,
 * while device-file vnodes mainly use name.
 */
struct devfs_vnode {
    char name[DEVFS_MAX_NAME];            // Component name of this directory or device file.
    struct vnode* entry[DEVFS_MAX_ENTRY]; // Child device vnodes when this vnode is a directory.
};

static int devfs_open(struct vnode* file_node, struct file** target);
static int devfs_close(struct file* file);
static int uart_device_read(struct file* file, void* buf, size_t len);
static int uart_device_write(struct file* file, const void* buf, size_t len);
static int framebuffer_device_write(struct file* file, const void* buf, size_t len);
static long framebuffer_device_lseek64(struct file* file, long offset, int whence);
static int framebuffer_device_ioctl(struct file* file, unsigned long request, void* arg);
static int devfs_invalid_read(struct file* file, void* buf, size_t len);
static int devfs_invalid_write(struct file* file, const void* buf, size_t len);
static int devfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name);
static int devfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name);
static int devfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name);

static struct file_operations devfs_dir_file_ops = {
    .open = devfs_open,
    .close = devfs_close,
    .read = devfs_invalid_read,
    .write = devfs_invalid_write,
};

static struct file_operations uart_device_file_ops = {
    .open = devfs_open,
    .close = devfs_close,
    .read = uart_device_read,
    .write = uart_device_write,
};

static struct file_operations framebuffer_device_file_ops = {
    .open = devfs_open,
    .close = devfs_close,
    .read = devfs_invalid_read,
    .write = framebuffer_device_write,
    .lseek64 = framebuffer_device_lseek64,
    .ioctl = framebuffer_device_ioctl,
};

static struct vnode_operations devfs_vnode_ops = {
    .lookup = devfs_lookup,
    .create = devfs_create,
    .mkdir = devfs_mkdir,
};
/*
 * Copy a component name into a fixed-size devfs name buffer.
 * At most DEVFS_MAX_NAME - 1 characters are copied so the resulting
 * string always has space for a terminating null character.
 */
static void devfs_copy_name(char* dst, const char* src) {
    int i = 0;
    while (src[i] != '\0' && i < DEVFS_MAX_NAME - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}
/*
 * Allocate and initialize one devfs vnode.
 * f_ops determines the behavior of the created vnode. For example,
 * "/dev/uart" receives UART operations, while "/dev/fb" receives
 * framebuffer operations.
 */
static struct vnode* devfs_create_vnode(enum vnode_type type, struct file_operations* f_ops) {
    struct vnode* vnode = (struct vnode*)allocate(sizeof(struct vnode));

    struct devfs_vnode* inode = (struct devfs_vnode*)allocate(sizeof(struct devfs_vnode));

    if (vnode == 0 || inode == 0) {
        if (vnode != 0) free(vnode);
        if (inode != 0) free(inode);
        return 0;
    }

    mem_zero(vnode, sizeof(struct vnode));
    mem_zero(inode, sizeof(struct devfs_vnode));
    vnode->type = type;
    vnode->v_ops = &devfs_vnode_ops;
    vnode->f_ops = f_ops;
    vnode->internal = inode;
    return vnode;
}
/*
 * Initialize an already allocated file handle for a devfs vnode.
 */
static int devfs_open(struct vnode* file_node, struct file** target) {
    if (file_node == 0 || target == 0 || *target == 0) return -1;
    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}
/*
 * Release an opened devfs file handle.
 * The device vnode itself remains alive after the handle is closed.
 */
static int devfs_close(struct file* file) {
    if (file == 0) return -1;
    free(file);
    return 0;
}
/*
 * Read characters from the UART device into a kernel buffer.
 * sys_read() later copies this kernel buffer back to the user buffer
 * through copy_to_user().
 */
static int uart_device_read(struct file* file, void* buf, size_t len) {
    if (buf == 0) return -1;
    for (size_t i = 0; i < len; i++) 
        ((char*)buf)[i] = uart_getc();
    return (int)len;
}
/*
 * Write characters from a kernel buffer to the UART device.
 * sys_write() previously copied the data from userspace into this
 * kernel buffer through copy_from_user().
 */
static int uart_device_write(struct file* file, const void* buf, size_t len) {
    if (buf == 0) return -1;
    for (size_t i = 0; i < len; i++) 
        uart_putc(((const char*)buf)[i]);
    return (int)len;
}
/*
 * Write data from a kernel buffer into the framebuffer device.
 * file->f_pos is interpreted as a byte offset from the framebuffer base.
 * After a successful write, f_pos advances by the number of bytes written.
 */
static int framebuffer_device_write(struct file* file, const void* buf, size_t len) {
    if (file == 0 || buf == 0) return -1;
    int result = video_framebuffer_write(file->f_pos, buf, len);
    if (result < 0) return -1;
    file->f_pos += result;
    return result;
}
/*
 * Change the byte position used by the next framebuffer write.
 * This lab only requires SEEK_SET, so offset is measured from the
 * beginning of framebuffer memory.
 */
static long framebuffer_device_lseek64(struct file* file, long offset, int whence) {
    if (file == 0) return -1;
    if (whence != SEEK_SET) return -1;
    if (offset < 0) return -1;

    unsigned long fb_size = (unsigned long)FB_WIDTH * FB_HEIGHT * FB_BPP;
    if ((unsigned long)offset > fb_size) return -1;

    file->f_pos = (size_t)offset;
    return offset;
}
/*
 * Handle device-specific control requests for the framebuffer.
 * FB_IOCTL_GET_INFO returns the display width, height, and bytes per
 * pixel through the supplied framebuffer_info structure.
 */
static int framebuffer_device_ioctl(struct file* file, unsigned long request, void* arg) {
    if (arg == 0) return -1;
    if (request != FB_IOCTL_GET_INFO) return -1;
    return video_framebuffer_get_info((struct framebuffer_info*)arg);
}
/*
 * Reject read operations for a vnode that does not support reading.
 * This is used by directory vnodes and the write-only framebuffer file.
 */
static int devfs_invalid_read(struct file* file, void* buf, size_t len) {
    return -1;
}
/*
 * Reject write operations for a vnode that does not support writing.
 * This is primarily used by devfs directory vnodes.
 */
static int devfs_invalid_write(struct file* file, const void* buf, size_t len) {
    return -1;
}
/*
 * Find a predefined device vnode by name inside a devfs directory.
 * Examples include looking up "uart" and "fb" while resolving
 * "/dev/uart" and "/dev/fb".
 */
static int devfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    if (dir_node == 0 || target == 0 || component_name == 0) return -1;
    if (dir_node->type != VNODE_DIR) return -1;

    struct devfs_vnode* dir = (struct devfs_vnode*)dir_node->internal;
    if (dir == 0) return -1;
    for (int i = 0; i < DEVFS_MAX_ENTRY; i++) {
        if (dir->entry[i] == 0) continue;

        struct devfs_vnode* inode = (struct devfs_vnode*)dir->entry[i]->internal;

        if (inode != 0 &&
            strcmp(inode->name, component_name) == 0) {
            *target = dir->entry[i];
            return 0;
        }
    }

    return -1;
}
/*
 * Reject dynamic device-file creation.
 * This devfs contains only the predefined device files created during
 * mount setup, so create() is not supported.
 */
static int devfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1;
}
/*
 * Reject dynamic directory creation inside devfs.
 * The devfs directory structure is fixed when the filesystem is mounted.
 */
static int devfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1;
}
/*
 * Create and initialize one mounted devfs instance.
 * The mounted filesystem contains:
 * /dev
 * ├── uart
 * └── fb
 * Each device vnode receives its own file_operations table, allowing
 * VFS to access the device without hardcoding device behavior.
 */
int devfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    if (fs == 0 || mnt == 0) return -1;

    struct vnode* root = devfs_create_vnode(VNODE_DIR, &devfs_dir_file_ops);
    if (root == 0) return -1;

    struct vnode* uart = devfs_create_vnode(VNODE_FILE, &uart_device_file_ops);
    if (uart == 0) {
        free(root->internal);
        free(root);
        return -1;
    }

    struct vnode* fb = devfs_create_vnode(VNODE_FILE, &framebuffer_device_file_ops);
    if (fb == 0) {
        free(uart->internal);
        free(uart);
        free(root->internal);
        free(root);
        return -1;
    }

    struct devfs_vnode* root_inode = (struct devfs_vnode*)root->internal;
    struct devfs_vnode* uart_inode = (struct devfs_vnode*)uart->internal;
    struct devfs_vnode* fb_inode = (struct devfs_vnode*)fb->internal;

    devfs_copy_name(uart_inode->name, "uart");
    devfs_copy_name(fb_inode->name, "fb");

    root->parent = root;
    root->owner_mount = mnt;
    uart->parent = root;
    uart->owner_mount = mnt;
    fb->parent = root;
    fb->owner_mount = mnt;
    root_inode->entry[0] = uart;
    root_inode->entry[1] = fb;
    mnt->root = root;
    mnt->fs = fs;
    return 0;
}
