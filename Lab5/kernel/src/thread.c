#include "thread.h"
#include "tool.h"
#include "uart.h"
#include "buddy.h"
#include "initrd.h"
#include "trap.h"
// Assembly functions.
extern void switch_to(struct task_struct* prev, struct task_struct* next);
extern void ret_to_user(void);
// Private scheduler state.These are only used inside thread.c.
static int nr_threads = 0;
static struct task_struct* run_queue = 0;
/*
 * Temporarily disable timer interrupts while printing UART logs.
 * This prevents timer preemption from interrupting debug output.
 */
static void uart_lock(void) {
    disable_timer_interrupt();
}
/*
 * Re-enable timer interrupts after UART logging is finished.
 */
static void uart_unlock(void) {
    enable_timer_interrupt();
}
/*
 * Search the run queue for a task with the given pid.
 * Returns the task pointer if found, otherwise returns 0.
 */
static struct task_struct* find_task_by_pid(int pid) {
    if (run_queue == 0) return 0;

    struct task_struct* task = run_queue;
    do {
        if (task->pid == pid) return task;
        task = task->next;
    } while (task != run_queue);

    return 0;
}
/*
 * Add a child process to the parent's children list.
 * The child is inserted at the front of the list.
 */
static void add_child(struct task_struct* parent, struct task_struct* child) {
    if (parent == 0 || child == 0) return;

    child->parent = parent;
    child->sibling = parent->children;
    parent->children = child;
}
/*
 * Check whether child is in parent's children list.
 * Returns 1 if yes, otherwise returns 0.
 */
static int is_child_of(struct task_struct* parent, struct task_struct* child) {
    struct task_struct* c = parent->children;

    while (c != 0) {
        if (c == child) return 1;

        c = c->sibling;
    }

    return 0;
}
/*
 * Remove a child process from the parent's children list.
 * This is used when waitpid() collects a zombie child.
 */
static void remove_child(struct task_struct* parent, struct task_struct* child) {
    if (parent == 0 || child == 0) return;

    struct task_struct* prev = 0;
    struct task_struct* curr = parent->children;

    while (curr != 0) {
        if (curr == child) {
            if (prev == 0) {
                parent->children = curr->sibling;
            } else {
                prev->sibling = curr->sibling;
            }

            curr->sibling = 0;
            return;
        }

        prev = curr;
        curr = curr->sibling;
    }
}
/*
 * Insert a task into a circular queue.
 * If the queue is empty, the task points to itself.
 */
static void enqueue(struct task_struct** queue, struct task_struct* task) {
    task->next = 0;

    if (*queue == 0) {
        *queue = task;
        task->next = task;
        return;
    }

    struct task_struct* head = *queue;
    struct task_struct* tail = head;

    while (tail->next != head) {
        tail = tail->next;
    }

    tail->next = task;
    task->next = head;
}
/*
 * Remove a task from the circular run queue.
 * The task itself is not freed here; this only unlinks it from the queue.
 */
static void remove_from_run_queue(struct task_struct* target) {
    if (run_queue == 0 || target == 0) return;

    struct task_struct* prev = run_queue;
    // Find the tail of the circular list.
    while (prev->next != run_queue) {
        prev = prev->next;
    }

    struct task_struct* curr = run_queue;

    do {
        if (curr == target) {
            // If this is the only task in the queue, clear run_queue.
            if (curr == curr->next) {
                run_queue = 0;
            } else {
                prev->next = curr->next;
                // If removing the head, move run_queue to the next task.
                if (curr == run_queue) {
                    run_queue = curr->next;
                }
            }

            curr->next = 0;
            return;
        }

        prev = curr;
        curr = curr->next;
    } while (curr != run_queue);
}
/*
 * Detach all children from a parent process.
 * This prevents children from keeping a pointer to a dead parent.
 */
static void orphan_children(struct task_struct* parent) {
    if (parent == 0) return;

    struct task_struct* child = parent->children;

    while (child != 0) {
        struct task_struct* next = child->sibling;
        child->parent = 0;
        child->sibling = 0;
        child = next;
    }

    parent->children = 0;
}
/*
 * Free zombie tasks that no longer need to be waited for.
 * User processes with a parent are not freed here, because their parent
 * should collect them through waitpid(). Kernel threads or orphaned user
 * processes can be safely removed and freed here.
 */
static void kill_zombies(void) {
    struct task_struct* current = get_current();

    if (run_queue == 0) return;

    struct task_struct* prev = run_queue;

    while (prev->next != run_queue) {
        prev = prev->next;
    }

    struct task_struct* task = run_queue;

    do {
        struct task_struct* next = task->next;
        if (task != current && task->state == TASK_ZOMBIE) {
            // Do not free the currently running task.
            if (task->type == TASK_USER_PROCESS && task->parent != 0) {
                // If this zombie is a user process and still has a parent,keep it until the parent calls waitpid().
                prev = task;
                task = next;
                continue;
            }
            // Remove the zombie task from the circular run queue.
            prev->next = next;

            if (task == run_queue) run_queue = next;
            if (task == next) run_queue = 0;

            uart_lock();
            uart_puts("[kill_zombies] free pid: ");
            uart_hex(task->pid);
            uart_puts("\n");
            uart_unlock();
            // Free resources owned by the task.
            if (task->signal_stack != 0) free((void*)task->signal_stack);
            if (task->user_stack != 0) free((void*)task->user_stack);
            if (task->stack != 0) free((void*)task->stack);

            free(task);

            task = next;

            if (run_queue == 0) {
                return;
            }
        } else {
            prev = task;
            task = next;
        }

    } while (task != run_queue);
}

// ================================================================================================== //

/*
 * Get the currently running task.
 *
 * The tp register is used to store the pointer to the current task_struct.
 * switch_to() updates tp when switching to another task.
 */
struct task_struct* get_current(void) {
    register struct task_struct* current asm("tp");
    return current;
}
/*
 * Create a kernel thread.
 *
 * A kernel thread only runs in S-mode, so it does not need a user stack
 * or trap frame. It only needs:
 * - task_struct
 * - kernel stack
 * - thread context for switch_to()
 */
struct task_struct* kthread_create(void (*threadfn)(void)) {
    // Allocate task control block.
    struct task_struct* task = (struct task_struct*)allocate(sizeof(struct task_struct));

    if (task == 0) {
        uart_puts("kthread_create: allocate task failed\n");
        return 0;
    }
    // Initialize basic task information.
    task->pid = nr_threads++;
    task->state = TASK_RUNNABLE;
    task->type = TASK_KERNEL_THREAD;
    task->entry = threadfn;
    // Allocate kernel stack.
    task->stack = (unsigned long)allocate(KERNEL_STACK_SIZE);

    if (task->stack == 0) {
        uart_puts("kthread_create: allocate stack failed\n");
        free(task);
        return 0;
    }
    // Stack grows downward, so the initial stack pointer is placed at the top of the allocated stack.
    task->kernel_sp = task->stack + KERNEL_STACK_SIZE;
    // Kernel threads do not have user stack or trap frame.
    task->user_stack = 0;
    task->user_sp = 0;
    task->trap_frame = 0;
    // Initialize process-related fields.
    task->exit_status = 0;
    task->wait_pid = -1;
    task->wakeup_time = 0;
    /*
     * Initialize signal-related fields.
     * Kernel threads do not handle user signals, but these fields
     * are still cleared for safety.
     */
    task->signal_pending = 0;
    task->signal_number = 0;
    task->signal_handler = 0;
    task->handling_signal = 0;
    task->signal_stack = 0;
    // Kernel threads are not part of the user process parent-child tree.
    task->parent = 0;
    task->children = 0;
    task->sibling = 0;
    /*
     * Set initial context for switch_to().
     * When this thread is scheduled for the first time:
     * switch_to() restores ra/sp,
     * ret jumps to thread_trampoline,
     * and thread_trampoline calls task->entry().
     */
    task->thread.ra = (unsigned long)thread_trampoline;
    task->thread.sp = task->kernel_sp;
    // Clear callee-saved registers s0 ~ s11.
    for (int i = 0; i < 12; i++) {
        task->thread.s[i] = 0;
    }
    // Add this kernel thread to the run queue.
    enqueue(&run_queue, task);

    return task;
}
/*
 * Pick the next runnable task and switch to it.
 * The run queue is a circular linked list. This scheduler performs
 * simple round-robin scheduling by looking for the next runnable task.
 */
void schedule(void) {
    struct task_struct* current = get_current();

    if (current == 0) return;
    // Start from the task after the current one.
    struct task_struct* next = current->next;
    // Skip tasks that are not runnable.
    while (next->state != TASK_RUNNABLE && next->state != TASK_RUNNING) {
        next = next->next;
        // If we loop back to current, no other runnable task exists.
        if (next == current) return;
    }
    // No need to switch if the selected task is still current.
    if (current == next) return;
    // Mark the current task as runnable again before switching out.
    if (current->state == TASK_RUNNING) current->state = TASK_RUNNABLE;
    // Mark the next task as running.
    next->state = TASK_RUNNING;
    // Save current kernel context and restore next kernel context.
    switch_to(current, next);
}
/*
 * Exit the current kernel thread.
 * The thread is marked as ZOMBIE and then the scheduler is called
 * to switch to another runnable task. It should never continue running
 * after schedule(), so it stays in an infinite loop as a safety guard.
 */
void thread_exit(void) {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE;
    schedule();

    while (1) {
    }
}
/*
 * Initialize the threading system.
 * This creates the idle kernel thread and sets it as the first running task.
 * The tp register is initialized to point to the idle task, so get_current()
 * can work correctly after boot.
 */
void thread_init(void) {
    struct task_struct* idle_task = kthread_create(idle);

    if (idle_task == 0) {
        uart_puts("thread_init: create idle failed\n");
        while (1) {
        }
    }

    idle_task->state = TASK_RUNNING;
    // Store the current task pointer in tp.
    asm volatile("mv tp, %0" : : "r"(idle_task));
}
/*
 * Idle kernel thread.
 * This thread runs when no other task is ready.
 * It also cleans up zombie tasks that are safe to free.
 * wfi lets the CPU wait until the next interrupt.
 */
void idle(void) {
    while (1) {
        kill_zombies();
        asm volatile("wfi");
    }
}
/*
 * Entry point for newly created kernel threads.
 * switch_to() restores this thread's context and returns into
 * thread_trampoline(). Then it enables interrupts and calls the thread's
 * entry function. When the entry function returns, the thread exits.
 */
void thread_trampoline(void) {
    struct task_struct* current = get_current();
    irq_enable();
    current->entry();
    thread_exit();
}
/*
 * Create a new user process from a program file.
 * This function loads the program from initramfs, allocates a task_struct,
 * kernel stack, user stack, and an initial trap frame. The trap frame is
 * prepared so that when the scheduler first runs this task, it will enter
 * user mode through ret_to_user.
 */
int user_process_create(const char* filename) {
    unsigned long prog_size = 0;
    // Load the user program into memory.
    void* prog = load_user_program(filename, &prog_size);

    if (prog == 0) {
        uart_puts("user_process_create: load failed\n");
        return -1;
    }
    // Allocate task control block.
    struct task_struct* task = (struct task_struct*)allocate(sizeof(struct task_struct));

    if (task == 0) {
        uart_puts("user_process_create: allocate task failed\n");
        return -1;
    }
    // Initialize basic process information.
    task->pid = nr_threads++;
    task->state = TASK_RUNNABLE;
    task->type = TASK_USER_PROCESS;
    task->entry = 0;
    // Allocate kernel stack.This stack is used when the user process traps into kernel mode.
    task->stack = (unsigned long)allocate(KERNEL_STACK_SIZE);

    if (task->stack == 0) {
        free(task);
        return -1;
    }

    task->kernel_sp = task->stack + KERNEL_STACK_SIZE;
    // Allocate user stack.This stack is used when the process runs in user mode.
    task->user_stack = (unsigned long)allocate(USER_STACK_SIZE);

    if (task->user_stack == 0) {
        free((void*)task->stack);
        free(task);
        return -1;
    }

    task->user_sp = task->user_stack + USER_STACK_SIZE;
    // Place the initial trap frame at the top of the kernel stack.This fake trap frame is used to enter user mode for the first time.
    task->trap_frame = (struct trap_frame*)(task->kernel_sp - sizeof(struct trap_frame));

    mem_zero(task->trap_frame, sizeof(struct trap_frame));
    /*
     * Set initial user-mode context.
     * sepc    = user program entry
     * sp      = user stack pointer
     * tp      = current task pointer
     * sstatus = SPIE set, SPP clear, so sret returns to U-mode
     */
    task->trap_frame->sepc = (unsigned long)prog;
    task->trap_frame->sp = task->user_sp;
    task->trap_frame->tp = (unsigned long)task;
    task->trap_frame->sstatus = (1 << 5);
    /*
     * Set initial kernel context for switch_to().
     * When scheduled for the first time:
     * switch_to() restores ra/sp,
     * ret jumps to ret_to_user,
     * ret_to_user restores trap_frame,
     * then sret enters user mode.
     */
    task->thread.ra = (unsigned long)ret_to_user;
    task->thread.sp = (unsigned long)task->trap_frame;
    // Clear callee-saved registers s0 ~ s11.
    for (int i = 0; i < 12; i++) {
        task->thread.s[i] = 0;
    }
    // Initialize wait/exit/sleep state.
    task->exit_status = 0;
    task->wait_pid = -1;
    task->wakeup_time = 0;
    // Initialize signal state.
    task->signal_pending = 0;
    task->signal_number = 0;
    task->signal_handler = 0;
    task->handling_signal = 0;
    task->signal_stack = 0;
    // Set parent-child relationship.If this process is created by the kernel shell, parent is usually idle.
    task->parent = get_current();
    task->children = 0;
    task->sibling = 0;

    if (task->parent != 0) {
        add_child(task->parent, task);
    }

    enqueue(&run_queue, task);

    return task->pid;
}
/*
 * Replace the current user process with a new user program.
 * This does not create a new process or change the PID.
 * It loads the target program, clears the old user stack,
 * resets the trap frame, and makes the process return to
 * the new program entry after syscall handling finishes.
 */
long process_exec(const char* filename, struct trap_frame* regs) {
    struct task_struct* current = get_current();
    // The current task and trap frame must both exist.
    if (current == 0 || regs == 0) return -1;
    // Only user processes are allowed to exec.
    if (current->type != TASK_USER_PROCESS) return -1;
    // The filename must be valid.
    if (filename == 0 || filename[0] == '\0') return -1;
    // Load the new user program from initramfs.
    unsigned long prog_size = 0;
    void* prog = load_user_program(filename, &prog_size);

    if (prog == 0) return -1;
    // The process must already have a user stack.
    if (current->user_stack == 0) return -1;
    // Clear the old user stack so the new program starts cleanly.
    mem_zero((void*)current->user_stack, USER_STACK_SIZE);
    // Reset user stack pointer to the top of the user stack.
    current->user_sp = current->user_stack + USER_STACK_SIZE;
    // Clear the old trap frame so the new program does not inherit old registers from the previous program.
    mem_zero(regs, sizeof(struct trap_frame));
    /*
     * Set the new user context.
     * do_trap() will add sepc += 4 after syscall_handler().
     * Therefore, set sepc to prog - 4 here, so after do_trap()
     * increments it, the final sepc becomes prog.
     */
    regs->sepc = (unsigned long)prog - 4;      
    regs->sp = current->user_sp;            
    regs->tp = (unsigned long)current;     
    /*
     * SPP = 0: return to U-mode.
     * SPIE = 1: enable interrupts after sret.
     */
    regs->sstatus = (1 << 5);
    // exec success return value.In practice, the process will jump to the new program.
    regs->a0 = 0;
    // Clear signal state because exec starts a new program image.
    current->signal_pending = 0;
    current->signal_number = 0;
    current->signal_handler = 0;
    current->handling_signal = 0;
    // Free any existing signal stack.
    if (current->signal_stack != 0) {
        free((void*)current->signal_stack);
        current->signal_stack = 0;
    }

    return 0;
}
/*
 * Duplicate the current user process.
 * The child gets its own task_struct, kernel stack, user stack,
 * and trap frame. The user stack and trap frame are copied from
 * the parent so the child continues from the same execution point.
 */
long process_fork(struct trap_frame* regs) {
    struct task_struct* parent = get_current();

    if (parent == 0 || regs == 0) return -1;
    // Allocate child task control block.
    struct task_struct* child = (struct task_struct*)allocate(sizeof(struct task_struct));

    if (child == 0) return -1;
    // Initialize basic child process information.
    child->pid = nr_threads++;
    child->state = TASK_RUNNABLE;
    child->type = TASK_USER_PROCESS;
    child->entry = 0;
    // Allocate child kernel stack.
    child->stack = (unsigned long)allocate(KERNEL_STACK_SIZE);

    if (child->stack == 0) {
        free(child);
        return -1;
    }

    child->kernel_sp = child->stack + KERNEL_STACK_SIZE;
    // Allocate child user stack.
    child->user_stack = (unsigned long)allocate(USER_STACK_SIZE);

    if (child->user_stack == 0) {
        free((void*)child->stack);
        free(child);
        return -1;
    }

    child->user_sp = child->user_stack + USER_STACK_SIZE;
    // Copy the entire parent user stack to the child.
    mem_copy((void*)child->user_stack, (void*)parent->user_stack, USER_STACK_SIZE);
    // Place the child trap frame on the child kernel stack.
    child->trap_frame =(struct trap_frame*)(child->kernel_sp - sizeof(struct trap_frame));
    // Copy the parent's current trap frame.
    mem_copy(child->trap_frame, regs, sizeof(struct trap_frame));
    // The child should return to the instruction after ecall.
    child->trap_frame->sepc += 4;
    // Update tp so get_current() in the child points to the child task.
    child->trap_frame->tp = (unsigned long)child;
    // Adjust the child's user sp to point into its own copied user stack.
    unsigned long sp_offset = regs->sp - parent->user_stack;
    child->trap_frame->sp = child->user_stack + sp_offset;
    // fork return value: parent gets child pid, child gets 0.
    child->trap_frame->a0 = 0;
    /*
     * Set initial kernel context.
     * When scheduled, the child will enter ret_to_user and restore
     * its copied trap frame.
     */
    child->thread.ra = (unsigned long)ret_to_user;
    child->thread.sp = (unsigned long)child->trap_frame;
    // Clear callee-saved registers for the child kernel context.
    for (int i = 0; i < 12; i++) {
        child->thread.s[i] = 0;
    }
    // Initialize child process state.
    child->exit_status = 0;
    child->wait_pid = -1;
    child->wakeup_time = 0;
    // Inherit signal handler from parent, but not pending/handling state.
    child->signal_pending = 0;
    child->signal_number = 0;
    child->signal_handler = parent->signal_handler;
    child->handling_signal = 0;
    child->signal_stack = 0;
    // Link child to parent.
    child->parent = parent;
    child->children = 0;
    child->sibling = 0;

    add_child(parent, child);
    // Add child to run queue so it can be scheduled.
    enqueue(&run_queue, child);
    // Parent receives child pid as fork return value.
    return child->pid;
}
/*
 * Wait for a child process to exit.
 * If pid == -1, wait for any child.
 * If pid is specific, wait for that child only.
 * When the child becomes ZOMBIE, this function removes it from
 * the child list and run queue, frees its resources, and returns its pid.
 */
long process_waitpid(int pid) {
    struct task_struct* current = get_current();

    if (current == 0) return -1;
    while (1) {
        struct task_struct* child = current->children;
        // Search for a zombie child that matches pid.
        while (child != 0) {
            if ((pid == -1 || child->pid == pid) &&
                child->state == TASK_ZOMBIE) {
                int child_pid = child->pid;
                // Remove child from parent-child tree and run queue.
                remove_child(current, child);
                remove_from_run_queue(child);
                
                uart_puts("[waitpid] free pid: ");
                uart_hex(child_pid);
                uart_puts("\n");
                // Free child resources.
                if (child->stack != 0) free((void*)child->stack);
                if (child->user_stack != 0) free((void*)child->user_stack);

                free(child);
                return child_pid;
            }

            child = child->sibling;
        }
        /*
         * If waiting for a specific pid, verify that the target exists
         * and is really this process's child.
         */
        if (pid != -1) {
            struct task_struct* target = find_task_by_pid(pid);

            if (target == 0 || !is_child_of(current, target)) {
                return -1;
            }
        }
        /*
         * No matching zombie child yet.
         * Block current process and let scheduler run another task.
         */
        current->wait_pid = pid;
        current->state = TASK_WAITING;

        schedule();
    }
}
/*
 * Exit the current user process.
 * The process becomes ZOMBIE and stays in the run queue until its
 * parent collects it with waitpid(), or until it becomes orphaned
 * and is cleaned by kill_zombies().
 */
void process_exit(int status) {
    struct task_struct* current = get_current();

    if (current == 0) {
        while (1) {
        }
    }
    // Save exit status and mark process as zombie.
    current->exit_status = status;
    current->state = TASK_ZOMBIE;
    // Detach all children from this process.
    orphan_children(current);
    // Wake parent if it is waiting for this process.
    if (current->parent != 0 &&
        current->parent->state == TASK_WAITING) {
        if (current->parent->wait_pid == -1 ||
            current->parent->wait_pid == current->pid) {
            current->parent->state = TASK_RUNNABLE;
        }
    }

    schedule();
    while (1) {
    }
}
/*
 * Force another process to stop.
 * This is similar to killing a process directly. The target is marked
 * as ZOMBIE, its children are orphaned, and its waiting parent is woken up.
 */
long process_stop(int pid) {
    struct task_struct* target = find_task_by_pid(pid);

    if (target == 0) return -1;
    // Do not allow a process to stop itself through stop().
    if (target == get_current()) return -1;
    if (target->state == TASK_ZOMBIE) return -1;
    uart_lock();
    uart_puts("[stop] target pid = ");
    uart_hex(target->pid);
    uart_puts("\n");
    uart_unlock();
    // Mark target as killed/zombie.
    target->exit_status = -1;
    target->state = TASK_ZOMBIE;
    // Detach target's children.
    orphan_children(target);
    // Wake parent if it is waiting for this target.
    if (target->parent != 0 &&
        target->parent->state == TASK_WAITING) {
        if (target->parent->wait_pid == -1 ||
            target->parent->wait_pid == target->pid) {
            target->parent->state = TASK_RUNNABLE;
        }
    }

    return 0;
}
/*
 * Put the current process to sleep for a given number of microseconds.
 * The sleep duration is converted into timer ticks using TIMEBASE_FREQUENCY.
 * The process is marked as TASK_SLEEPING and will be woken up by
 * wakeup_sleeping_tasks() after the target time is reached.
 */
long process_usleep(unsigned long usec) {
    struct task_struct* current = get_current();

    if (current == 0) return -1;
    // Convert microseconds to timer ticks.
    unsigned long ticks = (TIMEBASE_FREQUENCY * usec) / 1000000;
    // Ensure at least one tick of sleep.
    if (ticks == 0) ticks = 1;
    // Record wakeup time and block current process.
    current->wakeup_time = get_time() + ticks;
    current->state = TASK_SLEEPING;

    schedule();
    return 0;
}
/*
 * Wake up sleeping tasks whose wakeup time has already arrived.
 * This function scans the run queue. If a task is sleeping and its
 * wakeup_time is less than or equal to the current time, the task is
 * marked runnable again.
 */
void wakeup_sleeping_tasks(void) {
    if (run_queue == 0) return;

    unsigned long now = get_time();
    struct task_struct* task = run_queue;

    do {
        if (task->state == TASK_SLEEPING &&
            task->wakeup_time <= now) {
            task->state = TASK_RUNNABLE;
            task->wakeup_time = 0;
        }

        task = task->next;
    } while (task != run_queue);
}
/*
 * Register a signal handler for the current user process.
 * signum is the signal number.
 * handler is the user-space function that should be executed when
 * the signal is delivered.
 */
long process_signal(int signum, void (*handler)(void)) {
    struct task_struct* current = get_current();

    if (current == 0 || current->type != TASK_USER_PROCESS) return -1;

    if (handler == 0) return -1;

    current->signal_handler = (unsigned long)handler;
    current->signal_number = signum;

    return 0;
}
/*
 * Return from a signal handler.
 * This restores the original user context that was saved before
 * the signal handler ran. It also frees the signal stack.
 */
long process_sigreturn(struct trap_frame* regs) {
    struct task_struct* current = get_current();

    if (current == 0 || regs == 0) return -1;
    if (!current->handling_signal) return -1;

    uart_lock();
    uart_puts("[sigreturn] pid = ");
    uart_hex(current->pid);
    uart_puts("\n");
    uart_unlock();
    // Free the temporary signal stack.
    if (current->signal_stack != 0) {
        free((void*)current->signal_stack);
        current->signal_stack = 0;
    }
    // Restore the user context saved before signal handling.
    mem_copy(regs, &current->signal_saved_context, sizeof(struct trap_frame));

    current->handling_signal = 0;
    /*
     * do_trap() will add sepc += 4 after syscall handling.
     * Subtract 4 here so the final sepc returns to the original value.
     */
    regs->sepc -= 4;

    return 0;
}
/*
 * Send a signal to a user process.
 * If the target process has no registered handler, the default action
 * is to terminate it. If it has a handler, mark the signal as pending
 * so it will be handled before returning to user mode.
 */
long process_kill(int pid, int signum) {
    struct task_struct* target = find_task_by_pid(pid);

    if (target == 0) return -1;

    if (target->type != TASK_USER_PROCESS) return -1;

    if (target->state == TASK_ZOMBIE) return -1;
    // No handler registered: default action is to terminate the target process.
    if (target->signal_handler == 0) {
        uart_puts("[kill] no handler, terminate pid = ");
        uart_hex(target->pid);
        uart_puts("\n");

        target->exit_status = -1;
        target->state = TASK_ZOMBIE;

        orphan_children(target);
        // Wake the parent if it is waiting for this process.
        if (target->parent != 0 && target->parent->state == TASK_WAITING) {
            if (target->parent->wait_pid == -1 ||
                target->parent->wait_pid == target->pid) {
                target->parent->state = TASK_RUNNABLE;
            }
        }

        return 0;
    }
    // Handler exists: mark the signal as pending.
    uart_puts("[kill] send signal to pid = ");
    uart_hex(target->pid);
    uart_puts("\n");

    target->signal_pending = 1;
    target->signal_number = signum;
    // If the target is sleeping, wake it up so it can handle the signal.
    if (target->state == TASK_SLEEPING) {
        target->state = TASK_RUNNABLE;
        target->wakeup_time = 0;
    }

    return 0;
}
/*
 * Deliver a pending signal before returning to user mode.
 * This function saves the current user context, creates a temporary
 * signal stack, writes a trampoline on that stack, and modifies the
 * trap frame so the process runs the signal handler first.
 */
void handle_pending_signal(struct trap_frame* regs) {
    struct task_struct* current = get_current();

    if (current == 0 || regs == 0) return;

    if (current->type != TASK_USER_PROCESS) return;
    /*
     * If SPP is set, the trap will return to S-mode, not U-mode.
     * Signals should only be delivered before returning to user mode.
     */
    if (regs->sstatus & (1 << 8)) return;

    if (!current->signal_pending) return;

    if (current->handling_signal) return;

    if (current->signal_handler == 0) return;
    // Save the original user context before entering the signal handler.
    mem_copy(&current->signal_saved_context, regs, sizeof(struct trap_frame));
    // Allocate a temporary signal stack.
    current->signal_stack = (unsigned long)allocate(SIGNAL_STACK_SIZE);

    if (current->signal_stack == 0) return;

    unsigned long sig_sp = current->signal_stack + SIGNAL_STACK_SIZE;
    // Keep the signal stack 16-byte aligned.
    sig_sp &= ~0xfUL;
    // Reserve space for trampoline code.
    sig_sp -= 16;

    unsigned int* trampoline = (unsigned int*)sig_sp;
    /*
     * Trampoline code:
     *   addi a7, zero, 11   // syscall number: sigreturn
     *   ecall               // enter kernel
     *   j .                 // safety loop
     */
    trampoline[0] = 0x00b00893;  // addi a7, zero, 11
    trampoline[1] = 0x00000073;  // ecall
    trampoline[2] = 0x0000006f;  // j .
    /*
     * fence.i:
     * Make sure the CPU instruction fetch sees the trampoline code
     * that was just written into memory.
     */
    asm volatile(".word 0x0000100f" ::: "memory");

    current->signal_pending = 0;
    current->handling_signal = 1;
    // Return to user mode at the signal handler.
    regs->sepc = current->signal_handler;
    // When the handler returns, it will jump to trampoline.The trampoline then calls sigreturn().
    regs->ra = (unsigned long)trampoline;
    // Run the handler on the signal stack.
    regs->sp = sig_sp;
    // Pass signal number to the handler as the first argument.
    regs->a0 = current->signal_number;
}