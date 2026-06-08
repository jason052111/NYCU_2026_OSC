#include "vfs.h"
#include "tmpfs.h"
#include "buddy.h"
#include "tool.h"
#include "thread.h"
#include "ramfs.h"

#define MAX_FS   16
#define PATH_MAX 255

struct mount* rootfs = 0;
static struct filesystem fs_list[MAX_FS];

static int split_parent_child(const char* pathname,
                              char* parent,
                              const char** child)
{
    int len = strlen(pathname);
    int last_slash = -1;

    /*
    int end = len;

    while (end > 0 && pathname[end - 1] == '/') {
        end--;
    }

    if (end == 0) return -1;

    for (int i = 0; i < end; i++) {
        if (pathname[i] == '/') {
            last_slash = i;
        }
    }
    */

    if (len == 0 || pathname[len - 1] == '/') return -1;

    for (int i = 0; i < len; i++) {
        if (pathname[i] == '/') {
            last_slash = i;
        }
    }

    if (last_slash < 0) {
        parent[0] = '\0';
        *child = pathname;
        return 0;
    }

    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        for (int i = 0; i < last_slash; i++) {
            parent[i] = pathname[i];
        }

        parent[last_slash] = '\0';
    }

    *child = pathname + last_slash + 1;
    return 0;
}

static struct filesystem* find_filesystem(const char* name)
{
    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name != 0 && strcmp(fs_list[i].name, name) == 0) {
            return &fs_list[i];
        }
    }

    return 0;
}

static struct vnode* follow_mount(struct vnode* node)
{
    while (node->mount != 0) {
        node = node->mount->root;
    }

    return node;
}

static struct vnode* lookup_parent(struct vnode* node,
                                   struct vnode* process_root)
{
    if (node == process_root) {
        return node;
    }

    if (node->owner_mount != 0 &&
        node == node->owner_mount->root &&
        node->owner_mount->mountpoint != 0) {
        return node->owner_mount->mountpoint->parent;
    }

    return node->parent;
}

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
    return 0;
}

int register_filesystem(struct filesystem* fs)
{
    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name == 0) {
            fs_list[i].name = fs->name;
            fs_list[i].setup_mount = fs->setup_mount;
            return i;
        }
    }
    return -1;
}

int vfs_open(const char* pathname, int flags, struct file** target)
{
    struct vnode* vnode = 0;
    int ret = vfs_lookup(pathname, &vnode);

    if (ret != 0) {
        if (!(flags & O_CREAT)) return -1;

        char parent_path[PATH_MAX];
        const char* filename = 0;

        mem_zero(parent_path, PATH_MAX);

        if (split_parent_child(pathname, parent_path, &filename) != 0) return -1;

        if (vfs_lookup(parent_path, &vnode) != 0) return -1;

        if (vnode->v_ops->create(vnode, &vnode, filename) != 0) return -1;
    }

    *target = (struct file*)allocate(sizeof(struct file));

    if (*target == 0) return -1;

    mem_zero(*target, sizeof(struct file));
    (*target)->flags = flags;

    ret = vnode->f_ops->open(vnode, target);

    if (ret != 0) {
        free(*target);
        *target = 0;
        return -1;
    }

    return 0;
}

int vfs_close(struct file* file)
{
    return file->f_ops->close(file);
}

int vfs_read(struct file* file, void* buf, size_t len)
{
    return file->f_ops->read(file, buf, len);
}

int vfs_write(struct file* file, const void* buf, size_t len)
{
    return file->f_ops->write(file, buf, len);
}

int vfs_lookup(const char* pathname, struct vnode** target)
{
    struct task_struct* current = get_current();
    struct vnode* process_root = rootfs->root;
    struct vnode* node;
    int len = strlen(pathname);
    int i = 0;

    if (current != 0 && current->root != 0) {
        process_root = current->root;
    }

    if (pathname[0] == '/') {
        node = process_root;
    } else if (current != 0 && current->cwd != 0) {
        node = current->cwd;
    } else { // fallback 
        node = process_root;
    }

    if (len == 0) {
        *target = node;
        return 0;
    }

    while (i < len) {
        while (i < len && pathname[i] == '/') {
            i++;
        }

        if (i >= len) break;

        char component[PATH_MAX];
        int component_len = 0;
        mem_zero(component, PATH_MAX);

        while (i < len && pathname[i] != '/') {
            if (component_len >= PATH_MAX - 1) return -1;
            component[component_len++] = pathname[i++];
        }

        component[component_len] = '\0';

        if (strcmp(component, ".") == 0) {
            continue;
        }

        if (strcmp(component, "..") == 0) {
            node = lookup_parent(node, process_root);
            continue;
        }

        if (node->v_ops->lookup(node, &node, component) != 0) {
            return -1;
        }

        node = follow_mount(node);
    }

    *target = node;
    return 0;
}

int vfs_mkdir(const char* pathname)
{
    struct vnode* dir_node = 0;
    struct vnode* target = 0;
    char parent_path[PATH_MAX];
    const char* dirname = 0;

    mem_zero(parent_path, PATH_MAX);

    if (split_parent_child(pathname, parent_path, &dirname) != 0) return -1;

    if (vfs_lookup(parent_path, &dir_node) != 0) return -1;

    return dir_node->v_ops->mkdir(dir_node, &target, dirname);
}

int vfs_mount(const char* target, const char* filesystem)
{
    struct vnode* mount_point = 0;
    struct filesystem* fs = find_filesystem(filesystem);

    if (fs == 0) return -1;

    if (vfs_lookup(target, &mount_point) != 0) return -1;

    struct mount* mnt = (struct mount*)allocate(sizeof(struct mount));

    if (mnt == 0) return -1;

    mem_zero(mnt, sizeof(struct mount));

    if (fs->setup_mount(fs, mnt) != 0) {
        free(mnt);
        return -1;
    }

    mnt->mountpoint = mount_point;
    mount_point->mount = mnt;
    return 0;
}

int vfs_chdir(const char* pathname)
{
    struct task_struct* current = get_current();
    struct vnode* target = 0;

    if (current == 0) return -1;

    if (vfs_lookup(pathname, &target) != 0) return -1;

    if (target->type != VNODE_DIR) return -1;

    current->cwd = target;
    return 0;
}