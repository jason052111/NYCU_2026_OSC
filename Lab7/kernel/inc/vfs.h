#ifndef VFS_H
#define VFS_H

#include <stddef.h>
// open() flag used to create a regular file when the requested pathname does not already exist.
#define O_CREAT 00000100
// lseek64() mode that interprets offset relative to the beginning of the file or device.
#define SEEK_SET 0
/*
 * The type of object represented by a vnode.
 */
enum vnode_type {
    VNODE_DIR,      // The vnode represents a directory.
    VNODE_FILE,     // The vnode represents a regular file or device file.
};
/*
 * A vnode represents one filesystem object inside the VFS.
 * It stores filesystem-independent information and points to the
 * operations and private data supplied by its underlying filesystem.
 */
struct vnode {
    struct mount* mount;              // Filesystem mounted on this vnode, or NULL if none.
    struct mount* owner_mount;        // Mounted filesystem instance that owns this vnode.
    struct vnode* parent;             // Parent directory vnode used when resolving "..".
    enum vnode_type type;             // Whether this vnode represents a file or directory.
    struct vnode_operations* v_ops;   // Filesystem operations performed on this vnode.
    struct file_operations* f_ops;    // Operations available after this vnode is opened.
    void* internal;                   // Filesystem-specific private vnode or inode data.
};
/*
 * A file structure represents one opened file handle.
 * Multiple file descriptors may share the same file structure after
 * fork(), which also means they share the same f_pos.
 */
struct file {
    struct vnode* vnode;              // Vnode representing the opened filesystem object.
    size_t f_pos;                     // Byte offset used by the next read or write operation.
    struct file_operations* f_ops;    // Operations supported by this opened file.
    int flags;                        // Flags originally supplied to open(), such as O_CREAT.
    int ref_count;                    // Number of file descriptors referring to this handle.
};
/*
 * A mount structure represents one mounted filesystem instance.
 */
struct mount {
    struct vnode* root;               // Root vnode of the mounted filesystem.
    struct vnode* mountpoint;         // Vnode in the parent filesystem where this is mounted.
    struct filesystem* fs;            // Filesystem type used by this mounted instance.
};
/*
 * A filesystem structure describes a registered filesystem type.
 * Examples include tmpfs, ramfs, and devfs.
 */
struct filesystem {
    const char* name;                 // Name used to find the filesystem during vfs_mount().
    // Callback that creates and initializes a mount instance.
    int (*setup_mount)(struct filesystem* fs, struct mount* mount);
};
/*
 * Operations that can be performed on an opened file handle.
 * Each filesystem or device provides the methods it supports.
 */
struct file_operations {
    int (*open)(struct vnode* file_node, struct file** target);
    int (*close)(struct file* file);
    int (*read)(struct file* file, void* buf, size_t len);
    int (*write)(struct file* file, const void* buf, size_t len);
    long (*lseek64)(struct file* file, long offset, int whence);
    int (*ioctl)(struct file* file, unsigned long request, void* arg);
};
/*
 * Operations performed directly on vnodes, usually during pathname
 * lookup or filesystem object creation.
 */
struct vnode_operations {
    int (*lookup)(struct vnode* dir_node, struct vnode** target, const char* component_name);
    int (*create)(struct vnode* dir_node, struct vnode** target, const char* component_name);
    int (*mkdir)(struct vnode* dir_node, struct vnode** target, const char* component_name);
};

extern struct mount* rootfs;

int vfs_init(void);
int register_filesystem(struct filesystem* fs);
int vfs_dup(struct file* file);
int vfs_open(const char* pathname, int flags, struct file** target);
int vfs_close(struct file* file);
int vfs_read(struct file* file, void* buf, size_t len);
int vfs_write(struct file* file, const void* buf, size_t len);
int vfs_lookup(const char* pathname, struct vnode** target);
int vfs_mkdir(const char* pathname);
int vfs_mount(const char* target, const char* filesystem);
int vfs_chdir(const char* pathname);
long vfs_lseek64(struct file* file, long offset, int whence);
int vfs_ioctl(struct file* file, unsigned long request, void* arg);

#endif