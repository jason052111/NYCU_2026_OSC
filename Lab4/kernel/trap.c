extern void uart_puts(const char* s);
extern void uart_putc(char c);
extern void uart_hex(unsigned long h);
extern void uart_int(int num);
extern void sbi_set_timer(unsigned long stime_value);
extern unsigned long get_timebase_freq(unsigned long dtb_addr);
extern void* allocate(unsigned long size);
extern void free(void* ptr);
extern int plic_claim(unsigned long hartid);
extern void plic_complete(int irq, unsigned long hartid);
extern char uart_getc(void);
extern void uart_putc(char c);
extern volatile unsigned char* uart_rbr_addr(void);
extern void uart_buf_push(char c);

#define UART_IRQ 0x2a

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

struct pt_regs {
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
    unsigned long sepc;    // 對應 31*8(sp)
    unsigned long sstatus; // 對應 32*8(sp)
    unsigned long scause;  // 對應 33*8(sp)
    unsigned long stval;   // 對應 34*8(sp)
};

unsigned long TIMEBASE_FREQUENCY = 0xFFFFFFFF;
unsigned long TIMER_INTERVAL     = 0xFFFFFFFF;
unsigned long BOOT_TIME          = 0xFFFFFFFF;
unsigned long BOOT_HARTID = 0;
int current_task_priority = -1;

struct timer_event* timer_head = 0;
struct task* task_head = 0;
struct task* task_tail = 0;

void set_boot_hartid(unsigned long hartid) {
    BOOT_HARTID = hartid;
}

unsigned long get_time(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

void print_boot_time(){
    unsigned long now = get_time();
    unsigned long sec = (now-BOOT_TIME) / TIMEBASE_FREQUENCY;

    uart_puts("boot time: ");
    uart_int((int)sec);
    uart_puts("\n");
}

void irq_enable() {
    asm volatile("csrsi sstatus, (1 << 1)");
}

void irq_disable() {
    asm volatile("csrci sstatus, (1 << 1)");
}

void enable_software_interrupt() {
    asm volatile(
        "li t0, (1 << 1);"
        "csrs sie, t0;");
}

void enable_timer_interrupt() {
    asm volatile(
        "li t0, (1 << 5);"
        "csrs sie, t0;");
}

void disable_timer_interrupt() {
    asm volatile(
        "li t0, (1 << 5);"
        "csrc sie, t0;");
}

void enable_external_interrupt() {
    asm volatile(
        "li t0, (1 << 9);"
        "csrs sie, t0;");
}

void set_time_base_freq(unsigned long dtb_addr){
    TIMEBASE_FREQUENCY = get_timebase_freq(dtb_addr);
    TIMER_INTERVAL = TIMEBASE_FREQUENCY << 1;
}

void add_timer(void (*callback)(void*), void* arg, int sec) {
    if (callback == 0 || sec < 0) {
        return;
    }

    struct timer_event* new_node = (struct timer_event*)allocate(sizeof(struct timer_event));
    if (new_node == 0) {
        return;
    }

    new_node->expire_time = get_time() + (unsigned long)sec * TIMEBASE_FREQUENCY;
    new_node->callback = callback;
    new_node->arg = arg;
    new_node->next = 0;

    if (timer_head == 0 || new_node->expire_time < timer_head->expire_time) {
        new_node->next = timer_head;
        timer_head = new_node;
        sbi_set_timer(timer_head->expire_time);
        return;
    }

    struct timer_event* curr = timer_head;
    while (curr->next != 0 && curr->next->expire_time <= new_node->expire_time) {
        curr = curr->next;
    }

    new_node->next = curr->next;
    curr->next = new_node;
}

struct timer_event* pop_head_timer_event(void) {
    if (timer_head == 0) {
        return 0;
    }

    struct timer_event* node = timer_head;
    timer_head = timer_head->next;
    node->next = 0;

    return node;
}

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

void boot_time_callback(void* arg) {
    print_boot_time();
    add_timer(boot_time_callback, 0, 2);
}

void print_message_callback(void* arg) {
    char* msg = (char*)arg;
    uart_puts(msg);
    uart_puts("\n");
    free(arg);
}

void timer_init() {
    BOOT_TIME = get_time();
    uart_puts("boot time: 0\n");
    add_timer(boot_time_callback, 0, 2);
}

void do_trap(struct pt_regs* regs) {
    unsigned long is_interrupt = regs->scause >> 63;
    unsigned long cause = regs->scause & 0xff;
    if (!is_interrupt) {
        // ===== Exception =====
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
    else {
        if (cause == 1) {
            // Supervisor software interrupt
        }
        else if (cause == 5) {
            unsigned long now = get_time();
            while (timer_head != 0 && timer_head->expire_time <= now) {
                struct timer_event* ev = pop_head_timer_event();
                add_task(ev->callback, ev->arg, 5);
                free(ev);
            }

            if (timer_head != 0) {
                sbi_set_timer(timer_head->expire_time);
            }

            disable_timer_interrupt();
            irq_enable();
            run_task_queue();
            irq_disable();
            enable_timer_interrupt();
        }
        else if (cause == 9) {
            int irq = plic_claim(BOOT_HARTID);

            if (irq == UART_IRQ) {
                char c = (char)(*uart_rbr_addr());
                uart_buf_push(c);
            }

            if (irq) {
                plic_complete(irq, BOOT_HARTID);
            }
            run_task_queue();
        }
        else {
            // Other interrupt
        }
    }
}

//An example use case
void test_task_cb(void *arg) {
    uart_puts("[Task] Executing Priority ");
    uart_puts((char*)arg);
    uart_puts("\n");
}

void test_task() {
    uart_puts("\n===================================\n");
    add_task(test_task_cb, "3", 3);
    add_task(test_task_cb, "8", 8);
    add_task(test_task_cb, "9", 9);
    add_task(test_task_cb, "2", 2);
    add_task(test_task_cb, "1", 1);
    add_task(test_task_cb, "40", 40);
    run_task_queue();
    uart_puts("===================================\n");
}