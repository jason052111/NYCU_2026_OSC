#include "ramfs.h"
#include "initrd.h"
#include "buddy.h"
#include "tool.h"

#define RAMFS_MAX_NAME  16
#define RAMFS_MAX_ENTRY 64
/*
 * Filesystem-specific data associated with one ramfs vnode.
 * ramfs exposes files stored in the initramfs CPIO archive and keeps
 * them read-only, so file data points directly into the archive instead
 * of being copied into a new writable buffer.
 */
struct ramfs_vnode {
    char name[RAMFS_MAX_NAME];            // Component name stored inside the parent directory.
    struct vnode* entry[RAMFS_MAX_ENTRY]; // Child vnodes when this node represents a directory.
    const char* data;                     // Read-only address of file data inside the CPIO archive.
    size_t size;                          // Size of the file data in bytes.
};

static int ramfs_open(struct vnode* file_node, struct file** target);
static int ramfs_close(struct file* file);
static int ramfs_read(struct file* file, void* buf, size_t len);
static int ramfs_write(struct file* file, const void* buf, size_t len);
static int ramfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name);
static int ramfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name);
static int ramfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name);

static struct file_operations ramfs_file_ops = {
    .open = ramfs_open,
    .close = ramfs_close,
    .read = ramfs_read,
    .write = ramfs_write,
};

static struct vnode_operations ramfs_vnode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
};
/*
 * Copy a component name into the fixed-size name buffer of a ramfs node.
 * At most RAMFS_MAX_NAME - 1 characters are copied so the resulting
 * string always has space for the terminating null character.
 */
static void ramfs_copy_name(char* dst, const char* src) {
    int i = 0;
    while (src[i] != '\0' && i < RAMFS_MAX_NAME - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}
/*
 * Allocate and initialize one ramfs vnode.
 * This function creates both the generic VFS vnode and the private
 * ramfs_vnode stored through vnode->internal.
 */
static struct vnode* ramfs_create_vnode(enum vnode_type type) {
    struct vnode* vnode = (struct vnode*)allocate(sizeof(struct vnode));
    struct ramfs_vnode* inode = (struct ramfs_vnode*)allocate(sizeof(struct ramfs_vnode));

    if (vnode == 0 || inode == 0) {
        if (vnode != 0) free(vnode);
        if (inode != 0) free(inode);
        return 0;
    }

    mem_zero(vnode, sizeof(struct vnode));
    mem_zero(inode, sizeof(struct ramfs_vnode));
    vnode->type = type;
    vnode->v_ops = &ramfs_vnode_ops;
    vnode->f_ops = &ramfs_file_ops;
    vnode->internal = inode;
    return vnode;
}
/*
 * Initialize an already allocated file handle for a ramfs vnode.
 */
static int ramfs_open(struct vnode* file_node, struct file** target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = &ramfs_file_ops;
    (*target)->f_pos = 0;
    return 0;
}
/*
 * Release an opened ramfs file handle.
 * The vnode and CPIO data remain alive because close() only frees the
 * temporary file handle created by vfs_open().
 */
static int ramfs_close(struct file* file) {
    free(file);
    return 0;
}
/*
 * Read data from a ramfs file into a kernel buffer.
 * The data is read directly from its original location in the CPIO
 * archive. No separate copy of the complete file is stored by ramfs.
 */
static int ramfs_read(struct file* file, void* buf, size_t len) {
    struct ramfs_vnode* inode = (struct ramfs_vnode*)file->vnode->internal;

    if (file->vnode->type != VNODE_FILE) return -1;
    if (file->f_pos >= inode->size) return 0;

    size_t readable = inode->size - file->f_pos;
    if (len > readable) len = readable;
    mem_copy(buf, inode->data + file->f_pos, len);
    file->f_pos += len;
    return (int)len;
}
/*
 * Reject every write request because ramfs is read-only.
 */
static int ramfs_write(struct file* file, const void* buf, size_t len) {
    return -1;
}
/*
 * Find a child vnode by name inside a ramfs directory.
 */
static int ramfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    struct ramfs_vnode* dir = (struct ramfs_vnode*)dir_node->internal;
    if (dir_node->type != VNODE_DIR) return -1;
    for (int i = 0; i < RAMFS_MAX_ENTRY; i++) {
        if (dir->entry[i] == 0) continue;
        struct ramfs_vnode* inode = (struct ramfs_vnode*)dir->entry[i]->internal;
        if (strcmp(inode->name, component_name) == 0) {
            *target = dir->entry[i];
            return 0;
        }
    }

    return -1;
}
/*
 * Reject regular-file creation because ramfs is read-only.
 */
static int ramfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1;
}
/*
 * Reject directory creation because ramfs is read-only.
 */
static int ramfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1;
}
// ============================================================================================================= //
/*
 * Create and initialize one mounted ramfs instance.
 * This function creates the ramfs root directory and parses every file
 * in the initramfs CPIO archive into a read-only vnode.
 * The file data is not copied. Each ramfs inode stores a pointer to the
 * original file data inside the CPIO archive.
 */
int ramfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    struct vnode* root = ramfs_create_vnode(VNODE_DIR);
    if (root == 0) return -1;
    root->parent = root;
    root->owner_mount = mnt;
    mnt->root = root;
    mnt->fs = fs;

    struct ramfs_vnode* root_inode = (struct ramfs_vnode*)root->internal;
    const char* ptr = (const char*)INITRD_BASE;
    int entry_index = 0;

    while (entry_index < RAMFS_MAX_ENTRY) {
        struct cpio_t* header = (struct cpio_t*)ptr;
        if (strncmp(header->magic, "070701", 6) != 0) break;
        int namesize = hextoi(header->namesize, 8);
        int filesize = hextoi(header->filesize, 8);
        const char* filename = ptr + sizeof(struct cpio_t);
        if (strcmp(filename, "TRAILER!!!") == 0) break;
        const char* filedata = (const char*)align_up(filename + namesize, 4);

        struct vnode* file_node = ramfs_create_vnode(VNODE_FILE);
        if (file_node == 0) return -1;
        struct ramfs_vnode* file_inode = (struct ramfs_vnode*)file_node->internal;

        ramfs_copy_name(file_inode->name, filename);
        file_inode->data = filedata;
        file_inode->size = filesize;
        file_node->parent = root;
        file_node->owner_mount = mnt;
        root_inode->entry[entry_index++] = file_node;

        ptr = (const char*)align_up(filedata + filesize, 4);
    }

    return 0;
}