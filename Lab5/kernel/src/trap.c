#include "trap.h"
#include "uart.h"
#include "sbi.h"
#include "fdt.h"
#include "buddy.h"
#include "thread.h"
#include "syscall.h"

extern long BOOT_HARTID;

unsigned long TIMEBASE_FREQUENCY = 0xFFFFFFFF;
unsigned long TIMER_INTERVAL     = 0xFFFFFFFF;
unsigned long BOOT_TIME          = 0xFFFFFFFF;

int current_task_priority = -1;
int need_resched = 0;

struct timer_event* timer_head = 0;
struct task* task_head = 0;
struct task* task_tail = 0;
/*
 * Read the current RISC-V time counter.
 * This value increases at the platform timebase frequency.
 */
unsigned long get_time(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}
/*
 * Print the elapsed time since the kernel booted.
 * The raw time counter is converted into seconds using TIMEBASE_FREQUENCY.
 */
void print_boot_time(void) {
    unsigned long now = get_time();
    unsigned long sec = (now - BOOT_TIME) / TIMEBASE_FREQUENCY;

    uart_puts("boot time: ");
    uart_int((int)sec);
    uart_puts("\n");
}
/*
 * Enable global supervisor interrupts by setting SIE in sstatus.
 */
void irq_enable(void) {
    asm volatile("csrsi sstatus, (1 << 1)");
}
/*
 * Disable global supervisor interrupts by clearing SIE in sstatus.
 */
void irq_disable(void) {
    asm volatile("csrci sstatus, (1 << 1)");
}
/*
 * Enable supervisor software interrupts.
 */
void enable_software_interrupt(void) {
    asm volatile(
        "li t0, (1 << 1);"
        "csrs sie, t0;"
    );
}
/*
 * Disable supervisor software interrupts.
 */
void disable_software_interrupt(void) {
    asm volatile(
        "li t0, (1 << 1);"
        "csrc sie, t0;"
    );
}
/*
 * Enable supervisor timer interrupts.
 */
void enable_timer_interrupt(void) {
    asm volatile(
        "li t0, (1 << 5);"
        "csrs sie, t0;"
    );
}
/*
 * Disable supervisor timer interrupts.
 */
void disable_timer_interrupt(void) {
    asm volatile(
        "li t0, (1 << 5);"
        "csrc sie, t0;"
    );
}
/*
 * Enable supervisor external interrupts.
 * This is needed for devices such as UART through the PLIC.
 */
void enable_external_interrupt(void) {
    asm volatile(
        "li t0, (1 << 9);"
        "csrs sie, t0;"
    );
}
/*
 * Read the timebase frequency from the DTB and set the timer interval.
 * TIMER_INTERVAL is set to two seconds worth of timer ticks.
 */
void set_time_base_freq(unsigned long dtb_addr) {
    TIMEBASE_FREQUENCY = get_dtb_prop_u32((const void*)dtb_addr, "/cpus", "timebase-frequency");
    TIMER_INTERVAL = TIMEBASE_FREQUENCY << 1;
}
/*
 * Add a timer event using raw timer ticks.
 *
 * The event will expire at current_time + ticks.
 * Timer events are inserted into a sorted linked list by expire_time,
 * so the earliest timer is always at timer_head.
 */
void add_timer_tick(void (*callback)(void*), void* arg, unsigned long ticks) {
    // A timer without a callback has nothing to execute.
    if (callback == 0) {
        return;
    }
    // Allocate a new timer event node.
    struct timer_event* new_node =
        (struct timer_event*)allocate(sizeof(struct timer_event));

    if (new_node == 0) {
        return;
    }
    // Set the expiration time and callback information.
    new_node->expire_time = get_time() + ticks;
    new_node->callback = callback;
    new_node->arg = arg;
    new_node->next = 0;
    /*
     * If the list is empty, or this new timer expires earlier than
     * the current first timer, insert it at the head.
     *
     * Since the earliest timer changed, update the hardware timer.
     */
    if (timer_head == 0 || new_node->expire_time < timer_head->expire_time) {
        new_node->next = timer_head;
        timer_head = new_node;
        sbi_set_timer(timer_head->expire_time);
        return;
    }
    /*
     * Find the correct position so the timer list remains sorted
     * by expire_time in increasing order.
     */
    struct timer_event* curr = timer_head;
    while (curr->next != 0 && curr->next->expire_time <= new_node->expire_time) {
        curr = curr->next;
    }
    // Insert the new timer event after curr.
    new_node->next = curr->next;
    curr->next = new_node;
}
/*
 * Add a timer event using seconds.
 *
 * This is a wrapper around add_timer_tick().
 * It converts seconds into timer ticks using TIMEBASE_FREQUENCY.
 */
void add_timer(void (*callback)(void*), void* arg, int sec) {
    add_timer_tick(callback, arg, (unsigned long)sec * TIMEBASE_FREQUENCY);
}
/*
 * Remove and return the first timer event from the timer list.
 *
 * The timer list is sorted by expire_time, so the head is always the
 * earliest timer event. After removing it, the returned node is detached
 * from the list.
 */
struct timer_event* pop_head_timer_event(void) {
    if (timer_head == 0) {
        return 0;
    }

    struct timer_event* node = timer_head;
    timer_head = timer_head->next;
    node->next = 0;

    return node;
}
/*
 * Add a task to the task queue.
 *
 * Each task contains a callback function, an argument, and a priority.
 * Tasks are appended to the tail of the queue. Higher priority tasks
 * will be selected first when the task queue is executed.
 */
void add_task(void (*callback)(void*), void* arg, int priority) {
    if (callback == 0) {
        return;
    }

    struct task* new_task = (struct task*)allocate(sizeof(struct task));

    if (new_task == 0) {
        return;
    }

    new_task->priority = priority;
    new_task->callback = callback;
    new_task->arg = arg;
    new_task->next = 0;

    if (task_head == 0) {
        task_head = new_task;
        task_tail = new_task;
    } else {
        task_tail->next = new_task;
        task_tail = new_task;
    }
}
/*
 * Remove and return the task with the highest priority.
 *
 * The task queue is a linked list, so this function scans the whole list
 * to find the task with the largest priority value. After finding it,
 * the task is removed from the queue and returned.
 */
struct task* pop_highest_priority_task(void) {
    if (task_head == 0) {
        return 0;
    }

    struct task* best = task_head;
    struct task* best_prev = 0;

    struct task* prev = 0;
    struct task* curr = task_head;

    while (curr != 0) {
        if (curr->priority > best->priority) {
            best = curr;
            best_prev = prev;
        }

        prev = curr;
        curr = curr->next;
    }

    if (best_prev == 0) {
        task_head = best->next;
    } else {
        best_prev->next = best->next;
    }

    if (task_tail == best) {
        task_tail = best_prev;
    }

    best->next = 0;
    return best;
}
/*
 * Return the highest priority value in the task queue.
 *
 * If the queue is empty, return -1.
 */
int highest_task_priority(void) {
    if (task_head == 0) {
        return -1;
    }

    int best = task_head->priority;
    struct task* curr = task_head->next;

    while (curr != 0) {
        if (curr->priority > best) {
            best = curr->priority;
        }

        curr = curr->next;
    }

    return best;
}
/*
 * Run pending tasks in priority order.
 *
 * A task only runs if its priority is higher than the currently running
 * task priority. This prevents lower or equal priority tasks from
 * interrupting a higher priority task.
 *
 * After a task finishes, its task node is freed.
 */
void run_task_queue(void) {
    while (task_head != 0 && highest_task_priority() > current_task_priority) {
        struct task* t = pop_highest_priority_task();

        if (t == 0) {
            break;
        }

        int prev_priority = current_task_priority;
        current_task_priority = t->priority;

        t->callback(t->arg);

        current_task_priority = prev_priority;
        free(t);
    }
}
/*
 * Timer callback used for kernel preemption.
 *
 * It sets need_resched to 1 so the trap handler knows that the scheduler
 * should run. Then it registers itself again, so preemption happens
 * periodically.
 */
void preempt_callback(void* arg) {
    need_resched = 1;
    add_timer_tick(preempt_callback, 0, TIMEBASE_FREQUENCY / 32);
}
/*
 * Timer callback used to print a delayed message.
 *
 * The argument is treated as a string. After printing the message,
 * the allocated message buffer is freed.
 */
void print_message_callback(void* arg) {
    char* msg = (char*)arg;
    uart_puts(msg);
    uart_puts("\n");
    free(arg);
}
/*
 * Initialize the timer system.
 *
 * It records the boot time and starts the periodic preemption timer.
 * The preemption callback will continue to re-register itself.
 */
void timer_init(void) {
    BOOT_TIME = get_time();
    add_timer_tick(preempt_callback, 0, TIMEBASE_FREQUENCY / 32);
}
/*
 * Main trap handler.
 *
 * This function handles:
 * - system calls from user mode
 * - timer interrupts
 * - UART external interrupts
 * - unexpected exceptions
 *
 * regs points to the trap frame saved by start.S.
 */
void do_trap(struct trap_frame* regs) {
    /*
     * scause highest bit tells whether this trap is an interrupt.
     * The lower bits contain the interrupt/exception cause number.
     */
    unsigned long is_interrupt = regs->scause >> 63;
    unsigned long cause = regs->scause & 0xff;
    // Exception handling.
    if (!is_interrupt) {
        /*
         * cause == 8 means environment call from U-mode.
         * This is how user programs enter the kernel to request syscalls.
         */
        if (cause == 8) {
            syscall_handler(regs);
            // Move sepc to the next instruction after ecall.Otherwise, returning to user mode would execute ecall again.
            regs->sepc += 4;
            // Before returning to user mode, check whether this process has a pending signal to handle.
            handle_pending_signal(regs);
            return;
        }
        /*
         * Other exceptions are unexpected.
         * Print debug information and stop the kernel.
         */
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
        while (1) {
        }
    }
    // Interrupt handling.
    if (cause == 1) {
        // cause == 1 means supervisor software interrupt.
    }
    else if (cause == 5) {
        // cause == 5 means supervisor timer interrupt.
        unsigned long now = get_time();
        /*
         * Move all expired timer events into the task queue.
         * Timer callbacks are not executed directly here.
         * Instead, they are added as deferred tasks.
         */
        while (timer_head != 0 && timer_head->expire_time <= now) {
            struct timer_event* ev = pop_head_timer_event();
            add_task(ev->callback, ev->arg, 5);
            free(ev);
        }
        // Program the next hardware timer interrupt.
        if (timer_head != 0) {
            sbi_set_timer(timer_head->expire_time);
        }
        /*
         * Run deferred tasks.
         * Timer interrupt is disabled while running task queue to avoid
         * nested timer interrupts, but global interrupts are temporarily
         * enabled so higher-priority interrupts can still be handled.
         */
        disable_timer_interrupt();
        irq_enable();
        run_task_queue();
        irq_disable();
        enable_timer_interrupt();
        // If preemption was requested, wake up sleeping tasks and run scheduler.
        if (need_resched) {
            need_resched = 0;
            wakeup_sleeping_tasks();
            schedule();
        }
        // Check pending signal before returning to user mode.
        handle_pending_signal(regs);
    }
    else if (cause == 9) {
        // cause == 9 means supervisor external interrupt.UART interrupt comes through the PLIC.
        // Claim the pending IRQ from PLIC.
        int irq = plic_claim(BOOT_HARTID);
        /*
         * If the interrupt is from UART, read one character from UART
         * receive buffer and push it into the software ring buffer.
         */
        if (irq == UART_IRQ) {
            char c = (char)(*uart_rbr_addr());
            uart_buf_push(c);
        }
        // Tell PLIC this IRQ has been handled.
        if (irq) {
            plic_complete(irq, BOOT_HARTID);
        }
        // Run any pending deferred tasks.
        run_task_queue();
    }
    else {
    }
}
