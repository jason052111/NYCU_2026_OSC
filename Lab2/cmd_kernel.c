extern const char* get_argument(const char *input);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, int n);
extern long sbi_get_spec_version(void);
extern long sbi_get_impl_id(void);
extern long sbi_get_impl_version(void);
extern void initrd_list(const void* rd);
extern void initrd_cat(const void* rd, const char* filename);
extern unsigned long initrd_address(unsigned long dtb_addr);

void prompt(void) {
    uart_puts("opi-rv2> ");
}

void cmd_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help  - show all commands.\n");
    uart_puts("  hello - print Hello World.\n");
    uart_puts("  info  - print system info.\n");
    uart_puts("  ls    - list all files.\n");
    uart_puts("  cat   - show file data.\n");
}

void cmd_hello(void) {
    uart_puts("Hello world.\n");
}

void cmd_info(void) {
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

void cmd_ls(unsigned long boot_dtb) {
    initrd_list((const void*)initrd_address(boot_dtb));
}

void cmd_cat(unsigned long boot_dtb, const char* filename) {
    initrd_cat((const void*)initrd_address(boot_dtb), filename);
}

void run_command(const char* cmd, unsigned long boot_hartid, unsigned long boot_dtb) {
    if (cmd[0] == '\0') return;

    const char *arg = get_argument(cmd);

    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "hello") == 0) cmd_hello();
    else if (strcmp(cmd, "info") == 0) cmd_info();
    else if (strcmp(cmd, "ls") == 0) cmd_ls(boot_dtb);
    else if (strncmp(cmd, "cat", 3) == 0) cmd_cat(boot_dtb, arg);
    else {
        uart_puts("Unknown command: ");
        uart_puts(cmd);
        uart_puts("\n");
    }
}