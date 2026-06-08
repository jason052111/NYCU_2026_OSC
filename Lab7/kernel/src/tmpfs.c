#include "tmpfs.h"
#include "buddy.h"
#include "tool.h"

#define TMPFS_MAX_FILE_NAME 16
#define TMPFS_MAX_DIR_ENTRY 16
#define TMPFS_MAX_FILE_SIZE 4096

enum fsnode_type { FS_DIR, FS_FILE };

struct tmpfs_vnode {
    enum fsnode_type type;
    char name[TMPFS_MAX_FILE_NAME];
    struct vnode* entry[TMPFS_MAX_DIR_ENTRY];
    char* data;
    size_t size;
};

static int tmpfs_open(struct vnode* file_node, struct file** target);
static int tmpfs_close(struct file* file);
static int tmpfs_read(struct file* file, void* buf, size_t len);
static int tmpfs_write(struct file* file, const void* buf, size_t len);
static int tmpfs_lookup(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name);
static int tmpfs_create(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name);
static int tmpfs_mkdir(struct vnode* dir_node,
                       struct vnode** target,
                       const char* component_name);

static struct file_operations tmpfs_file_ops = {
    .open = tmpfs_open,
    .close = tmpfs_close,
    .read = tmpfs_read,
    .write = tmpfs_write,
};

static struct vnode_operations tmpfs_vnode_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
};

static void tmpfs_copy_name(char* dst, const char* src)
{
    int i = 0;

    while (src[i] != '\0' && i < TMPFS_MAX_FILE_NAME - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static struct vnode* tmpfs_create_vnode(enum fsnode_type type)
{
    struct vnode* vnode = (struct vnode*)allocate(sizeof(struct vnode));
    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)allocate(sizeof(struct tmpfs_vnode));

    if (vnode == 0 || inode == 0) {
        if (vnode != 0) free(vnode);
        if (inode != 0) free(inode);
        return 0;
    }

    mem_zero(vnode, sizeof(struct vnode));
    mem_zero(inode, sizeof(struct tmpfs_vnode));

    vnode->mount = 0;
    vnode->owner_mount = 0;
    vnode->parent = 0;
    if (type == FS_DIR) {
        vnode->type = VNODE_DIR;
    } else {
        vnode->type = VNODE_FILE;
    }
    vnode->v_ops = &tmpfs_vnode_ops;
    vnode->f_ops = &tmpfs_file_ops;
    vnode->internal = inode;

    inode->type = type;
    inode->data = 0;
    inode->size = 0;

    return vnode;
}

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mnt)
{
    if (mnt == 0) return -1;
    mnt->root = tmpfs_create_vnode(FS_DIR);
    if (mnt->root == 0) return -1;
    mnt->root->parent = mnt->root;
    mnt->root->owner_mount = mnt;
    mnt->fs = fs;
    return 0;
}

static int tmpfs_open(struct vnode* file_node, struct file** target)
{
    if (file_node == 0 || target == 0 || *target == 0) return -1;
    (*target)->vnode = file_node;
    (*target)->f_ops = &tmpfs_file_ops;
    (*target)->f_pos = 0;
    return 0;
}

static int tmpfs_close(struct file* file)
{
    if (file == 0) return -1;
    free(file);
    return 0;
}

static int tmpfs_read(struct file* file, void* buf, size_t len)
{
    if (file == 0 || buf == 0) return -1;

    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)file->vnode->internal;

    if (inode == 0 || inode->type != FS_FILE) return -1;

    if (inode->data == 0 || file->f_pos >= inode->size) return 0;

    size_t readable = inode->size - file->f_pos;

    if (len > readable) len = readable;

    mem_copy(buf, inode->data + file->f_pos, len);
    file->f_pos += len;

    return (int)len;
}

static int tmpfs_write(struct file* file, const void* buf, size_t len)
{
    if (file == 0 || buf == 0) return -1;

    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)file->vnode->internal;

    if (inode == 0 || inode->type != FS_FILE) return -1;

    if (file->f_pos + len > TMPFS_MAX_FILE_SIZE) return -1;

    if (inode->data == 0) {
        inode->data = (char*)allocate(TMPFS_MAX_FILE_SIZE);
        if (inode->data == 0) return -1;
        mem_zero(inode->data, TMPFS_MAX_FILE_SIZE);
    }

    mem_copy(inode->data + file->f_pos, buf, len);
    file->f_pos += len;

    if (file->f_pos > inode->size) {
        inode->size = file->f_pos;
    }

    return (int)len;
}

static int tmpfs_lookup(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name)
{
    if (dir_node == 0 || target == 0 || component_name == 0) return -1;

    struct tmpfs_vnode* dir_inode = (struct tmpfs_vnode*)dir_node->internal;

    if (dir_inode == 0 || dir_inode->type != FS_DIR) return -1;

    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_inode->entry[i] == 0) continue;

        struct tmpfs_vnode* inode = (struct tmpfs_vnode*)dir_inode->entry[i]->internal;

        if (inode != 0 && strcmp(inode->name, component_name) == 0) {
            *target = dir_inode->entry[i];
            return 0;
        }
    }

    return -1;
}

static int tmpfs_create(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name)
{
    if (dir_node == 0 || target == 0 || component_name == 0) return -1;

    if (component_name[0] == '\0') return -1;

    struct tmpfs_vnode* dir_inode = (struct tmpfs_vnode*)dir_node->internal;

    if (dir_inode == 0 || dir_inode->type != FS_DIR) return -1;

    struct vnode* existing = 0;

    if (tmpfs_lookup(dir_node, &existing, component_name) == 0) return -1;

    int free_slot = -1;

    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_inode->entry[i] == 0) {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0) return -1;

    struct vnode* new_vnode = tmpfs_create_vnode(FS_FILE);

    if (new_vnode == 0) return -1;

    struct tmpfs_vnode* new_inode = (struct tmpfs_vnode*)new_vnode->internal;

    tmpfs_copy_name(new_inode->name, component_name);

    new_vnode->parent = dir_node;
    new_vnode->owner_mount = dir_node->owner_mount;
    dir_inode->entry[free_slot] = new_vnode;
    *target = new_vnode;

    return 0;
}

static int tmpfs_mkdir(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name)
{
    if (dir_node == 0 || target == 0 || component_name == 0) return -1;

    if (component_name[0] == '\0') return -1;

    struct tmpfs_vnode* dir_inode = (struct tmpfs_vnode*)dir_node->internal;

    if (dir_inode == 0 || dir_inode->type != FS_DIR) return -1;

    struct vnode* existing = 0;

    if (tmpfs_lookup(dir_node, &existing, component_name) == 0) return -1;

    int free_slot = -1;

    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_inode->entry[i] == 0) {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0) return -1;

    struct vnode* new_vnode = tmpfs_create_vnode(FS_DIR);

    if (new_vnode == 0) return -1;

    struct tmpfs_vnode* new_inode = (struct tmpfs_vnode*)new_vnode->internal;

    tmpfs_copy_name(new_inode->name, component_name);

    new_vnode->parent = dir_node;
    new_vnode->owner_mount = dir_node->owner_mount;
    dir_inode->entry[free_slot] = new_vnode;
    *target = new_vnode;

    return 0;
}

