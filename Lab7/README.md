# NYCU OSC Lab 7 - Virtual File System

This repository contains my implementation for NYCU Operating System Capstone Lab 7.

The goal of this lab is to add a Virtual File System layer to the kernel. The VFS provides a unified interface for different filesystems and device files.

This lab implements:

- VFS abstraction and filesystem registration
- tmpfs as the root filesystem
- pathname lookup and mount traversal
- per-process current working directory
- per-process file descriptor table
- read-only ramfs backed by initramfs CPIO
- `/dev/uart` as the console device
- `/dev/fb` as a write-only framebuffer device
- `lseek64()` and `ioctl()`
- userspace/kernel data transfer
- file descriptor inheritance across `fork()`

---

## Overview

The implemented exercises are:

- Basic Exercise 1: Root File System
- Basic Exercise 2: Multi-level VFS
- Basic Exercise 3: Multitask VFS
- Basic Exercise 4: `/ramfs`
- Advanced Exercise 1: `/dev/uart`
- Advanced Exercise 2: `/dev/fb`

The kernel continues to support the virtual memory, demand paging, mmap, Copy-on-Write, process management, and signal handling features from previous labs.

---

## Project Structure

| File | Description |
|---|---|
| `kernel/src/vfs.c` | Filesystem registration, pathname lookup, mount, open, read, write, mkdir, chdir, lseek, and ioctl |
| `kernel/inc/vfs.h` | VFS structures, operation tables, flags, and function declarations |
| `kernel/src/tmpfs.c` | Writable memory-based root filesystem |
| `kernel/src/ramfs.c` | Read-only filesystem backed by initramfs CPIO |
| `kernel/src/devfs.c` | Device filesystem containing UART and framebuffer files |
| `kernel/src/syscall.c` | VFS system calls and user/kernel buffer handling |
| `kernel/src/uaccess.c` | `copy_from_user()`, `copy_to_user()`, and string copying |
| `kernel/src/thread.c` | Per-process VFS state, fd inheritance, stdio initialization, and cleanup |
| `kernel/src/video.c` | Framebuffer writing, information query, and cache flushing |
| `kernel/src/main_kernel.c` | VFS initialization and mounting `/ramfs` and `/dev` |
| `kernel/src/test.c` | Kernel-side VFS tests |

---

## VFS Architecture

The main VFS structures are:

```c
struct vnode;
struct file;
struct mount;
struct filesystem;
struct vnode_operations;
struct file_operations;
```

Their relationships are:

```text
Process fd table
      |
      v
struct file
      |
      v
struct vnode
      |
      +---- vnode_operations
      |
      +---- file_operations
      |
      +---- filesystem-specific internal data
```

A `vnode` represents a filesystem object, while a `file` represents one opened file handle.

A file handle stores:

- the opened vnode
- the current read/write position `f_pos`
- open flags
- filesystem or device operations
- a reference count

---

## Core Structures

### vnode

```c
struct vnode {
    struct mount* mount;
    struct mount* owner_mount;
    struct vnode* parent;
    enum vnode_type type;
    struct vnode_operations* v_ops;
    struct file_operations* f_ops;
    void* internal;
};
```

Important fields:

| Field | Description |
|---|---|
| `mount` | Filesystem mounted on this vnode |
| `owner_mount` | Mounted filesystem instance that owns this vnode |
| `parent` | Parent vnode used to resolve `..` |
| `type` | Directory or file |
| `v_ops` | Path lookup and creation operations |
| `f_ops` | Operations available after opening |
| `internal` | Filesystem-specific inode data |

### file

```c
struct file {
    struct vnode* vnode;
    size_t f_pos;
    struct file_operations* f_ops;
    int flags;
    int ref_count;
};
```

`f_pos` stores the position of the next read or write. Multiple file descriptors may share the same file handle after `fork()`.

### mount

```c
struct mount {
    struct vnode* root;
    struct vnode* mountpoint;
    struct filesystem* fs;
};
```

`root` is the root vnode of the mounted filesystem. `mountpoint` records the directory in the parent filesystem where it is attached.

---

## Filesystem Registration

The VFS maintains a filesystem registration table.

```c
int register_filesystem(struct filesystem* fs);
```

The registered filesystems are:

| Filesystem | Purpose |
|---|---|
| `tmpfs` | Writable root filesystem |
| `ramfs` | Read-only view of initramfs |
| `devfs` | Device files `/dev/uart` and `/dev/fb` |

During initialization, tmpfs is mounted directly as the root filesystem.

```text
/
|-- ramfs
`-- dev
    |-- uart
    `-- fb
```

---

## Basic Exercise 1 - Root File System

tmpfs is mounted as the root filesystem.

It supports:

- regular files
- directories
- file creation with `O_CREAT`
- open and close
- read and write
- pathname lookup

The tmpfs limits follow the lab specification:

```c
#define TMPFS_MAX_FILE_NAME 16
#define TMPFS_MAX_DIR_ENTRY 16
#define TMPFS_MAX_FILE_SIZE 4096
```

File data is allocated on the first write. Reads and writes begin at `file->f_pos`, which is updated after each successful operation.

### tmpfs read flow

```text
vfs_read()
    -> file->f_ops->read()
    -> tmpfs_read()
    -> copy inode data from f_pos
    -> advance f_pos
```

### tmpfs write flow

```text
vfs_write()
    -> file->f_ops->write()
    -> tmpfs_write()
    -> allocate data buffer on first write
    -> copy data at f_pos
    -> update f_pos and file size
```

---

## Basic Exercise 2 - Multi-level VFS

The VFS supports:

- creating subdirectories
- mounting filesystems on directories
- multi-component pathname traversal
- crossing mount points
- mount isolation

When pathname lookup reaches a mounted vnode, it follows:

```text
mount point vnode
      |
      v
mounted filesystem root vnode
```

`vfs_mount()` verifies that:

- the filesystem is registered
- the target exists
- the target is a directory
- the target is not already mounted

The mount relation is stored in both directions:

```text
mountpoint vnode --mount--> struct mount
struct mount --mountpoint--> mountpoint vnode
struct mount --root--> mounted root vnode
```

---

## Pathname Lookup

`vfs_lookup()` supports:

- absolute paths
- relative paths
- repeated `/`
- empty paths
- `.`
- `..`
- current working directory
- process root directory
- entering and leaving mounted filesystems

Absolute paths begin at:

```c
current->root
```

Relative paths begin at:

```c
current->cwd
```

When resolving `..` at the process root, lookup remains at the root.

When resolving `..` from the root of a mounted filesystem, the VFS returns to the parent of its mount point:

```text
mounted root
    -> owner_mount
    -> mountpoint
    -> mountpoint->parent
```

---

## Basic Exercise 3 - Multitask VFS

Each task contains:

```c
struct vnode* root;
struct vnode* cwd;
struct file* fd_table[MAX_FD];
```

The maximum number of file descriptors is:

```c
#define MAX_FD 16
```

`open()` allocates the lowest unused fd. `close()` clears the fd slot and decreases the file handle reference count.

The implemented VFS syscalls are:

| Syscall | Number | Description |
|---|---:|---|
| `open` | 14 | Open or create a file |
| `close` | 15 | Close a file descriptor |
| `read` | 16 | Read data into a user buffer |
| `write` | 17 | Write data from a user buffer |
| `mkdir` | 18 | Create a directory |
| `mount` | 19 | Mount a registered filesystem |
| `chdir` | 20 | Change the current working directory |
| `lseek64` | 21 | Change the file position |
| `ioctl` | 22 | Perform a device-specific request |

---

## open and close

### open

```text
User pathname
    -> copy_string_from_user()
    -> vfs_open()
    -> vfs_lookup()
    -> optional create() when O_CREAT is set
    -> allocate struct file
    -> allocate fd
```

### close

```text
fd
    -> find struct file in fd_table
    -> clear fd_table entry
    -> vfs_close()
    -> decrease ref_count
    -> release file handle when ref_count reaches zero
```

---

## read and write

VFS and device drivers receive kernel buffers rather than raw user pointers.

### read

```text
filesystem or device
    -> vfs_read()
    -> kernel buffer
    -> copy_to_user()
    -> user buffer
```

### write

```text
user buffer
    -> copy_from_user()
    -> kernel buffer
    -> vfs_write()
    -> filesystem or device
```

Large operations are split into chunks using:

```c
#define UACCESS_BUFFER_SIZE 512
```

---

## File Descriptors and fork

During `fork()`, the child inherits:

- the parent root directory
- the parent current working directory
- all opened file descriptors

Parent and child share the same `struct file` objects:

```c
child->fd_table[i] = parent->fd_table[i];
vfs_dup(child->fd_table[i]);
```

Therefore, parent and child also share the same `f_pos`.

`ref_count` prevents a shared file handle from being released until every reference has been closed.

All opened file descriptors are closed during:

- `process_exit()`
- `process_stop()`
- `process_waitpid()` cleanup
- orphan zombie cleanup
- fork failure cleanup

---

## Basic Exercise 4 - /ramfs

ramfs is mounted read-only at:

```text
/ramfs
```

During mount setup, it parses the initramfs CPIO archive and creates one vnode for every archive file.

The complete file content is not copied. Each ramfs inode records:

```c
const char* data;
size_t size;
```

`data` points directly to the original file content inside the CPIO archive.

The following operations fail because ramfs is read-only:

- write
- create
- mkdir

The current initramfs contains flat filenames:

```text
osctest.bin
vfs.bin
```

Therefore, nested CPIO pathname construction is not required by the current archive.

---

## Device Filesystem

This implementation uses the alternative devfs approach allowed by the lab specification.

devfs is mounted at:

```text
/dev
```

It creates two predefined device files:

```text
/dev/uart
/dev/fb
```

Device-specific behavior is provided through each vnode's `file_operations`. The VFS itself does not contain hardcoded UART or framebuffer behavior.

---

## Advanced Exercise 1 - /dev/uart

`/dev/uart` provides the console through standard VFS read and write operations.

```text
read(fd, buffer, count)
    -> vfs_read()
    -> uart_device_read()
    -> uart_getc()

write(fd, buffer, count)
    -> vfs_write()
    -> uart_device_write()
    -> uart_putc()
```

Every new user process opens `/dev/uart` as:

| fd | Purpose |
|---:|---|
| 0 | stdin |
| 1 | stdout |
| 2 | stderr |

For example:

```c
write(1, "hello world\n", 12);
```

This writes to the UART terminal through `/dev/uart`.

---

## Advanced Exercise 2 - /dev/fb

`/dev/fb` is implemented as a write-only framebuffer device.

Writing to the file copies data to:

```text
framebuffer base + file->f_pos
```

After a successful write, `f_pos` advances by the number of bytes written.

The framebuffer driver flushes the corresponding data-cache blocks so the display hardware can observe the new pixel data.

The framebuffer size is:

```c
FB_WIDTH * FB_HEIGHT * FB_BPP
```

---

## lseek64

Only `SEEK_SET` is supported:

```c
#define SEEK_SET 0

long lseek64(int fd, long offset, int whence);
```

For `/dev/fb`, it sets:

```c
file->f_pos = offset;
```

The offset must remain within the framebuffer range.

This allows a user process to repeatedly update different pixels without reopening the framebuffer device.

---

## ioctl

The framebuffer supports:

```c
#define FB_IOCTL_GET_INFO 0
```

The returned structure is:

```c
struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;
};
```

Example:

```c
struct framebuffer_info fb;

int fd = open("/dev/fb", 0);
ioctl(fd, FB_IOCTL_GET_INFO, &fb);
```

The request flow is:

```text
sys_ioctl()
    -> vfs_ioctl()
    -> framebuffer_device_ioctl()
    -> video_framebuffer_get_info()
    -> kernel framebuffer_info
    -> copy_to_user()
    -> user framebuffer_info
```

---

## Userspace Pointer Access

System call arguments may contain userspace pointers. The kernel does not pass these pointers directly to VFS or device drivers.

The uaccess helpers are:

```c
copy_from_user();
copy_to_user();
copy_string_from_user();
```

Their directions are:

| Function | Direction | Usage |
|---|---|---|
| `copy_from_user()` | User to Kernel | write buffers, image data |
| `copy_to_user()` | Kernel to User | read buffers, ioctl results |
| `copy_string_from_user()` | User string to Kernel | paths and executable names |

During a user-memory copy, the kernel:

1. saves the original `sstatus`
2. disables supervisor interrupts
3. temporarily enables `SUM`
4. accesses the user virtual address
5. restores the original `SUM` state
6. restores the original interrupt state

If a legal user page has not been mapped yet, accessing it while SUM is enabled causes a page fault. The existing demand-paging handler creates the page and resumes the copy.

---

## Kernel Initialization

The VFS initialization sequence is:

```c
vfs_init();

vfs_mkdir("/ramfs");
vfs_mount("/ramfs", "ramfs");

vfs_mkdir("/dev");
vfs_mount("/dev", "devfs");
```

This produces:

- a writable tmpfs root
- a read-only initramfs view
- device files accessible through normal VFS APIs

---

## Build

Build the Orange Pi RV2 kernel:

```bash
make build-opi-kernel
```

Build the Orange Pi RV2 bootloader:

```bash
make build-opi-boot
```

Run the QEMU configuration:

```bash
make run-qemu
```

---

## Test

The initramfs contains:

```text
osctest.bin
vfs.bin
```

Run the Lab 7 user test program:

```text
opi-rv2> exec vfs.bin
```

Inside the user program:

```text
$ vfs
$ vfs_fork
```

`vfs` tests:

- open, read, and write
- mkdir and mount
- relative and absolute lookup
- ramfs read-only behavior
- stdin, stdout, and stderr

`vfs_fork` tests:

- `/dev/fb`
- framebuffer information through `ioctl()`
- framebuffer position through `lseek64()`
- video output through VFS writes
- fd inheritance across `fork()`

---

## Test Result

```text
Basic Exercise 1
open/read/write: [PASS]

Basic Exercise 2
mkdir: [PASS]
mount: [PASS]

Basic Exercise 3
lookup-1: [PASS]
lookup-2: [PASS]

Basic Exercise 4
ramfs: [PASS]
create on ramfs: [PASS]
write on ramfs: [PASS]
mkdir on ramfs: [PASS]

Advanced Exercise 1
stdin: [PASS]
stdout: [PASS]
stderr: [PASS]

Advanced Exercise 2
/dev/fb: [PASS]
lseek64: [PASS]
ioctl: [PASS]
vfs_fork video output: [PASS]
```

---

## Summary

This lab implementation completes:

- generic VFS interfaces
- filesystem registration
- tmpfs root filesystem
- regular file and directory creation
- file handles and `f_pos`
- absolute and relative pathname lookup
- `.`, `..`, and repeated slash handling
- mount traversal and mount isolation
- per-process root and working directory
- reusable per-process file descriptors
- VFS system calls 14 through 22
- read-only ramfs backed by CPIO
- fixed devfs mounted at `/dev`
- `/dev/uart` console device
- stdin, stdout, and stderr initialization
- `/dev/fb` framebuffer device
- framebuffer `lseek64()` and `ioctl()`
- userspace pointer copying with temporary SUM
- fd reference counting and fork inheritance
- automatic fd cleanup during process termination

Overall, the kernel now provides a unified file interface for memory filesystems, initramfs files, UART, and framebuffer devices.

---

## Notes and Design Choices

- tmpfs is the root filesystem.
- ramfs is read-only and points directly into the CPIO archive.
- The current CPIO archive contains only flat filenames.
- devfs contains predefined `uart` and `fb` device files.
- Device behavior is selected through `file_operations`.
- `/dev/fb` is write-only.
- `lseek64()` currently supports only `SEEK_SET`.
- `ioctl()` currently supports only `FB_IOCTL_GET_INFO`.
- Each process has at most 16 file descriptors.
- Parent and child share open file handles and `f_pos` after `fork()`.
- File handles are reference-counted.
- VFS and device drivers operate on kernel buffers.
- User pointers are accessed only through the uaccess helpers.
- SUM is enabled only during controlled userspace memory access.
