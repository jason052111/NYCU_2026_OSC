#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mnt);

#endif