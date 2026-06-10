#include "vfs.h"
#include "tmpfs.h"
#include "buddy.h"
#include "tool.h"
#include "thread.h"
#include "ramfs.h"
#include "devfs.h"

#define MAX_FS   16
#define PATH_MAX 255
/*
 * The root mount of the whole virtual file system.
 * rootfs->root is the starting vnode for absolute pathname lookup.
 */
struct mount* rootfs = 0;
/*
 * Registered filesystem types.
 * Each entry stores a filesystem name and its mount setup function.
 * vfs_mount() searches this table to find the requested filesystem.
 */
static struct filesystem fs_list[MAX_FS];
/*
 * Split a pathname into its parent pathname and final component.
 * Examples:
 * "/dir/file" -> parent = "/dir", child = "file"
 * "/file"     -> parent = "/",    child = "file"
 * "file"      -> parent = "",     child = "file"
 * child points directly into the original pathname, so no additional
 * memory is allocated for the final component.
 */
static int split_parent_child(const char* pathname, char* parent, const char** child) {
    int len = strlen(pathname);
    int last_slash = -1;

    if (len == 0 || pathname[len - 1] == '/') return -1;

    for (int i = 0; i < len; i++)
        if (pathname[i] == '/') last_slash = i;

    if (last_slash < 0) {
        parent[0] = '\0';
        *child = pathname;
        return 0;
    }

    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        for (int i = 0; i < last_slash; i++) 
            parent[i] = pathname[i];
        
        parent[last_slash] = '\0';
    }

    *child = pathname + last_slash + 1;
    return 0;
}
/*
 * Find a registered filesystem by name.
 * For example, searching for "tmpfs" returns the corresponding
 * filesystem structure containing its setup_mount callback.
 */
static struct filesystem* find_filesystem(const char* name) {
    for (int i = 0; i < MAX_FS; i++) 
        if (fs_list[i].name != 0 && strcmp(fs_list[i].name, name) == 0) return &fs_list[i];

    return 0;
}
/*
 * Follow mount points until reaching the vnode that should actually be used for pathname lookup.
 * When a filesystem is mounted on a vnode, accessing that vnode should
 * enter the root vnode of the mounted filesystem.
 */
static struct vnode* follow_mount(struct vnode* node) {
    while (node->mount != 0) node = node->mount->root;
    return node;
}
/*
 * Find the logical parent of a vnode during ".." pathname lookup.
 * This function handles both normal parent relationships and crossing
 * from the root of a mounted filesystem back to its parent filesystem.
 */
static struct vnode* lookup_parent(struct vnode* node, struct vnode* process_root) {
    if (node == process_root) return node;
    /*
     * If the current vnode is the root of a mounted filesystem,
     * ".." should leave that filesystem.
     * The mounted root corresponds to the mountpoint in the parent
     * filesystem, so return the parent of that mountpoint.
     */
    if (node->owner_mount != 0 && node == node->owner_mount->root && node->owner_mount->mountpoint != 0)
        return node->owner_mount->mountpoint->parent;

    return node->parent;
}
/*
 * Initialize the virtual file system.
 * tmpfs is mounted directly as the root filesystem. ramfs and devfs
 * are registered here so they can be mounted later by name.
 */
int vfs_init(void)
{
    rootfs = (struct mount*)allocate(sizeof(struct mount));
    if (rootfs == 0) return -1;
    mem_zero(rootfs, sizeof(struct mount));
    struct filesystem tmpfs = {.name = "tmpfs", .setup_mount = tmpfs_setup_mount};
    int tmpfs_id = register_filesystem(&tmpfs);
    if (tmpfs_id < 0) return -1;
    if (fs_list[tmpfs_id].setup_mount(&fs_list[tmpfs_id], rootfs) != 0) return -1;
    struct filesystem ramfs = {.name = "ramfs", .setup_mount = ramfs_setup_mount};
    if (register_filesystem(&ramfs) < 0) return -1;
    struct filesystem devfs = {.name = "devfs", .setup_mount = devfs_setup_mount};
    if (register_filesystem(&devfs) < 0) return -1;
    return 0;
}
/*
 * Register a filesystem type in the global filesystem table.
 * The filesystem structure provides its name and setup_mount callback.
 * The returned index can later be used to access the registered entry.
 */
int register_filesystem(struct filesystem* fs) {
    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name == 0) {
            fs_list[i].name = fs->name;
            fs_list[i].setup_mount = fs->setup_mount;
            return i;
        }
    }
    return -1;
}
/*
 * Duplicate a reference to an opened file handle.
 * fork() uses this function when the parent and child share the same
 * struct file. Sharing the structure also means they share f_pos.
 */
int vfs_dup(struct file* file) {
    if (file == 0) return -1;
    file->ref_count++;
    return 0;
}
/*
 * Open a file through the VFS.
 * If the pathname exists, this function creates a new file handle for
 * its vnode. If it does not exist and O_CREAT is specified, the file is
 * first created through the parent filesystem's create operation.
 */
int vfs_open(const char* pathname, int flags, struct file** target) {
    struct vnode* vnode = 0;
    // Look up the vnode corresponding to the requested pathname.
    int ret = vfs_lookup(pathname, &vnode);
    /*
     * If lookup fails, only continue when O_CREAT is specified.
     * Without O_CREAT, opening a nonexistent file must fail.
     */
    if (ret != 0) {
        if (!(flags & O_CREAT)) return -1;
        char parent_path[PATH_MAX];
        const char* filename = 0;
        mem_zero(parent_path, PATH_MAX);
        if (split_parent_child(pathname, parent_path, &filename) != 0) return -1;
        // Find the directory in which the new file should be created.
        if (vfs_lookup(parent_path, &vnode) != 0) return -1;
        /*
         * Ask the underlying filesystem to create the regular file.
         * On success, vnode is updated to point to the new file vnode.
         */
        if (vnode->v_ops->create(vnode, &vnode, filename) != 0) return -1;
    }

    *target = (struct file*)allocate(sizeof(struct file));
    if (*target == 0) return -1;
    mem_zero(*target, sizeof(struct file));
    (*target)->flags = flags;
    (*target)->ref_count = 1;
    /*
     * Call the open method provided by the vnode's filesystem.
     * The filesystem initializes fields such as vnode, f_ops, and f_pos
     * inside the newly allocated file handle.
     */
    ret = vnode->f_ops->open(vnode, target);

    if (ret != 0) {
        free(*target);
        *target = 0;
        return -1;
    }

    return 0;
}
/*
 * Close one reference to an opened file handle.
 * A file handle may be shared by multiple file descriptors after fork(),
 * so it is released only when its reference count reaches zero.
 */
int vfs_close(struct file* file) {
    if (file == 0) return -1;
    file->ref_count--;
    if (file->ref_count > 0) return 0;
    return file->f_ops->close(file);
}
/*
 * Read data from an opened file.
 * VFS dispatches the operation to the read implementation associated
 * with this file handle. The underlying implementation is responsible
 * for reading data and updating f_pos when appropriate.
 */
int vfs_read(struct file* file, void* buf, size_t len) {
    return file->f_ops->read(file, buf, len);
}
/*
 * Write data to an opened file.
 * VFS dispatches the operation to the write implementation associated
 * with this file handle. The underlying implementation is responsible
 * for writing data and updating f_pos when appropriate.
 */
int vfs_write(struct file* file, const void* buf, size_t len) {
    return file->f_ops->write(file, buf, len);
}
/*
 * Resolve a pathname into its final vnode.
 * Path lookup supports:
 * - absolute and relative paths
 * - current working directory
 * - ".", "..", and repeated '/'
 * - entering and leaving mounted filesystems
 */
int vfs_lookup(const char* pathname, struct vnode** target) {
    struct task_struct* current = get_current();
    struct vnode* process_root = rootfs->root;
    struct vnode* node;
    int len = strlen(pathname);
    int i = 0;
    // Use the process-specific root when it has been initialized.
    if (current != 0 && current->root != 0) process_root = current->root;
    /*
     * Select the starting vnode.
     * Absolute paths begin at the process root.
     * Relative paths begin at the current working directory.
     */
    if (pathname[0] == '/') node = process_root;
    else if (current != 0 && current->cwd != 0) node = current->cwd;
    else node = process_root; // fallback 
    if (len == 0) {
        *target = node;
        return 0;
    }
    // Parse and resolve each pathname component.
    while (i < len) {
        while (i < len && pathname[i] == '/') i++;

        if (i >= len) break;
        char component[PATH_MAX];
        int component_len = 0;
        mem_zero(component, PATH_MAX);
        // Copy the next component until '/' or the end of the pathname.
        while (i < len && pathname[i] != '/') {
            if (component_len >= PATH_MAX - 1) return -1;
            component[component_len++] = pathname[i++];
        }
        component[component_len] = '\0';
        // "." refers to the current vnode, so no traversal is required.
        if (strcmp(component, ".") == 0) continue;
        /*
         * ".." refers to the logical parent vnode.
         * lookup_parent() also handles staying at the process root and
         * leaving the root of a mounted filesystem.
         */
        if (strcmp(component, "..") == 0) {
            node = lookup_parent(node, process_root);
            continue;
        }
        /*
         * Ask the current vnode's filesystem to find the named child.
         * On success, node is replaced by the child vnode.
         */
        if (node->v_ops->lookup(node, &node, component) != 0) return -1;
        /*
         * If another filesystem is mounted on the resulting vnode,
         * continue lookup from the root of that mounted filesystem.
         */
        node = follow_mount(node);
    }

    *target = node;
    return 0;
}
/*
 * Create a directory through the VFS.
 * The pathname is separated into its parent path and final directory
 * name. The underlying filesystem creates the actual directory vnode.
 */
int vfs_mkdir(const char* pathname) {
    struct vnode* dir_node = 0;
    struct vnode* target = 0;
    char parent_path[PATH_MAX];
    const char* dirname = 0;
    mem_zero(parent_path, PATH_MAX);
    if (split_parent_child(pathname, parent_path, &dirname) != 0) return -1;
    if (vfs_lookup(parent_path, &dir_node) != 0) return -1;
    return dir_node->v_ops->mkdir(dir_node, &target, dirname);
}
/*
 * Mount a registered filesystem on an existing directory vnode.
 */
int vfs_mount(const char* target, const char* filesystem) {
    struct vnode* mount_point = 0;
    struct filesystem* fs = find_filesystem(filesystem);

    if (fs == 0) return -1;
    // Resolve the target pathname into the mount-point vnode.
    if (vfs_lookup(target, &mount_point) != 0) return -1;
    if (mount_point->type != VNODE_DIR) return -1;
    if (mount_point->mount != 0) return -1;

    struct mount* mnt = (struct mount*)allocate(sizeof(struct mount));
    if (mnt == 0) return -1;
    mem_zero(mnt, sizeof(struct mount));
    /*
     * Ask the filesystem to create and initialize its root vnode.
     * setup_mount() also records the filesystem pointer in mnt and sets
     * each created vnode's owner_mount to this mount object.
     */
    if (fs->setup_mount(fs, mnt) != 0) {
        free(mnt);
        return -1;
    }
    /*
     * Store both directions of the mount relationship:
     * mnt->mountpoint identifies where this filesystem is attached.
     * mount_point->mount identifies what is mounted on this vnode.
     */
    mnt->mountpoint = mount_point;
    mount_point->mount = mnt;
    return 0;
}
/*
 * Change the current process's working directory.
 */
int vfs_chdir(const char* pathname) {
    struct task_struct* current = get_current();
    struct vnode* target = 0;
    if (current == 0) return -1;
    if (vfs_lookup(pathname, &target) != 0) return -1;
    if (target->type != VNODE_DIR) return -1;
    current->cwd = target;
    return 0;
}
/*
 * Change the read/write position of an opened file.
 * The VFS forwards this operation to the implementation associated
 * with the opened file. For this lab, /dev/fb supports SEEK_SET.
 */
long vfs_lseek64(struct file* file, long offset, int whence) {
    if (file == 0 || file->f_ops == 0 || file->f_ops->lseek64 == 0) return -1;
    return file->f_ops->lseek64(file, offset, whence);
}
/*
 * Perform a device-specific operation on an opened file.
 * The meaning of request and arg is determined by the underlying device.
 * For /dev/fb, request FB_IOCTL_GET_INFO returns framebuffer information.
 */
int vfs_ioctl(struct file* file, unsigned long request, void* arg) {
    if (file == 0 || file->f_ops == 0 || file->f_ops->ioctl == 0) return -1;
    return file->f_ops->ioctl(file, request, arg);
}