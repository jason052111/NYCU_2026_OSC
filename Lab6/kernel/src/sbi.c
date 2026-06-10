#include "sbi.h"
/*
 * ext：SBI Extension ID
 * fid：Function ID
 */
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
/*
 * Execute the RISC-V `ecall` to invoke SBI: a0–a5 carry arguments, a6 = fid and a7 = ext
 * a0 and a1 are marked as input/output because SBI overwrites them with (error, value) on return
 * volatile means this part cannot be removed or arbitrarily moved by the compiler
 * The "memory" clobber prevents the compiler from reordering memory accesses across this call
*/    
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

void sbi_set_timer(unsigned long stime_value) {
    sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER,
              stime_value, 0, 0, 0, 0, 0);
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