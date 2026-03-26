extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

#define SBI_EXT_BASE      0x10
#define CMD_BUF_LEN       128

enum sbi_ext_base_fid { // defines named constants for the SBI Base Extension function IDs
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
    SBI_EXT_BASE_GET_MVENDORID,
    SBI_EXT_BASE_GET_MARCHID,
    SBI_EXT_BASE_GET_MIMPID,
};

struct sbiret {
    long error;
    long value;
};

// ext：SBI Extension ID
// fid：Function ID
struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0; // Bind this C variable to the RISC-V a0 register and initialize it with arg0
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;
    // Execute the RISC-V `ecall` to invoke SBI: a0–a5 carry arguments, a6 = fid and a7 = ext
    // a0 and a1 are marked as input/output because SBI overwrites them with (error, value) on return
    // volatile means this part cannot be removed or arbitrarily moved by the compiler
    // The "memory" clobber prevents the compiler from reordering memory accesses across this call
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

long sbi_get_spec_version(void) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE,
                                SBI_EXT_BASE_GET_SPEC_VERSION,
                                0, 0, 0, 0, 0, 0);
  return ret.value;
}

long sbi_get_impl_id(void) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE,
                                SBI_EXT_BASE_GET_IMP_ID,
                                0, 0, 0, 0, 0, 0);
  return ret.value;
}

long sbi_get_impl_version(void) {
  struct sbiret ret = sbi_ecall(SBI_EXT_BASE,
                                SBI_EXT_BASE_GET_IMP_VERSION,
                                0, 0, 0, 0, 0, 0);
  return ret.value;
}

// Compare two character arrays.
static int streq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static void prompt(void) {
    uart_puts("opi-rv2> ");
}

static void cmd_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help  - show all commands.\n");
    uart_puts("  hello - print Hello World.\n");
    uart_puts("  info  - print system info.\n");
}

static void cmd_hello(void) {
    uart_puts("Hello world.\n");
}

static void cmd_info(void) {
    uart_puts("System information: ");
    uart_puts("\n");
    uart_puts("  OpenSBI specification version: ");
    uart_hex(sbi_get_spec_version());
    uart_puts("\n");
    uart_puts("  implementation ID: ");
    uart_hex(sbi_get_impl_id());
    uart_puts("\n");
    uart_puts("  implementation version: ");
    uart_hex(sbi_get_impl_version());
    uart_puts("\n");
}

static void run_command(const char* cmd) {
    if (cmd[0] == '\0') return;

    if (streq(cmd, "help")) cmd_help();
    else if (streq(cmd, "hello")) cmd_hello();
    else if (streq(cmd, "info")) cmd_info();
    else {
        uart_puts("Unknown command: ");
        uart_puts(cmd);
        uart_puts("\n");
    }
}

void start_kernel() {
    char buf[CMD_BUF_LEN];
    int len = 0;

    prompt();

    while (1) {
        char c = uart_getc(); 

        if (c == '\n') {
            uart_putc('\n');          
            buf[len] = '\0';          
            run_command(buf);         
            len = 0;                 
            prompt();           
            continue;
        }

        if (c == 0x08 || c == 0x7f) {                 // Backspace ( some terminals send 0x08, while others send 0x7f )
            if (len > 0) {
                len--;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
            continue;
        }

        if (len < CMD_BUF_LEN - 1) {                  // Normal character: store it in the buffer and echo it back.
            buf[len++] = c;
            uart_putc(c);
        } 
    }
}