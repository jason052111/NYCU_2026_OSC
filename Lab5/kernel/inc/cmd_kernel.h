#ifndef CMD_KERNEL_H
#define CMD_KERNEL_H

void prompt(void);

void cmd_help(void);
void cmd_hello(void);
void cmd_info(void);
void cmd_ls(unsigned long boot_dtb);
void cmd_cat(unsigned long boot_dtb, const char* filename);

void run_command(const char* cmd,
                 unsigned long boot_hartid,
                 unsigned long boot_dtb);

#endif