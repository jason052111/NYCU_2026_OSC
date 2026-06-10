#ifndef SBI_H
#define SBI_H

#define SBI_EXT_BASE           0x10
#define SBI_EXT_TIME           0x00
#define SBI_EXT_TIME_SET_TIMER 0
/*
 * SBI Base Extension function IDs.
 */
enum sbi_ext_base_fid {
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
    SBI_EXT_BASE_GET_MVENDORID,
    SBI_EXT_BASE_GET_MARCHID,
    SBI_EXT_BASE_GET_MIMPID,
};
/*
 * SBI return value.
 * error: SBI error code
 * value: return value from SBI call
 */
struct sbiret {
    long error;
    long value;
};
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
                        unsigned long arg5);

void sbi_set_timer(unsigned long stime_value);

long sbi_get_spec_version(void);
long sbi_get_impl_id(void);
long sbi_get_impl_version(void);

#endif