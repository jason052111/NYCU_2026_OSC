# NYCU 2025 OS Lab 5 - Thread and User Process

This repository contains my implementation for NYCU 2025 Operating System Lab 5.

The goal of this lab is to extend the kernel from a basic interrupt-driven system into a small multitasking kernel that supports:

- kernel threads
- context switching
- user processes
- system calls
- fork / exec / waitpid / exit
- user-level video display
- process sleeping
- POSIX-like signal handling

The kernel runs in S-mode on RISC-V and executes user programs in U-mode.

---

## Overview

In this lab, I implemented the following parts:

- Basic Exercise 1: Kernel thread mechanism
- Basic Exercise 2: User process and system calls
- Basic Exercise 3: Video player with user process support
- Advanced Exercise: POSIX-like signal handling

The kernel also includes:

- run queue scheduling
- context switching
- trap frame save / restore
- user process creation from initramfs
- syscall dispatching
- framebuffer output on Orange Pi RV2
- sleeping process wakeup through timer interrupt
- zombie process recycling
- signal handler trampoline and sigreturn support
- separated source/header structure using `src/` and `inc/`

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
| `src/thread.c` | Thread/process management, scheduler, fork, exec, waitpid, exit, sleep, and signal handling |
| `inc/thread.h` | `task_struct`, `thread_struct`, task states, stack sizes, and function declarations |
| `src/trap.c` | Trap/interrupt handling, timer handling, task queue, and preemption |
| `inc/trap.h` | `trap_frame`, timer event, task queue structures, and trap-related declarations |
| `src/syscall.c` | System call dispatcher for user programs |
| `inc/syscall.h` | System call numbers and syscall handler declaration |
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
```

---

## Basic Exercise 1 - Kernel Thread

### Goal

The first part of the lab is to implement kernel threads, context switching, scheduling, and zombie thread recycling.

### What I implemented

I implemented a thread system using a `task_struct`.

Each task contains:

```c
struct thread_struct thread;

int pid;
int state;
int type;
void (*entry)(void);

unsigned long stack;
unsigned long kernel_sp;

struct task_struct* next;
```

The `thread_struct` stores the callee-saved kernel context:

```c
struct thread_struct {
    unsigned long ra;
    unsigned long sp;
    unsigned long s[12];
};
```

### Thread creation

Kernel threads are created using:

```c
struct task_struct* kthread_create(void (*threadfn)(void));
```

The function allocates:

- a `task_struct`
- a kernel stack
- initial saved context

The new thread is inserted into the circular run queue.

### Context switch

The low-level context switch is implemented in `start.S`.

It saves the previous thread's context:

```asm
sd ra, 0(a0)
sd sp, 8(a0)
sd s0, 16(a0)
...
sd s11, 104(a0)
```

Then it restores the next thread's context:

```asm
ld ra, 0(a1)
ld sp, 8(a1)
ld s0, 16(a1)
...
ld s11, 104(a1)
```

Finally, it updates `tp` to point to the next task:

```asm
mv tp, a1
ret
```

The `tp` register is used to store the current task pointer, so `get_current()` can directly retrieve the currently running task.

### Scheduler

The scheduler scans the circular run queue and selects the next runnable task:

```c
void schedule(void);
```

It skips tasks that are:

- waiting
- sleeping
- zombie

and switches to the next runnable task using:

```c
switch_to(current, next);
```

### Idle thread and zombie recycling

The idle thread always exists and runs when no other task is ready.

It also recycles zombie tasks using:

```c
static void kill_zombies(void);
```

For user processes, the kernel frees:

- kernel stack
- user stack
- signal stack
- task_struct

Zombie user processes that still have a parent are not immediately freed by the idle thread.  
They are kept until the parent calls `waitpid()`.  
If a zombie process becomes orphaned, the idle thread can recycle it.

---

## Basic Exercise 2 - User Process and System Call

### Goal

The second part of the lab is to support user processes running in U-mode and allow them to request kernel services through system calls.

---

## Trap Frame

When a user process enters the kernel, all registers are saved into a trap frame.

My `trap_frame` contains:

```c
struct trap_frame {
    unsigned long ra;
    unsigned long sp;
    unsigned long gp;
    unsigned long tp;

    unsigned long t0;
    unsigned long t1;
    unsigned long t2;

    unsigned long s0;
    unsigned long s1;

    unsigned long a0;
    unsigned long a1;
    unsigned long a2;
    unsigned long a3;
    unsigned long a4;
    unsigned long a5;
    unsigned long a6;
    unsigned long a7;

    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;

    unsigned long t3;
    unsigned long t4;
    unsigned long t5;
    unsigned long t6;

    unsigned long sepc;
    unsigned long sstatus;
    unsigned long scause;
    unsigned long stval;
};
```

The trap entry in `start.S` saves registers onto the kernel stack.

Before returning to user mode, `ret_from_exception` restores the trap frame and executes:

```asm
sret
```

---

## Entering User Mode

A user process is created by:

```c
int user_process_create(const char* filename);
```

This function is used by the kernel side, especially the kernel shell.

It:

1. loads the user program from initramfs
2. allocates a `task_struct`
3. allocates a kernel stack
4. allocates a user stack
5. creates an initial trap frame on the kernel stack
6. sets `sepc` to the loaded program address
7. sets `sp` to the top of the user stack
8. sets `sstatus.SPP = 0` so `sret` returns to U-mode
9. sets `thread.ra = ret_to_user`
10. inserts the task into the run queue

When the scheduler first switches to the user process, it returns to `ret_to_user`, restores the trap frame, and enters U-mode.

The kernel shell command:

```text
opi-rv2> exec osctest.bin
```

uses `user_process_create()` to create the first user process.

---

## Required System Calls

I implemented the required system calls with the numbers specified in the lab:

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
```

The syscall dispatcher is implemented in `syscall.c`.

---

## System Call 0 - getpid

### Function

```c
long getpid();
```

### Implementation

The kernel returns the current task's pid:

```c
return get_current()->pid;
```

This allows the user program to identify its own process ID.

---

## System Call 1 - uart_read

### Function

```c
long uart_read(char *buf, long count);
```

### Implementation

The kernel reads characters from UART into the user buffer.

Since MMU is not enabled in this lab, user pointers can be directly used as physical addresses.

---

## System Call 2 - uart_write

### Function

```c
long uart_write(const char *buf, long count);
```

### Implementation

The kernel writes `count` bytes from the user buffer to UART.

This is used by user-space `printf`.

---

## System Call 3 - exec

### Function

```c
int exec(const char *path);
```

### Implementation

The syscall `exec()` does not create a new process.

Instead, it replaces the current user process image with another program loaded from initramfs.

The syscall handler calls:

```c
process_exec(path, regs);
```

`process_exec()`:

1. loads the new program from initramfs
2. clears the current user stack
3. resets the current trap frame
4. sets the new `sepc` to the new program entry
5. resets the user stack pointer
6. clears old signal state
7. returns to the new program after syscall handling

Because `do_trap()` advances `sepc` after syscall handling, `process_exec()` sets:

```c
regs->sepc = (unsigned long)prog - 4;
```

After `do_trap()` adds 4, the final `sepc` becomes the new program entry.

This design follows the common fork/exec model:

```text
fork()
child process calls exec()
child keeps the same pid but runs a new program
```

This is different from the kernel shell command `exec`.  
The kernel shell is a kernel thread, so it uses `user_process_create()` to create the first user process.

---

## System Call 4 - fork

### Function

```c
long fork();
```

### Implementation

`process_fork()` duplicates the current user process.

It creates a child process with:

- a new `task_struct`
- a new kernel stack
- a new user stack
- a copied user stack
- a copied trap frame

The child process resumes from the same user context as the parent.

Important details:

```c
child->trap_frame->a0 = 0;
return child->pid;
```

So:

- parent receives the child pid
- child receives 0

The child also inherits the parent's registered signal handler.

---

## System Call 5 - waitpid

### Function

```c
long waitpid(long pid);
```

### Implementation

`process_waitpid()` waits for a child process to become zombie.

If the child has already exited, it is removed from:

- the parent's children list
- the run queue

Then the kernel frees its resources and returns the child pid.

If the child has not exited yet, the current process enters:

```c
TASK_WAITING
```

and calls:

```c
schedule();
```

---

## System Call 6 - exit

### Function

```c
void exit(int status);
```

### Implementation

`process_exit()` marks the current process as:

```c
TASK_ZOMBIE
```

It wakes up the parent if the parent is waiting for it.

It also handles orphan children by detaching them from the exiting parent.

---

## System Call 7 - stop

### Function

```c
int stop(long pid);
```

### Implementation

`process_stop()` finds the target process and marks it as zombie.

It refuses to stop:

- a non-existing process
- the current process itself
- a process that is already zombie

This prevents repeatedly stopping the same zombie process.

If the target has children, they are detached as orphan processes.  
If the target's parent is waiting for it, the parent is woken up.

---

## Basic Exercise 3 - Video Player

### Goal

The goal of Basic Exercise 3 is to allow the user program to display video frames through system calls while the shell and video child process run concurrently.

The required system calls are:

```text
8: display(unsigned int *bmp_image, unsigned int width, unsigned int height)
9: usleep(unsigned int usec)
```

---

## System Call 8 - display

### Function

```c
void display(unsigned int *bmp_image, unsigned int width, unsigned int height);
```

### Implementation

The syscall calls:

```c
video_bmp_display(bmp_image, width, height);
```

The video driver writes the image into the framebuffer.

For Orange Pi RV2, the framebuffer base address is:

```c
#define FB_BASE 0x7f700000UL
```

The display function centers the image on a 1920x1080 framebuffer.

After writing to the framebuffer, the kernel flushes the cache using `cbo.flush`:

```c
#define cbo_flush(start)                \
    ({                                  \
        asm volatile("mv a0, %0\n\t"    \
                     ".word 0x0025200F" \
                     :                  \
                     : "r"(start)       \
                     : "memory", "a0"); \
    })
```

This is required because CPU writes may stay in the data cache, while the HDMI/display hardware reads framebuffer data from DRAM.  
Flushing the cache writes the latest pixel data back to DRAM.

---

## System Call 9 - usleep

### Function

```c
int usleep(unsigned int usec);
```

### Implementation

`process_usleep()` converts microseconds into timer ticks:

```c
unsigned long ticks = (TIMEBASE_FREQUENCY * usec) / 1000000;
```

Then the current process is changed to:

```c
TASK_SLEEPING
```

and its wakeup time is stored:

```c
current->wakeup_time = get_time() + ticks;
```

The process then yields the CPU with:

```c
schedule();
```

The timer interrupt periodically calls:

```c
wakeup_sleeping_tasks();
```

This checks sleeping tasks and marks them runnable when their wakeup time has passed.

This allows the video child process to sleep between frames without busy waiting.

---

## Video Player Behavior

The provided user program supports:

```text
fork
stop [pid]
```

When the user enters:

```text
$ fork
```

the user program forks a child process.

The child process repeatedly:

1. displays a video frame
2. sleeps using `usleep`
3. displays the next frame

Because `usleep` yields the CPU, the shell and video process can run concurrently.

When the user enters:

```text
$ stop <pid>
```

the video child is terminated and later recycled.

---

## Advanced Exercise - POSIX-like Signal

### Goal

The advanced part implements a simplified POSIX-like signal mechanism.

The required system calls are:

```text
10: signal(int signum, void (*handler)())
11: sigreturn()
12: kill(int pid, int signum)
```

---

## System Call 10 - signal

### Function

```c
long signal(int signum, void (*handler)());
```

### Implementation

The current user process registers a signal handler:

```c
current->signal_handler = (unsigned long)handler;
current->signal_number = signum;
```

This handler address is inherited by child processes created after registration.

---

## System Call 12 - kill

### Function

```c
int kill(int pid, int signum);
```

### Implementation

`process_kill()` finds the target process by pid.

If the target has a registered handler:

```c
target->signal_pending = 1;
target->signal_number = signum;
```

Then the target will execute its handler the next time it returns to user mode.

If the target does not have a registered handler, the default behavior is to terminate the process:

```c
target->state = TASK_ZOMBIE;
```

This matches the lab requirement that a process without a registered handler should be terminated by default.

If the target is sleeping, it is woken up so it can handle the signal.

---

## Pending Signal Handling

The kernel checks for pending signals before returning to user mode:

```c
handle_pending_signal(regs);
```

If a process has a pending signal, the kernel:

1. saves the original trap frame
2. allocates a signal stack
3. writes a trampoline onto the signal stack
4. changes `sepc` to the user handler
5. changes `ra` to the trampoline address
6. changes `sp` to the signal stack
7. passes `signum` in `a0`

The important part is:

```c
regs->sepc = current->signal_handler;
regs->ra = (unsigned long)trampoline;
regs->sp = sig_sp;
regs->a0 = current->signal_number;
```

So when the process returns to user mode, it starts executing the handler.

When the handler returns, it jumps to the trampoline.

---

## Signal Trampoline and sigreturn

### Why trampoline is needed

The lab requires that `sigreturn()` is called automatically through a trampoline set by the kernel.

Therefore, the kernel writes a small trampoline into the signal stack:

```c
trampoline[0] = 0x00b00893;  // addi a7, zero, 11
trampoline[1] = 0x00000073;  // ecall
trampoline[2] = 0x0000006f;  // j .
```

This code does:

```asm
li a7, 11
ecall
j .
```

So when the handler returns, it automatically executes syscall 11.

After writing the trampoline, the kernel executes `fence.i` using raw instruction encoding:

```c
asm volatile(".word 0x0000100f" ::: "memory");
```

This ensures that CPU instruction fetch sees the newly written trampoline code.

---

## System Call 11 - sigreturn

### Function

```c
void sigreturn();
```

### Implementation

`process_sigreturn()` restores the original user context saved before signal handling:

```c
mem_copy(regs,
         &current->signal_saved_context,
         sizeof(struct trap_frame));
```

It also frees the signal stack:

```c
free((void*)current->signal_stack);
```

Then it clears:

```c
current->handling_signal = 0;
```

Because the trap handler always advances `sepc` after a syscall, `process_sigreturn()` subtracts 4 before returning:

```c
regs->sepc -= 4;
```

This makes the final restored `sepc` correct after the trap handler adds 4.

---

## Signal Behavior Across fork and exec

The signal behavior follows a simplified Linux-like model.

For `fork()`:

- the child inherits the parent's registered signal handler
- pending signal state is not inherited
- signal stack is not inherited

For `exec()`:

- the process keeps the same pid
- the old user program image is replaced
- old signal state is cleared
- old registered handler is cleared
- old signal stack is freed

This is necessary because after `exec()`, the old handler address may no longer be valid for the new program.

---

## Timer and Preemption

The kernel uses a timer interrupt for preemption.

A timer callback periodically sets:

```c
need_resched = 1;
```

When the timer interrupt occurs, the kernel:

1. handles expired timer events
2. runs task queue callbacks
3. wakes sleeping tasks
4. calls `schedule()` when rescheduling is needed
5. checks pending signals before returning to user mode

This allows the shell, video child, and other user processes to run concurrently.

---

## Device Tree Usage

The kernel uses DTB parsing to discover hardware information dynamically.

Parsed values include:

- UART base address
- UART IRQ number
- PLIC base address
- timer frequency
- initramfs location
- physical memory region

This avoids hardcoding most hardware addresses, except for the framebuffer base specified by the lab for Orange Pi RV2.

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
exec osctest.bin
```

This command creates a new user process from initramfs and waits for it to finish.

The `settimeout` command registers a timer callback and prints a message after the specified number of seconds.

---

## User Program Commands

Inside the user shell, the important commands include:

```text
pid
fork_test
fork
stop [pid]
signal
kill [pid]
exit
```

---

## Demo Flow

A complete demo flow can be:

```text
opi-rv2> exec osctest.bin
$ pid
$ fork_test
$ fork
$ stop <child_pid>
$ signal
$ fork
$ kill <child_pid>
$ stop <child_pid>
$ exit
```

Expected behavior:

- `pid` prints the current process ID
- `fork_test` verifies fork / waitpid / exit
- `fork` creates a video child process
- the video plays on HDMI
- the shell remains usable while the video is playing
- `stop <pid>` stops the video child
- repeated `stop` on an already-zombie process is rejected
- `signal` registers a SIGTERM handler
- `kill <pid>` sends SIGTERM
- if a handler exists, the handler runs
- after the handler returns, trampoline calls sigreturn automatically
- `exit` terminates the user shell and returns to the kernel shell

---

## Summary

This lab implementation completes:

- kernel thread creation
- context switching
- round-robin scheduling
- idle thread
- zombie recycling
- user process creation
- U-mode execution
- trap frame save and restore
- syscall dispatcher
- getpid / uart_read / uart_write
- exec / fork / waitpid / exit / stop
- display syscall
- usleep syscall
- Orange Pi RV2 framebuffer output
- cache flush for framebuffer
- timer-based sleeping and wakeup
- signal handler registration
- kill with default terminate behavior
- pending signal delivery
- signal stack
- kernel-generated trampoline
- automatic sigreturn
- original user context restoration
- separated `src/` and `inc/` project structure

Overall, the kernel is extended into a small multitasking OS that supports both kernel threads and user processes, and provides basic process control, video output, and POSIX-like signal handling.

---

## Notes / Known Design Choices

- The kernel does not enable MMU in this lab, so user pointers are treated as physical addresses.
- Kernel source files are organized into `kernel/src`, and header files are organized into `kernel/inc`.
- The Makefile uses `-Iinc` so source files can include headers directly, such as `#include "thread.h"`.
- Each user process has its own user stack and kernel stack.
- `user_process_create()` is used by the kernel shell to create a new user process from a file.
- User-mode `exec()` uses `process_exec()` to replace the current process image without changing the PID.
- `fork()` copies the user stack and trap frame to create a child process.
- `fork()` inherits the parent's registered signal handler.
- `exec()` clears old signal state because the process is now running a new program image.
- Zombie user processes with a parent are recycled by `waitpid`.
- Orphan zombie processes are recycled by the idle thread.
- `stop()` marks a target process as zombie and ignores already-zombie targets.
- `sigreturn()` is invoked automatically through a kernel-generated trampoline.
- The framebuffer base is fixed at `0x7f700000` for Orange Pi RV2.
- `cbo.flush` is required after framebuffer writes to avoid stale cache data.
- `fence.i` is required after writing signal trampoline instructions so the CPU can fetch the newly written code.
