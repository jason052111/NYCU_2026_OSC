#include "ramfs.h"
#include "initrd.h"
#include "buddy.h"
#include "tool.h"

#define RAMFS_MAX_NAME  16
#define RAMFS_MAX_ENTRY 64

struct ramfs_vnode {
    char name[RAMFS_MAX_NAME];
    struct vnode* entry[RAMFS_MAX_ENTRY];
    const char* data;
    size_t size;
};

static int ramfs_open(struct vnode* file_node, struct file** target);
static int ramfs_close(struct file* file);
static int ramfs_read(struct file* file, void* buf, size_t len);
static int ramfs_write(struct file* file, const void* buf, size_t len);
static int ramfs_lookup(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name);
static int ramfs_create(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name);
static int ramfs_mkdir(struct vnode* dir_node,
                       struct vnode** target,
                       const char* component_name);

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

static void ramfs_copy_name(char* dst, const char* src)
{
    int i = 0;

    while (src[i] != '\0' && i < RAMFS_MAX_NAME - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static struct vnode* ramfs_create_vnode(enum vnode_type type)
{
    struct vnode* vnode = (struct vnode*)allocate(sizeof(struct vnode));
    struct ramfs_vnode* inode =
        (struct ramfs_vnode*)allocate(sizeof(struct ramfs_vnode));

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

int ramfs_setup_mount(struct filesystem* fs, struct mount* mnt)
{
    struct vnode* root = ramfs_create_vnode(VNODE_DIR);

    if (root == 0) return -1;

    root->parent = root;
    root->owner_mount = mnt;

    mnt->root = root;
    mnt->fs = fs;

    struct ramfs_vnode* root_inode =
        (struct ramfs_vnode*)root->internal;

    const char* ptr = (const char*)INITRD_BASE;
    int entry_index = 0;

    while (entry_index < RAMFS_MAX_ENTRY) {
        struct cpio_t* header = (struct cpio_t*)ptr;

        if (strncmp(header->magic, "070701", 6) != 0) {
            break;
        }

        int namesize = hextoi(header->namesize, 8);
        int filesize = hextoi(header->filesize, 8);
        const char* filename = ptr + sizeof(struct cpio_t);

        if (strcmp(filename, "TRAILER!!!") == 0) {
            break;
        }

        const char* filedata =
            (const char*)align_up(filename + namesize, 4);

        struct vnode* file_node = ramfs_create_vnode(VNODE_FILE);

        if (file_node == 0) return -1;

        struct ramfs_vnode* file_inode =
            (struct ramfs_vnode*)file_node->internal;

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

static int ramfs_open(struct vnode* file_node, struct file** target)
{
    (*target)->vnode = file_node;
    (*target)->f_ops = &ramfs_file_ops;
    (*target)->f_pos = 0;
    return 0;
}

static int ramfs_close(struct file* file)
{
    free(file);
    return 0;
}

static int ramfs_read(struct file* file, void* buf, size_t len)
{
    struct ramfs_vnode* inode =
        (struct ramfs_vnode*)file->vnode->internal;

    if (file->vnode->type != VNODE_FILE) return -1;

    if (file->f_pos >= inode->size) return 0;

    size_t readable = inode->size - file->f_pos;
    if (len > readable) len = readable;

    mem_copy(buf, inode->data + file->f_pos, len);
    file->f_pos += len;

    return (int)len;
}

static int ramfs_write(struct file* file, const void* buf, size_t len)
{
    (void)file;
    (void)buf;
    (void)len;
    return -1;
}

static int ramfs_lookup(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name)
{
    struct ramfs_vnode* dir =
        (struct ramfs_vnode*)dir_node->internal;

    if (dir_node->type != VNODE_DIR) return -1;

    for (int i = 0; i < RAMFS_MAX_ENTRY; i++) {
        if (dir->entry[i] == 0) continue;

        struct ramfs_vnode* inode =
            (struct ramfs_vnode*)dir->entry[i]->internal;

        if (strcmp(inode->name, component_name) == 0) {
            *target = dir->entry[i];
            return 0;
        }
    }

    return -1;
}

static int ramfs_create(struct vnode* dir_node,
                        struct vnode** target,
                        const char* component_name)
{
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

static int ramfs_mkdir(struct vnode* dir_node,
                       struct vnode** target,
                       const char* component_name)
{
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}
