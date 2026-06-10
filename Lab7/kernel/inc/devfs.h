#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

int devfs_setup_mount(struct filesystem* fs, struct mount* mnt);

#endif