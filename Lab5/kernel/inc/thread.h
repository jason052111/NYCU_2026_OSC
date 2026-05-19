#include "trap.h"

#ifndef THREAD_H
#define THREAD_H

#include "trap.h"
#include "config.h"
// Task states.
#define TASK_RUNNING  0
#define TASK_RUNNABLE 1
#define TASK_WAITING  2
#define TASK_ZOMBIE   3
#define TASK_SLEEPING 4
// Task types.
#define TASK_KERNEL_THREAD 0
#define TASK_USER_PROCESS  1
// Saved kernel context for context switching.
struct thread_struct {
    unsigned long ra;
    unsigned long sp;
    unsigned long s[12];   // s0 ~ s11
};
/*
 * Main task/process structure.
 *
 * Kernel threads only use kernel stack and thread context.
 * User processes additionally use user stack and trap frame.
 */
struct task_struct {
    struct thread_struct thread;

    int pid;
    int state;
    int type;
    void (*entry)(void);
    // Kernel stack.
    unsigned long stack;
    unsigned long kernel_sp;
    // User process context.
    unsigned long user_stack;
    unsigned long user_sp;
    struct trap_frame* trap_frame;
    // Process exit/wait state.
    int exit_status;
    int wait_pid;
    // Sleeping state.
    unsigned long wakeup_time;
    // Signal handling state.
    int signal_pending;
    int signal_number;
    unsigned long signal_handler;
    int handling_signal;
    struct trap_frame signal_saved_context;
    unsigned long signal_stack;
    // Parent-child relationship.
    struct task_struct* parent;
    struct task_struct* children;
    struct task_struct* sibling;
    // Run queue link.
    struct task_struct* next;
};

struct task_struct* get_current(void);
struct task_struct* kthread_create(void (*threadfn)(void));

void schedule(void);
void thread_exit(void);
void thread_init(void);
void idle(void);
void thread_trampoline(void);

int user_process_create(const char* filename);
long process_exec(const char* filename, struct trap_frame* regs);
long process_fork(struct trap_frame* regs);
long process_waitpid(int pid);
void process_exit(int status);
long process_stop(int pid);

long process_usleep(unsigned long usec);
void wakeup_sleeping_tasks(void);

long process_signal(int signum, void (*handler)(void));
long process_sigreturn(struct trap_frame* regs);
long process_kill(int pid, int signum);
void handle_pending_signal(struct trap_frame* regs);

#endif