#include "uaccess.h"
#include "tool.h"

#define SSTATUS_SIE (1UL << 1)
#define SSTATUS_SUM (1UL << 18)

/*
 * Disable interrupts and temporarily allow S-mode
 * to access pages marked as userspace pages.
 */
static unsigned long uaccess_begin(void) {
    unsigned long old_sstatus;
    // Read the current sstatus CSR into old_sstatus.
    asm volatile("csrr %0, sstatus"
                 : "=r"(old_sstatus));
    // Clear SIE to disable supervisor interrupts during user memory access.
    asm volatile("csrc sstatus, %0"
                 :
                 : "r"(SSTATUS_SIE)
                 : "memory");
    // Set SUM to allow S-mode to access pages marked with PTE_U.
    asm volatile("csrs sstatus, %0"
                 :
                 : "r"(SSTATUS_SUM)
                 : "memory");
    return old_sstatus;
}
/*
 * Restore the original SUM and interrupt states.
 */
static void uaccess_end(unsigned long old_sstatus) {
    // If SUM was originally disabled, disable it again.
    if (!(old_sstatus & SSTATUS_SUM)) {
        // Clear SUM to prevent S-mode from accessing user pages.
        asm volatile("csrc sstatus, %0"
                     :
                     : "r"(SSTATUS_SUM)
                     : "memory");
    }
    // If supervisor interrupts were originally enabled, enable them again.
    if (old_sstatus & SSTATUS_SIE) {
        // Set SIE to restore supervisor interrupt handling.
        asm volatile("csrs sstatus, %0"
                     :
                     : "r"(SSTATUS_SIE)
                     : "memory");
    }
}
/*
 * Copy a fixed number of bytes from userspace to a kernel buffer.
 */
int copy_from_user(void* kernel_dst, const void* user_src, size_t len) {
    if (len == 0) return 0;
    if (kernel_dst == 0 || user_src == 0) return -1;
    unsigned long old_sstatus = uaccess_begin();
    mem_copy(kernel_dst, user_src, len);
    uaccess_end(old_sstatus);
    return 0;
}
/*
 * Copy a fixed number of bytes from a kernel buffer to userspace.
 */
int copy_to_user(void* user_dst, const void* kernel_src, size_t len) {
    if (len == 0) return 0;
    if (user_dst == 0 || kernel_src == 0) return -1;
    unsigned long old_sstatus = uaccess_begin();
    mem_copy(user_dst, kernel_src, len);
    uaccess_end(old_sstatus);
    return 0;
}
/*
 * Copy a null-terminated string from userspace to a kernel buffer.
 * max_len includes the space reserved for the terminating null byte.
 */
int copy_string_from_user(char* kernel_dst, const char* user_src, size_t max_len) {
    if (kernel_dst == 0 || user_src == 0 || max_len == 0) return -1;
    int result = -1;
    unsigned long old_sstatus = uaccess_begin();

    for (size_t i = 0; i < max_len; i++) {
        kernel_dst[i] = user_src[i];
        if (kernel_dst[i] == '\0') {
            result = 0;
            break;
        }
    }

    uaccess_end(old_sstatus);
    if (result < 0) kernel_dst[max_len - 1] = '\0';
    return result;
}