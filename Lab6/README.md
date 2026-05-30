# NYCU 2025 OS Lab 6 - Virtual Memory, mmap, Demand Paging, and Copy-on-Write

This repository contains my implementation for NYCU 2025 Operating System Lab 6.

The goal of this lab is to extend the previous multitasking kernel with virtual memory support.  
Each user process now runs in its own isolated virtual address space using RISC-V Sv39 page tables.

This lab also implements:

- per-process page tables
- virtual memory based user process layout
- page fault handling
- demand paging
- anonymous mmap
- fork / exec with isolated address spaces
- copy-on-write fork
- page reference counting
- signal handling with user-mapped signal stack

The kernel runs in S-mode on RISC-V and executes user programs in U-mode.

---

## Overview

In this lab, I implemented the following parts:

- Basic Exercise: RISC-V Sv39 virtual memory
- Revisit Syscalls: fork() and exec() with isolated virtual address spaces
- Advanced Exercise 1: mmap()
- Advanced Exercise 2: Page Fault Handler and Demand Paging
- Advanced Exercise 3: Copy-on-Write

The kernel also includes the Lab 5 features:

- kernel threads
- context switching
- user processes
- system calls
- fork / exec / waitpid / exit
- video display
- process sleeping
- POSIX-like signal handling

The main difference from Lab 5 is that user programs no longer directly use physical addresses.  
Instead, every user process has its own page table, and the same user virtual addresses can be used by different processes while mapping to different physical pages.

---

## Project Structure

The kernel source code is organized into `src/` and `inc/`:

```text
kernel/
├── src/
│   ├── start.S
│   ├── main_kernel.c
│   ├── thread.c
│   ├── trap.c
│   ├── syscall.c
│   ├── uart.c
│   ├── video.c
│   ├── vm.c
│   ├── sbi.c
│   ├── buddy.c
│   ├── fdt.c
│   ├── initrd.c
│   ├── cmd_kernel.c
│   ├── tool.c
│   ├── test.c
│   └── link_kernel.ld
└── inc/
    ├── config.h
    ├── thread.h
    ├── trap.h
    ├── syscall.h
    ├── uart.h
    ├── video.h
    ├── vm.h
    ├── sbi.h
    ├── buddy.h
    ├── fdt.h
    ├── initrd.h
    ├── cmd_kernel.h
    ├── tool.h
    └── bird.h
```

| File | Description |
|---|---|
| `src/start.S` | Low-level boot code, trap entry, context save/restore, and `switch_to` |
| `src/main_kernel.c` | Kernel entry, initialization, and shell thread creation |
| `src/thread.c` | Thread/process management, scheduler, fork, exec, waitpid, exit, mmap, COW setup, and signal handling |
| `inc/thread.h` | `task_struct`, `thread_struct`, task states, and function declarations |
| `src/trap.c` | Trap/interrupt handling, page fault handling, demand paging, and COW fault handling |
| `inc/trap.h` | `trap_frame`, timer event, task queue structures, and trap-related declarations |
| `src/syscall.c` | System call dispatcher for user programs |
| `inc/syscall.h` | System call numbers and syscall handler declaration |
| `src/vm.c` | Sv39 page table setup, page mapping, page lookup, page reference count, and VM switching |
| `inc/vm.h` | Virtual memory constants, PTE flags, VMA structure, and VM function declarations |
| `src/uart.c` | UART driver, UART ring buffer, and PLIC setup |
| `inc/uart.h` | UART/PLIC definitions and UART function declarations |
| `src/video.c` | Orange Pi RV2 framebuffer output |
| `inc/video.h` | Framebuffer constants and video display declaration |
| `src/sbi.c` | SBI calls, especially timer setup |
| `inc/sbi.h` | SBI constants, structures, and function declarations |
| `src/buddy.c` | Buddy allocator, small chunk allocator, and reserved memory handling |
| `inc/buddy.h` | Page allocator structures, constants, and declarations |
| `src/fdt.c` | Device Tree parsing helpers |
| `inc/fdt.h` | FDT structures, constants, and function declarations |
| `src/initrd.c` | Initramfs parsing and user program loading |
| `inc/initrd.h` | CPIO header and initrd function declarations |
| `src/cmd_kernel.c` | Kernel shell commands |
| `inc/cmd_kernel.h` | Kernel shell function declarations |
| `src/tool.c` | String, memory, and utility functions |
| `inc/tool.h` | Utility function declarations |
| `inc/config.h` | Global build-time configuration such as `OPIORQEMU` |
| `inc/bird.h` | Video frame data used by the test program |
| `src/link_kernel.ld` | Kernel linker script |

---

## Build System Update

Because the kernel files are separated into `src/` and `inc/`, the Makefile compiles kernel sources using:

```makefile
-Iinc
src/*.c src/*.S
-T src/link_kernel.ld
```

For example:

```makefile
cd kernel && $(CC) $(CFLAGS) -Iinc -c src/*.c src/*.S -D OPIORQEMU=1
cd kernel && $(LD) -T src/link_kernel.ld -o $(KERNEL_TARGET).elf *.o
```

This allows source files to include headers normally:

```c
#include "thread.h"
#include "trap.h"
#include "uart.h"
#include "vm.h"
```

---

## Virtual Memory Overview

Lab 6 enables RISC-V Sv39 virtual memory.

The kernel uses page tables to translate virtual addresses to physical addresses.  
Each user process owns its own page table, while kernel mappings are shared in the high-half address space.

Important virtual memory constants include:

```c
#define PAGE_OFFSET        0xffffffc000000000UL

#define USER_CODE_VA       0x0UL
#define USER_MMAP_BASE     0x100000000UL
#define USER_STACK_TOP     0x4000000000UL
#define USER_STACK_BASE    (USER_STACK_TOP - USER_STACK_SIZE)

#define PAGE_SIZE          4096
#define PMD_SIZE           (1UL << 21)
#define PGD_SIZE           (1UL << 30)
```

The user virtual address layout is:

```text
0x0000000000
    ├── user code / data region
    │
0x000100000000
    ├── mmap region
    │
0x003fffffffff
    ├── user stack grows downward
0x004000000000
```

Each user process can use the same virtual addresses, but they map to isolated physical pages.

---

## Sv39 Page Table

The page table uses three levels:

```text
PGD -> PMD -> PTE -> physical page
```

The index macros are:

```c
#define PGD_INDEX(va) (((unsigned long)(va) >> PGD_SHIFT) & VPN_MASK)
#define PMD_INDEX(va) (((unsigned long)(va) >> PMD_SHIFT) & VPN_MASK)
#define PTE_INDEX(va) (((unsigned long)(va) >> PTE_SHIFT) & VPN_MASK)
```

The PTE permission bits include:

```c
#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)
```

For Copy-on-Write, I also use bit 8 as a software-defined COW bit:

```c
#define PTE_COW (1UL << 8)
#define PTE_FLAGS(pte) ((pte) & 0x3ffUL)
```

---

## Kernel and User Page Tables

The kernel first creates its own page table and maps:

- normal RAM
- UART MMIO
- PLIC MMIO
- framebuffer MMIO
- kernel high-half address space

Each user process creates its own page table using:

```c
unsigned long* create_user_pgd(void);
```

The user page table contains:

- user mappings below `USER_STACK_TOP`
- shared kernel high-half mappings copied from `final_kernel_pgd`

This is necessary because traps from user mode enter the kernel without automatically switching page tables.

---

## Page Mapping

Pages are mapped using:

```c
void map_pages(unsigned long* pgd,
               unsigned long va,
               unsigned long pa,
               unsigned long size,
               unsigned long prot);
```

This function walks the Sv39 page table levels and allocates intermediate page tables when needed.

Example:

```c
map_pages(current->pgd,
          page_va,
          VA2PA((unsigned long)page),
          PAGE_SIZE,
          vma->prot);
```

After changing page tables, the kernel flushes stale TLB entries using:

```c
sfence.vma
```

or by switching the current page table:

```c
switch_vm(current->pgd);
```

---

## VMA - Virtual Memory Area

I use a `vm_area` structure to record valid user memory regions.

```c
struct vm_area {
    unsigned long start;
    unsigned long end;
    unsigned long prot;
    int type;

    void* file_data;
    unsigned long file_size;

    struct vm_area* next;
};
```

Each user process has:

```c
struct vm_area* vma_list;
```

A VMA records:

- the valid virtual address range
- the permission bits
- whether the region is anonymous or file-backed
- file data source for demand loading user programs

The VMA types are:

```c
#define VMA_ANON 0
#define VMA_FILE 1
```

The kernel uses:

```c
struct vm_area* find_vma(struct task_struct* task, unsigned long addr);
```

to check whether a page fault address belongs to a valid user memory region.

---

## Revisit Syscalls - fork and exec with Virtual Memory

### Goal

In Lab 5, user programs relied on fixed physical addresses and manually separated stacks.  
In Lab 6, virtual memory removes this restriction.

Every user process can now have the same virtual memory layout:

```text
code  -> USER_CODE_VA
stack -> USER_STACK_TOP
mmap  -> USER_MMAP_BASE
```

but each process maps these virtual addresses to different physical pages.

---

## User Process Creation

A user process is created by:

```c
int user_process_create(const char* filename);
```

The function:

1. loads the user program from initramfs
2. creates a new user page table
3. creates a code VMA at `USER_CODE_VA`
4. creates a stack VMA at `USER_STACK_BASE`
5. creates a kernel stack
6. creates an initial trap frame
7. enters user mode through `ret_to_user`

The program code is not mapped immediately.  
Instead, it is registered as a `VMA_FILE` region and loaded on demand when an instruction page fault occurs.

The user stack is also not allocated immediately.  
It is registered as a `VMA_ANON` region and allocated on demand when the process touches the stack.

---

## System Call 3 - exec

### Function

```c
int exec(const char *path);
```

### Implementation

`exec()` replaces the current process image with a new user program.

The kernel:

1. loads the new program from initramfs
2. creates a new user page table
3. releases the old user pages
4. releases the old VMA list
5. switches to the new page table
6. creates a new code VMA
7. creates a new stack VMA
8. resets the trap frame
9. clears old signal state

Because `do_trap()` advances `sepc` after syscall handling, `process_exec()` sets:

```c
regs->sepc = USER_CODE_VA - 4;
```

After `do_trap()` adds 4, the final `sepc` becomes the new user program entry.

The old user address space is cleaned up by:

```c
release_user_pages(current);
release_vma_list(current);
free_user_pagetable(old_pgd);
```

---

## System Call 4 - fork

### Function

```c
long fork();
```

### Implementation

`fork()` creates a child process with:

- a new `task_struct`
- a new user page table
- a copied VMA list
- a new kernel stack
- a copied trap frame

The child process uses the same virtual address layout as the parent.

For Copy-on-Write, `fork()` does not immediately copy all user pages.  
Instead, it shares existing parent pages with the child:

```text
parent VA -> shared PA
child  VA -> shared PA
```

Writable pages are changed to:

```text
read-only + PTE_COW
```

This allows parent and child to share pages until one of them writes to a shared page.

The return values follow the standard fork behavior:

```c
child->trap_frame->a0 = 0;
return child->pid;
```

So:

- parent receives the child pid
- child receives 0

---

## Advanced Exercise 1 - mmap

### Goal

`mmap()` creates a new memory region for a user process.

This lab only requires anonymous mapping, so there is no real file-backed mmap support.

### System Call 13 - mmap

### Function

```c
void *mmap(void *addr, unsigned long length, int prot, int flags);
```

### Implementation

The syscall is implemented by:

```c
void* process_mmap(void* addr,
                   unsigned long length,
                   int prot,
                   int flags);
```

It supports:

```c
#define MAP_ANONYMOUS  0x20
#define MAP_POPULATE   0x8000
```

The protection flags are:

```c
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
```

The kernel converts mmap protection flags into PTE permissions:

```c
static unsigned long mmap_prot_to_pte(int prot);
```

If `addr` is valid, page-aligned, and does not overlap existing regions, the kernel uses it.  
Otherwise, it uses the process's `mmap_base`.

The mmap region is recorded as a VMA:

```c
add_vma(current,
        va,
        va + size,
        pte_prot,
        VMA_ANON,
        0,
        0);
```

If `MAP_POPULATE` is set, physical pages are allocated and mapped immediately.  
Otherwise, the region is only reserved and pages are allocated later by demand paging.

---

## Advanced Exercise 2 - Page Fault Handler and Demand Paging

### Goal

Instead of allocating all user pages at process creation time, the kernel allocates pages only when they are actually used.

This saves memory and avoids unnecessary work.

---

## Page Fault Causes

The page fault handler handles:

```text
12: instruction page fault
13: load page fault
15: store page fault
```

The fault address is stored in:

```c
regs->stval
```

The kernel aligns it to the page boundary:

```c
unsigned long page_va = PAGE_ALIGN_DOWN(addr);
```

---

## Demand Paging Flow

When a page fault happens:

1. The kernel checks whether the fault address belongs to a valid VMA.
2. If no VMA is found, the process is killed.
3. If the access type is not allowed by the VMA permission, the process is killed.
4. Otherwise, the kernel allocates one physical page.
5. The page is zero-filled.
6. If the VMA is file-backed, the corresponding program data is copied into the page.
7. The page is mapped into the current process page table.
8. The kernel prints the translation fault message.

Example log:

```text
[Translation fault]: 0x0000000000000000
```

---

## File-backed Demand Paging

User program code is represented as a `VMA_FILE`.

When the process first executes an unmapped code page, the kernel:

1. allocates a new page
2. calculates the file offset
3. copies program data from the loaded initramfs buffer
4. maps the page as user RX

Example:

```c
if (vma->type == VMA_FILE) {
    unsigned long offset = page_va - vma->start;
    ...
    mem_copy(page,
             (void*)((unsigned long)vma->file_data + offset),
             copy_size);
}
```

This allows user programs to be loaded lazily.

---

## Anonymous Demand Paging

Anonymous regions include:

- user stack
- mmap regions
- signal stack

When an anonymous page fault occurs, the kernel allocates a zero-filled page and maps it into the user page table.

This is used for:

```text
USER_STACK_BASE ~ USER_STACK_TOP
USER_MMAP_BASE  ~ mmap_base
```

---

## Advanced Exercise 3 - Copy-on-Write

### Goal

In a normal fork implementation, all user pages are copied immediately.  
However, many child processes call `exec()` right after `fork()`, so copying all pages can waste memory and CPU time.

Copy-on-Write avoids this by sharing pages after fork and copying only when a process writes.

---

## Page Reference Count

The kernel tracks how many processes share a physical page.

```c
static unsigned int page_ref[MAX_TRACKED_PAGES];
```

Helper functions:

```c
void page_ref_set(unsigned long pa, unsigned int count);
void page_ref_inc(unsigned long pa);
void page_ref_dec(unsigned long pa);
unsigned int page_ref_get(unsigned long pa);
```

When a page is newly allocated:

```c
page_ref_set(pa, 1);
```

When a page is shared during fork:

```c
page_ref_inc(pa);
```

When a process exits or replaces its address space:

```c
page_ref_dec(pa);
```

If the reference count becomes 0, the physical page can be freed.

---

## COW Setup During fork

During fork, the kernel walks through the parent's VMA list and checks which pages already have valid PTEs.

For each mapped user page:

1. get the parent PTE
2. extract the physical address
3. extract the PTE flags
4. if the page is writable:
   - clear `PTE_W`
   - set `PTE_COW`
   - update the parent PTE
5. map the child page table to the same physical page
6. increment the physical page reference count

Example behavior:

```text
Before fork:
parent VA -> PA, writable

After fork:
parent VA -> PA, read-only + COW
child  VA -> PA, read-only + COW
refcount(PA) = 2
```

Read access does not fault.  
Write access causes a store page fault.

---

## COW Page Fault Handling

When a store page fault occurs, the kernel checks whether the PTE has `PTE_COW`.

If it is not a COW page, it is treated as a normal permission fault.

If it is a COW page:

- If the reference count is 1:
  - the process is now the only owner
  - the kernel simply restores `PTE_W`
  - clears `PTE_COW`

- If the reference count is greater than 1:
  - the kernel allocates a new page
  - copies the old page content
  - updates the current process PTE to the new page
  - restores write permission
  - clears `PTE_COW`
  - decrements the old page reference count
  - sets the new page reference count to 1

The kernel prints:

```text
[Permission fault]: <addr>
```

This means the fault was caused by a write to a COW page.

---

## Memory Cleanup

To prevent memory leaks, the kernel releases user memory when a process exits, is collected by `waitpid()`, is recycled by `kill_zombies()`, or calls `exec()`.

The cleanup is split into three parts:

### 1. Releasing user pages

```c
static void release_user_pages(struct task_struct* task);
```

This function walks through the VMA list and checks existing PTEs.

For each mapped user page:

1. decrement the page reference count
2. free the page if the reference count becomes 0
3. clear the PTE

### 2. Releasing VMA list

```c
static void release_vma_list(struct task_struct* task);
```

This frees all `struct vm_area` nodes allocated by `add_vma()`.

### 3. Releasing user page table pages

```c
static void free_user_pagetable(unsigned long* pgd);
```

This frees:

- PTE table pages
- PMD table pages
- PGD page

It only frees user-space page table entries.  
Kernel high-half mappings are shared from `final_kernel_pgd` and must not be freed.

The function also checks:

```c
if (pgd == 0 || pgd == final_kernel_pgd) return;
```

to avoid accidentally freeing the kernel page table.

---

## Signal Handling with Virtual Memory

The Lab 5 signal design had to be updated for virtual memory.

In Lab 5, the signal stack was a kernel allocated address, and the user program could directly use it because MMU isolation was not enabled.

In Lab 6, this is no longer valid.  
User mode cannot use kernel virtual addresses.

Therefore, the signal stack is now mapped into the user address space.

The signal stack uses:

```c
#define USER_SIGNAL_STACK_TOP   USER_STACK_BASE
#define USER_SIGNAL_STACK_BASE  (USER_SIGNAL_STACK_TOP - SIGNAL_STACK_SIZE)
```

When a signal is delivered, the kernel:

1. saves the original user trap frame
2. allocates a physical signal stack
3. maps it to `USER_SIGNAL_STACK_BASE`
4. writes the trampoline using the kernel virtual address
5. sets user `sp` and `ra` to user virtual addresses
6. returns to the user signal handler

The important distinction is:

```text
kernel writes trampoline using kernel VA
user executes trampoline using user VA
```

The trampoline code is:

```c
trampoline[0] = 0x00b00893;  // addi a7, zero, 11
trampoline[1] = 0x00000073;  // ecall
trampoline[2] = 0x0000006f;  // j .
```

After writing the trampoline, the kernel executes:

```c
asm volatile(".word 0x0000100f" ::: "memory");
```

This is `fence.i`, which ensures instruction fetch sees the trampoline instructions.

When the signal handler returns, it jumps to the trampoline, which invokes:

```text
syscall 11: sigreturn
```

---

## sigreturn

### Function

```c
void sigreturn();
```

### Implementation

`process_sigreturn()` restores the original trap frame saved before signal handling.

It also removes the temporary signal stack mapping:

1. find the PTE for `USER_SIGNAL_STACK_BASE`
2. decrement its page reference count
3. clear the PTE
4. free the physical signal stack
5. restore the saved trap frame
6. clear `handling_signal`

Because the trap handler advances `sepc` after syscall handling, `sigreturn()` subtracts 4:

```c
regs->sepc -= 4;
```

This makes the restored `sepc` correct after returning from syscall handling.

---

## Required System Calls

The implemented syscall numbers are:

```c
#define SYS_GETPID      0
#define SYS_UART_READ   1
#define SYS_UART_WRITE  2
#define SYS_EXEC        3
#define SYS_FORK        4
#define SYS_WAITPID     5
#define SYS_EXIT        6
#define SYS_STOP        7
#define SYS_DISPLAY     8
#define SYS_USLEEP      9
#define SYS_SIGNAL      10
#define SYS_SIGRETURN   11
#define SYS_KILL        12
#define SYS_MMAP        13
```

---

## System Call Summary

| Syscall | Number | Description |
|---|---:|---|
| `getpid` | 0 | Return current process ID |
| `uart_read` | 1 | Read characters from UART into user buffer |
| `uart_write` | 2 | Write characters from user buffer to UART |
| `exec` | 3 | Replace current user process image |
| `fork` | 4 | Create child process using COW |
| `waitpid` | 5 | Wait for a child process to exit |
| `exit` | 6 | Terminate current process |
| `stop` | 7 | Stop another process |
| `display` | 8 | Display image on framebuffer |
| `usleep` | 9 | Sleep for a number of microseconds |
| `signal` | 10 | Register a signal handler |
| `sigreturn` | 11 | Restore context after signal handling |
| `kill` | 12 | Send signal to a target process |
| `mmap` | 13 | Create anonymous memory mapping |

---

## Video Player Support

The video player from Lab 5 still works with virtual memory.

The `display()` syscall accepts a user virtual address for the image buffer.  
The kernel uses the current process page table to access user memory correctly.

The framebuffer is mapped in the kernel high-half address space, and the video driver writes to the Orange Pi RV2 framebuffer.

The video child process can still be created using:

```text
$ fork
```

and stopped using:

```text
$ stop <pid>
```

---

## Timer and Preemption

The timer interrupt is still used for preemption and sleeping process wakeup.

The kernel periodically:

1. handles timer events
2. runs task queue callbacks
3. wakes sleeping tasks
4. sets `need_resched`
5. schedules the next runnable task
6. checks pending signals before returning to user mode

This allows multiple user processes to run concurrently.

---

## Kernel Shell Commands

Implemented kernel shell commands include:

```text
help
hello
info
ls
cat <filename>
exec <filename>
settimeout <sec> <message>
```

The most important command for this lab is:

```text
opi-rv2> exec osctest.bin
```

This creates the first user process from initramfs.

---

## User Program Commands

Inside the user shell, the important commands include:

```text
help
pid
exec [prog]
fork_test
fork
stop [pid]
signal
kill [pid]
mmap_r
mmap_w
demand
exit
```

---

## Demo Flow

A complete demo flow can be:

```text
opi-rv2> exec osctest.bin
$ pid
$ mmap_r
$ mmap_w
$ demand
$ fork_test
$ fork
$ stop <child_pid>
$ signal
$ fork
$ kill <child_pid>
$ exec osctest.bin
$ exit
```

Expected behavior:

- `pid` prints the current process ID
- `mmap_r` verifies anonymous mmap read behavior
- `mmap_w` verifies anonymous mmap write behavior
- `demand` triggers demand paging and prints translation fault logs
- `fork_test` verifies fork / waitpid / exit
- `fork` creates a child process
- COW pages are shared after fork
- writing to a COW page prints `[Permission fault]`
- `exec` replaces the current program image
- `signal` registers a signal handler
- `kill <pid>` sends a signal to a target process
- signal handler runs using a user-mapped signal stack
- `sigreturn` restores the original user context
- `exit` terminates the user process and returns to the kernel shell

---

## Example Logs

Demand paging:

```text
[Translation fault]: 0x0000000000000000
[Translation fault]: 0x0000003ffffffff8
```

COW write fault:

```text
[Permission fault]: 0x0000003ffffff000
```

Invalid memory access:

```text
[Segmentation fault]: Kill Process
```

Process cleanup:

```text
[waitpid] free pid: 0x0000000000000003
[kill_zombies] free pid: 0x0000000000000004
```

---

## Summary

This lab implementation completes:

- RISC-V Sv39 virtual memory
- kernel high-half mapping
- per-process user page tables
- user virtual address layout
- page table walking
- page mapping
- TLB flushing with `sfence.vma`
- VMA-based memory region tracking
- lazy user program loading
- demand paging
- anonymous mmap
- MAP_POPULATE support
- fork / exec with isolated address spaces
- Copy-on-Write fork
- PTE_COW software flag
- page reference counting
- COW store page fault handling
- user page cleanup
- VMA list cleanup
- user page table cleanup
- signal handling with user-mapped signal stack
- signal trampoline and sigreturn
- existing Lab 5 process management and video support

Overall, the kernel is extended from a simple multitasking OS into a virtual-memory-based multitasking kernel that supports isolated user processes, lazy allocation, mmap, and Copy-on-Write.

---

## Notes / Known Design Choices

- Each user process owns its own page table.
- Kernel high-half mappings are shared across all user page tables.
- User code starts at `USER_CODE_VA`.
- User stack uses the same virtual address range for every process.
- User code and stack are loaded lazily through page faults.
- `mmap()` only supports anonymous mappings in this lab.
- `MAP_POPULATE` immediately allocates physical pages.
- Without `MAP_POPULATE`, mmap pages are allocated by demand paging.
- `fork()` uses Copy-on-Write instead of immediately copying all user pages.
- Only writable pages are marked as COW.
- Read-only pages can be shared directly.
- A COW page is copied only when a process writes to it.
- Page reference counts are used to decide when a physical page can be freed.
- `process_exec()` releases the old address space before switching to the new one.
- `waitpid()` and `kill_zombies()` recycle zombie process memory.
- The signal stack must be mapped into user virtual memory.
- The kernel writes signal trampoline code through kernel VA, but user mode executes it through user VA.
- `fence.i` is required after writing signal trampoline instructions.
- Page table pages are freed only for user page tables, never for `final_kernel_pgd`.
