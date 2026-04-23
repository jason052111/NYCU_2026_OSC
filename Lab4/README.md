# NYCU 2025 OS Lab - Kernel Implementation Notes

This repository contains my implementation for the NYCU 2025 OS lab on RISC-V.
The project is built around a small S-mode kernel running on the board/QEMU, and gradually extends from basic trap handling to timer, UART interrupt, and task/timer frameworks.

---

## Overview

In this lab, I implemented the following parts:

- Basic 1: Exception / trap handling
- Basic 2: Timer interrupt and periodic boot time output
- Basic 3: UART interrupt with ring buffer
- Advanced 1: Software timer API with callback support
- Advanced 2: Task queue framework with priority-based execution

The kernel also includes:

- DTB parsing for hardware information
- Buddy allocator / dynamic allocation support
- Initramfs file listing / reading / execution
- Simple command-line shell

---

## Project Structure

main_kernel.c   - kernel entry and main shell loop
trap.c          - trap/interrupt/exception handling, timer queue, task queue
uart.c          - UART driver, ring buffer, PLIC setup
sbi.c           - SBI calls such as set_timer
fdt.c           - Device Tree parsing helpers
buddy.c         - physical page allocator + small chunk allocator
initrd.c        - initramfs parsing, file execution
cmd_kernel.c    - shell commands
start.S         - low-level startup / trap entry
link_kernel.ld  - linker script

---

## Part 1 - Basic 1: Exception Handling

### Goal

The first part of the lab is to correctly handle exceptions/traps in S-mode.

### What I implemented

In trap.c, I first distinguish whether the trap is:

- an exception
- or an interrupt

using:

unsigned long is_interrupt = regs->scause >> 63;
unsigned long cause = regs->scause & 0xff;

If it is not an interrupt, the kernel enters the exception path and prints:

- scause
- sepc
- stval

for debugging and inspection.

if (!is_interrupt) {
    uart_puts("=== S-Mode exception ===\n");

    uart_puts("scause: ");
    uart_int((int)regs->scause);
    uart_puts("\n");

    uart_puts("sepc: ");
    uart_hex(regs->sepc);
    uart_puts("\n");

    uart_puts("stval: ");
    uart_int((int)regs->stval);
    uart_puts("\n");

    regs->sepc += 4;
    return;
}

### Explanation

For the lab requirement, the important part is that after handling an ecall or exception, the kernel should not repeatedly trap on the same instruction. Therefore, I move sepc forward by 4 bytes:

regs->sepc += 4;

This allows execution to continue at the next instruction after returning from trap.

---

## Part 2 - Basic 2: Timer Interrupt

### Goal

The goal of Basic 2 is to enable timer interrupt support and print boot time periodically.

### What I implemented

#### 1. Read timer frequency from DTB

I parse the timebase-frequency from the /cpus node in DTB:

unsigned long get_timebase_freq(unsigned long dtb_addr){
    const void *fdt = (const void *)dtb_addr;
    int node = fdt_path_offset(fdt, "/cpus");
    if (node >= 0) {
        int len = 0;
        const uint32_t *reg = fdt_getprop(fdt, node, "timebase-frequency", &len);
        return (unsigned long)bswap32(reg[0]);
    }
    return -1;
}

Then I store it into TIMEBASE_FREQUENCY.

#### 2. Enable timer interrupt

I enable S-mode timer interrupt using:

void enable_timer_interrupt() {
    asm volatile(
        "li t0, (1 << 5);"
        "csrs sie, t0;");
}

and global interrupt with:

void irq_enable() {
    asm volatile("csrsi sstatus, (1 << 1)");
}

#### 3. Record boot time and print it periodically

I save the initial boot timestamp:

BOOT_TIME = get_time();

and print the first line as:

uart_puts("boot time: 0\n");

Then I register a timer callback:

add_timer(boot_time_callback, 0, 2);

The callback is:

void boot_time_callback(void* arg) {
    print_boot_time();
    add_timer(boot_time_callback, 0, 2);
}

So every time it runs, it prints elapsed boot time and schedules itself again 2 seconds later.

### Explanation

This completes the basic timer interrupt mechanism.
The timer interrupt no longer only fires once; instead, it is used together with a software timer queue to periodically re-register future timer events.

---

## Part 3 - Basic 3: UART Interrupt

### Goal

The goal of Basic 3 is to replace pure polling-based UART input with UART interrupt handling and a ring buffer.

### What I implemented

#### 1. UART ring buffer

In uart.c, I implemented a circular buffer:

static char uart_buf[UART_BUF_SIZE];
static volatile unsigned int uart_buf_r = 0;
static volatile unsigned int uart_buf_w = 0;

with helper functions:

- uart_buf_push
- uart_buf_pop
- uart_buf_is_empty
- uart_buf_is_full

This lets the interrupt handler quickly store received characters and allows the shell to consume them later.

#### 2. UART interrupt initialization

I parse hardware information from DTB:

- UART_BASE
- UART_IRQ
- PLIC_BASE

using:

UART_IRQ  = get_uart_irq(fdt);
UART_BASE = get_address(fdt, "/soc/serial", "reg");
PLIC_BASE = get_address(fdt, "/soc/interrupt-controller", "reg");

Then I enable UART interrupt in both UART and PLIC.

#### 3. PLIC handling

In plic_init() I:

- set UART interrupt priority
- enable UART IRQ for the current hart
- set threshold to 0
- enable external interrupt

*PLIC_PRIORITY(UART_IRQ) = 1;
enable[word_index] |= (1U << bit_index);
*PLIC_THRESHOLD(hartid) = 0;
enable_external_interrupt();

#### 4. UART interrupt trap path

In trap.c, when cause == 9, I handle external interrupt:

int irq = plic_claim(BOOT_HARTID);

if (irq == UART_IRQ) {
    char c = (char)(*uart_rbr_addr());
    uart_buf_push(c);
}

if (irq) {
    plic_complete(irq, BOOT_HARTID);
}
run_task_queue();

This means the interrupt handler does only the minimum required work:

- claim IRQ
- read one byte from UART
- push it into ring buffer
- complete IRQ

#### 5. Shell input path

In main_kernel.c, the shell now reads input from:

char c = uart_getc();

and uart_getc() internally pops from the ring buffer.

### Explanation

This design makes UART input interrupt-driven instead of directly polling UART hardware every time in the shell loop.
The interrupt handler remains short, while the shell keeps the simple line-buffer logic.

---

## Part 4 - Advanced 1: Timer API

### Goal

The goal of Advanced 1 is to build a software timer mechanism on top of the one-shot hardware timer.

### Required API

The required API is:

void add_timer(void (*callback)(void*), void* arg, int sec)

### What I implemented

I implemented a timer event queue:

struct timer_event {
    unsigned long expire_time;
    void (*callback)(void*);
    void* arg;
    struct timer_event* next;
};

The queue is sorted by expire_time.

#### add_timer

When a timer is added:

- allocate a new node
- compute expire_time = get_time() + sec * TIMEBASE_FREQUENCY
- insert the node into the sorted linked list
- if it becomes the earliest timer, reprogram hardware timer

new_node->expire_time = get_time() + (unsigned long)sec * TIMEBASE_FREQUENCY;

#### pop_head_timer_event

I also implemented:

struct timer_event* pop_head_timer_event(void)

to remove the earliest timer from the software queue.

#### settimeout command

In cmd_kernel.c, I implemented:

settimeout <sec> <message>

It parses:

- the delay in seconds
- the remaining string as message

and then registers:

add_timer(print_message_callback, msg_copy, time);

The callback prints the message and frees the allocated string.

### Explanation

This part multiplexes multiple software timers onto a single one-shot hardware timer.
Only the earliest software timer is programmed into hardware, and the rest remain in the timer queue until they become the earliest.

---

## Part 5 - Advanced 2: Task Queue Framework

### Goal

The goal of Advanced 2 is to decouple interrupt top-half from the actual work by using a task queue.

### What I implemented

#### 1. Task structure

I implemented:

struct task {
    int priority;
    void (*callback)(void*);
    void* arg;
    struct task* next;
};

Each task stores:

- callback function
- callback argument
- priority
- linked-list pointer

#### 2. Task insertion API

I implemented:

void add_task(void (*callback)(void*), void* arg, int priority)

which appends a task to the task queue.

#### 3. Priority-based selection

Instead of FIFO execution, I implemented:

struct task* pop_highest_priority_task(void)

which scans the queue and removes the task with the highest priority.

So the framework supports priority-based task execution.

#### 4. Task execution

I implemented:

void run_task_queue(void)

which repeatedly picks the highest-priority task and executes it:

t->callback(t->arg);

The framework also tracks the currently running priority using:

current_task_priority

#### 5. Timer interrupt decoupling

Previously, timer interrupt directly executed callback functions.
Now in cause == 5, expired timer events are first converted into tasks:

struct timer_event* ev = pop_head_timer_event();
add_task(ev->callback, ev->arg, 5);
free(ev);

So the trap handler no longer directly executes timer callback logic.
Instead, the callback is deferred into the task queue.

This is the core idea of decoupling.

#### 6. Task execution timing

After handling expired timers, the trap handler temporarily disables timer interrupt source, re-enables global interrupts, and then runs the task queue:

disable_timer_interrupt();
irq_enable();
run_task_queue();
irq_disable();
enable_timer_interrupt();

This keeps the interrupt path short while still allowing task execution before returning.

#### 7. Priority test API

To demonstrate the task framework, I implemented test_task():

add_task(test_task_cb, "3", 3);
add_task(test_task_cb, "8", 8);
add_task(test_task_cb, "9", 9);
add_task(test_task_cb, "2", 2);
add_task(test_task_cb, "1", 1);
add_task(test_task_cb, "40", 40);
run_task_queue();

This shows that higher-priority tasks execute before lower-priority tasks.

### Explanation

This part separates the interrupt handler into:

- top-half: acknowledge hardware and enqueue work
- bottom-half: execute task queue later

Although run_task_queue() is still called within trap flow, the actual callback work is no longer directly executed at the first moment the interrupt fires.
So the timer callback logic is decoupled from the immediate hardware servicing path.

---

## Device Tree Usage

I also used DTB parsing to avoid hardcoding many hardware values.

### Parsed values include

- UART base address
- UART IRQ number
- PLIC base address
- timer frequency
- initrd location
- memory region information

This improves portability between board/QEMU configurations.

---

## Shell Commands

Implemented commands include:

help
hello
info
ls
cat
test
exec
settimeout <sec> <message>

### Notes

- test runs task framework priority test
- settimeout demonstrates software timer API
- exec loads user programs from initramfs and enters U-mode

---

## Summary

This lab implementation completes the following:

- trap and exception handling
- timer interrupt handling
- periodic boot time printing
- UART interrupt with ring buffer
- software timer queue
- timeout API with callback
- task queue with priority-based execution
- task framework demonstration
- initramfs file support
- DTB-based hardware discovery

Overall, the kernel evolves from a simple trap-based monitor into a small interrupt-driven kernel with timer and task abstractions.

---

## Notes / Known Design Choices

- UART is kept as a short interrupt handler with ring buffer instead of being fully converted into task framework.
- Timer callbacks are decoupled into task queue.
- Task queue currently uses linked-list + linear scan for highest-priority selection.
- Exception handling currently advances sepc by 4 for lab-oriented simplicity.

---
