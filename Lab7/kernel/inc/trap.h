#ifndef TRAP_H
#define TRAP_H

struct trap_frame {
    // Integer registers
    unsigned long ra;   // x1
    unsigned long sp;   // x2
    unsigned long gp;   // x3
    unsigned long tp;   // x4
    unsigned long t0;   // x5
    unsigned long t1;   // x6
    unsigned long t2;   // x7
    unsigned long s0;   // x8
    unsigned long s1;   // x9
    unsigned long a0;   // x10
    unsigned long a1;   // x11
    unsigned long a2;   // x12
    unsigned long a3;   // x13
    unsigned long a4;   // x14
    unsigned long a5;   // x15
    unsigned long a6;   // x16
    unsigned long a7;   // x17
    unsigned long s2;   // x18
    unsigned long s3;   // x19
    unsigned long s4;   // x20
    unsigned long s5;   // x21
    unsigned long s6;   // x22
    unsigned long s7;   // x23
    unsigned long s8;   // x24
    unsigned long s9;   // x25
    unsigned long s10;  // x26
    unsigned long s11;  // x27
    unsigned long t3;   // x28
    unsigned long t4;   // x29
    unsigned long t5;   // x30
    unsigned long t6;   // x31

    // Saved supervisor registers
    unsigned long sepc;
    unsigned long sstatus;
    unsigned long scause;
    unsigned long stval;
};

struct timer_event {
    unsigned long expire_time;
    void (*callback)(void*);
    void* arg;
    struct timer_event* next;
};

struct task {
    int priority;
    void (*callback)(void*);
    void* arg;
    struct task* next;
};

extern unsigned long TIMEBASE_FREQUENCY;
extern unsigned long TIMER_INTERVAL;
extern unsigned long BOOT_TIME;

extern int current_task_priority;
extern int need_resched;

extern struct timer_event* timer_head;
extern struct task* task_head;
extern struct task* task_tail;

unsigned long get_time(void);
void print_boot_time(void);
void irq_enable(void);
void irq_disable(void);
void enable_software_interrupt(void);
void disable_software_interrupt(void);
void enable_timer_interrupt(void);
void disable_timer_interrupt(void);
void enable_external_interrupt(void);
void set_time_base_freq(unsigned long dtb_addr);
void add_timer_tick(void (*callback)(void*), void* arg, unsigned long ticks);
void add_timer(void (*callback)(void*), void* arg, int sec);
struct timer_event* pop_head_timer_event(void);
void add_task(void (*callback)(void*), void* arg, int priority);
struct task* pop_highest_priority_task(void);
int highest_task_priority(void);
void run_task_queue(void);
void preempt_callback(void* arg);
void print_message_callback(void* arg);
void timer_init(void);
void do_trap(struct trap_frame* regs);

#endif