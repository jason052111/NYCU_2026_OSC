extern void uart_puts(const char* s);
extern void add_timer(void (*callback)(void*), void* arg, int sec);
extern void add_task(void (*callback)(void*), void* arg, int priority);

int priority_set[4];

void p1_callback(){
    uart_puts("P1 start\n");
    uart_puts("P1 end\n");
}

void p3_callback(){
    uart_puts("P3 start\n");
    add_task(p1_callback, 0, priority_set[0]);
    add_timer(0, 0, 0);
    uart_puts("P3 end\n");
}

void p2_callback(){
    uart_puts("P2 start\n");
    add_task(p3_callback, 0, priority_set[2]);
    add_timer(0, 0, 0);
    uart_puts("P2 end\n");
}

void p4_callback(){
    uart_puts("P4 start\n");
    add_task(p2_callback, 0, priority_set[1]);
    add_timer(0, 0, 0);
    uart_puts("P4 end\n");
}

void test_func(){
    int from_small_to_big = 1; // set to 0 if the task with a smaller number has a higher priority
    if(from_small_to_big){
        priority_set[0] = 10;
        priority_set[1] = 20;
        priority_set[2] = 30;
        priority_set[3] = 40;
    }else{
        priority_set[0] = 40;
        priority_set[1] = 30;
        priority_set[2] = 20;
        priority_set[3] = 10;
    }

    add_task(p4_callback, 0, priority_set[3]);
}

void main(){
  add_timer(test_func, 0, 0);
}