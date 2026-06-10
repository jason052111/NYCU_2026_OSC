#ifndef FDT_H
#define FDT_H

#include <stdint.h>
#include <stddef.h>

// Device Tree Block (DTB) standard structural tags
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

struct fdt_header {
    uint32_t magic;             // Magic number to verify FDT validity (must be 0xd00dfeed)
    uint32_t totalsize;         // Total size of the device tree block in bytes
    uint32_t off_dt_struct;     // Offset in bytes to the structure block
    uint32_t off_dt_strings;    // Offset in bytes to the strings block
    uint32_t off_mem_rsvmap;    // Offset in bytes to the memory reservation block
    uint32_t version;           // Format version of the FDT
    uint32_t last_comp_version; // Lowest version this FDT is backwards compatible with
    uint32_t boot_cpuid_phys;   // Physical CPU ID (Hart ID in RISC-V) of the boot processor
    uint32_t size_dt_strings;   // Length in bytes of the strings block
    uint32_t size_dt_struct;    // Length in bytes of the structure block
};

int fdt_path_offset(const void* fdt, const char* path);
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp);
int get_parent_address_cells(const void* dtb_addr, const char *node_path, const char *cell_name);
int get_address_cells(const void* dtb_addr, const char *node_path, const char *cell_name);
unsigned long get_address(const void* dtb_addr, const char *node_path, const char *prop_name);
unsigned long get_size(const void* dtb_addr, const char *node_path, const char *prop_name);
unsigned long get_dtb_prop_u32(const void* dtb_addr, const char* path, const char* prop_name);
void dtb_reg_reserve(const void* dtb_addr);
void dtb_static_reserve(const void* dtb_addr);
void reserve_dtb(const void* fdt_ptr);

#endif