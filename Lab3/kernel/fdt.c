#include <stdint.h>
#include <stddef.h>

extern void uart_puts(const char* s);
extern uint32_t bswap32(uint32_t x);
extern const void* align_up(const void* ptr, size_t align);
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, int n);
extern char *strchr(const char *s, int c);
extern void early_reserve(unsigned long start, unsigned long size);
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
/* 
 * Find the offset of a device tree node given its absolute path.
 * Returns the offset in bytes from the start of the FDT, or -1 if not found.
 */
int fdt_path_offset(const void* fdt, const char* path) {
    const struct fdt_header* header = (const struct fdt_header*)fdt;
    // Verify FDT magic number (convert from Big-Endian first) 
    if (bswap32(header->magic) != 0xd00dfeed) {
        uart_puts("Error: Invalid FDT magic number\n");
        return -1; 
    }

    uint32_t off_struct = bswap32(header->off_dt_struct);
    const char* ptr = (const char*)fdt + off_struct;

    // Root node special case
    if (strcmp(path, "/") == 0) {
        return off_struct;
    }

    // Count how many levels deep the target path is
    int segment_count = 0;
    const char* p = path;
    while (*p) {
        if (*p == '/' && *(p+1) != '\0' && *(p+1) != '/') segment_count++;
        p++;
    }

    int depth = -1;
    int curr_match_level = 0;

    // Parse the FDT structure block linearly
    while (1) {
        uint32_t tag = bswap32(*(uint32_t*)ptr);
        uint32_t curr_offset = ptr - (const char*)fdt;
        ptr += 4;

        if (tag == FDT_BEGIN_NODE) {
            const char* name = ptr;
            ptr += strlen(name) + 1; 
            ptr = (const char*)align_up(ptr, 4); // Pad to 4-byte boundary
            depth++;

            // If the current depth matches our progress in resolving the path
            if (depth == curr_match_level + 1) {
                const char* seg_start = path;
                if (*seg_start == '/') seg_start++;
                // Skip to the current path segment we are trying to match
                for (int i = 0; i < curr_match_level; i++) {
                    seg_start = strchr(seg_start, '/') + 1;
                }
                const char* seg_end = strchr(seg_start, '/');
                int seg_len = seg_end ? (seg_end - seg_start) : strlen(seg_start);
                // Match node name (ignoring the @address part if present)
                if (strncmp(name, seg_start, seg_len) == 0 && (name[seg_len] == '\0'|| name[seg_len] == '@')) {
                    curr_match_level++;
                    if (curr_match_level == segment_count) {
                        return curr_offset;
                    }
                }
            }
        } else if (tag == FDT_END_NODE) {
            // Backtrack our match level if we exit a matching node path
            if (depth <= curr_match_level) {
                curr_match_level = depth - 1;
            }
            depth--;
        } else if (tag == FDT_PROP) {
            // Skip over properties (Length + NameOffset + Data)
            uint32_t len = bswap32(*(uint32_t*)ptr);
            ptr += 8; 
            ptr += len;
            ptr = (const char*)align_up(ptr, 4);
        } else if (tag == FDT_NOP) {
            continue;
        } else if (tag == FDT_END) {
            break; 
        }
    }
    return -1;
}
/* 
 * Get the value of a property inside a specific node.
 * Returns a pointer to the property data, and updates lenp with its size.
 */
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp) {
    const struct fdt_header* header = (const struct fdt_header*)fdt;
    if (bswap32(header->magic) != 0xd00dfeed) {
        uart_puts("Error: Invalid FDT magic number\n");
        return NULL; 
    }

    const char* ptr = (const char*)fdt + nodeoffset;
    const char* strings = (const char*)fdt + bswap32(header->off_dt_strings);

    uint32_t tag = bswap32(*(uint32_t*)ptr);
    if (tag != FDT_BEGIN_NODE) return NULL;
    ptr += 4;
    // Skip the node's name
    ptr += strlen(ptr) + 1;
    ptr = (const char*)align_up(ptr, 4);
    // Iterate through the properties of this node
    while (1) {
        tag = bswap32(*(uint32_t*)ptr);
        ptr += 4;

        if (tag == FDT_PROP) {
            uint32_t len = bswap32(*(uint32_t*)ptr);
            uint32_t nameoff = bswap32(*(uint32_t*)(ptr + 4));
            ptr += 8;
            // Get the property name from the strings block
            const char* prop_name = strings + nameoff;
            // If the property name matches, return its data pointer
            if (strcmp(prop_name, name) == 0) {
                if (lenp) *lenp = len;
                return ptr; 
            }
            // Skip this property's data if it doesn't match
            ptr += len;
            ptr = (const char*)align_up(ptr, 4);
        } else if (tag == FDT_NOP) {
            continue;
        } else {
            break;
        }
    }
    return NULL;
}
/* 
 * Find the parent node of a given path and extract a specific cell size property
 */
int get_parent_address_cells(const void* dtb_addr, const char *node_path, const char *cell_name) {
    const void *fdt = (const void *)dtb_addr;
    char parent_path[64];
    int last_slash = -1;
    int i = 0;
    // Extract the parent path string by finding the last
    while (node_path[i] != '\0' && i < 63) {
        if (node_path[i] == '/') {
            last_slash = i;
        }
        parent_path[i] = node_path[i];
        i++;
    }
    parent_path[i] = '\0'; 

    if (last_slash == 0) {
        parent_path[1] = '\0'; // Parent is the root node
    } else if (last_slash > 0) {
        parent_path[last_slash] = '\0'; // Truncate at the last slash
    } 

    int parent_node = fdt_path_offset(fdt, parent_path);
    if (parent_node < 0) {
        uart_puts("Can't find parent path");
        return -1; 
    }

    if (parent_node >= 0) {
        int len = 0;
        // Get the requested cell size property from the parent
        const uint32_t *prop = (const uint32_t *)fdt_getprop(fdt, parent_node, cell_name, &len);
        if (prop != 0 && len == 4) {
            return (int)bswap32(prop[0]); // Return the Big-Endian parsed value
        }
    }

    return -1; 
}
/* 
 * Find the node of a given path and extract a specific cell size property
 */
int get_address_cells(const void* dtb_addr, const char *node_path, const char *cell_name) {
    const void *fdt = (const void *)dtb_addr;

    int node = fdt_path_offset(fdt, node_path);
    if (node < 0) {
        uart_puts("Can't find path");
        return -1; 
    }

    if (node >= 0) {
        int len = 0;
        // Get the requested cell size property from the parent
        const uint32_t *prop = (const uint32_t *)fdt_getprop(fdt, node, cell_name, &len);
        if (prop != 0 && len == 4) {
            return (int)bswap32(prop[0]); // Return the Big-Endian parsed value
        }
    }

    return -1; 
}
/* 
 * Get the physical memory address of a node.
 * It automatically handles 32-bit (1 cell) or 64-bit (2 cells) addresses 
 * based on the parent's "#address-cells" rule.
 */
unsigned long get_address(const void* dtb_addr, const char *node_path, const char *prop_name) {
    const void *fdt = (const void *)dtb_addr;
    int node = fdt_path_offset(fdt, node_path);
    // Device Tree rule: The address format of a node is dictated by its parent
    int address_cells = get_parent_address_cells(fdt, node_path, "#address-cells");
    if (node >= 0) {
        int len = 0;
        const uint32_t *reg = fdt_getprop(fdt, node, prop_name, &len);
        unsigned long addr = 0;

        // Parse the address based on how many cells the parent specified
        if (address_cells == 1) {
            addr = bswap32(reg[0]);
        } else if (address_cells == 2) {
            uint64_t high = bswap32(reg[0]);
            uint64_t low  = bswap32(reg[1]);
            addr = (high << 32) | low;
        } 
        return addr; 
    }

    return -1;
}
/* 
 * Get the physical memory address of a node.
 * It automatically handles 32-bit (1 cell) or 64-bit (2 cells) addresses 
 * based on the parent's "#size-cells" rule.
 */
unsigned long get_size(const void* dtb_addr, const char *node_path, const char *prop_name) {
    const void *fdt = (const void *)dtb_addr;
    int node = fdt_path_offset(fdt, node_path);
    // Device Tree rule: The address format of a node is dictated by its parent
    int address_cells = get_parent_address_cells(fdt, node_path, "#size-cells");
    if (node >= 0) {
        int len = 0;
        const uint32_t *reg = fdt_getprop(fdt, node, prop_name, &len);
        unsigned long addr = 0;

        // Parse the address based on how many cells the parent specified
        if (address_cells == 1) {
            addr = bswap32(reg[2]);
        } else if (address_cells == 2) {
            uint64_t high = bswap32(reg[2]);
            uint64_t low  = bswap32(reg[3]);
            addr = (high << 32) | low;
        } 
        return addr; 
    }

    return -1;
}
/*
 * Parses the DTB to find and reserve memory regions under "/reserved-memory".
 * It extracts the base address and size from the "reg" properties of child nodes, 
 * handles endianness conversion, and calls early_reserve() to protect them.
 */
void dtb_reg_reserve(const void* dtb_addr){
    const void *fdt = (const void *)dtb_addr;
    int address_cells = get_address_cells(fdt, "/reserved-memory", "#address-cells");
    int size_cells = get_address_cells(fdt, "/reserved-memory", "#size-cells");
    int offset = fdt_path_offset(fdt, "/reserved-memory");
    if (offset < 0) return;
    const char* ptr = (const char*)fdt + offset;
    const struct fdt_header* header = (const struct fdt_header*)fdt;
    const char* strings = (const char*)fdt + bswap32(header->off_dt_strings);

    int depth = 0;
    while (1) {
        uint32_t tag = bswap32(*(uint32_t*)ptr);
        if (tag == FDT_BEGIN_NODE) {
            depth++;
            ptr += 4;
            ptr += strlen(ptr) + 1;
            ptr = (const char*)align_up(ptr, 4);
        } 
        else if (tag == FDT_END_NODE) {
            depth--;
            ptr += 4;
            if (depth == 0) break;
        } 
        else if (tag == FDT_PROP) {
            ptr += 4;
            uint32_t len = bswap32(*(uint32_t*)ptr);
            uint32_t nameoff = bswap32(*(uint32_t*)(ptr + 4));
            ptr += 8;
            const char* prop_name = strings + nameoff;
            if (depth == 2 && strcmp(prop_name, "reg") == 0) {
                const uint32_t* reg_val = (const uint32_t*)ptr;
                unsigned long base = 0, size = 0;
                if (address_cells == 1) base = bswap32(reg_val[0]);
                else base = ((uint64_t)bswap32(reg_val[0]) << 32) | bswap32(reg_val[1]);
                if (size_cells == 1) size = bswap32(reg_val[address_cells]);
                else size = ((uint64_t)bswap32(reg_val[address_cells]) << 32) | bswap32(reg_val[address_cells+1]);
                if (size > 0) {
                    early_reserve(base, size);
                }
            }

            ptr += len;
            ptr = (const char*)align_up(ptr, 4);
        } 
        else if (tag == FDT_NOP) {
            ptr += 4;
        } 
        else {
            break;
        }
    }
}
/*
 * Parses the FDT Memory Reservation Block to find and reserve statically 
 * defined memory regions. It reads 64-bit address and size pairs, handles 
 * endianness, and calls early_reserve() until the {0, 0} terminator is reached.
 */
void dtb_static_reserve(const void* dtb_addr) {
    const struct fdt_header* header = (const struct fdt_header*)dtb_addr;
    uint32_t off_rsvmap = bswap32(header->off_mem_rsvmap);
    const uint32_t* ptr = (const uint32_t*)((const char*)dtb_addr + off_rsvmap);

    while (1) {
        uint32_t addr_high = bswap32(ptr[0]);
        uint32_t addr_low  = bswap32(ptr[1]);
        uint32_t size_high = bswap32(ptr[2]);
        uint32_t size_low  = bswap32(ptr[3]);
        unsigned long addr = ((unsigned long)addr_high << 32) | addr_low;
        unsigned long size = ((unsigned long)size_high << 32) | size_low;
        if (addr == 0 && size == 0) break;

        if (size > 0) {
            early_reserve(addr, size);
        }

        ptr += 4;
    }
}
/*
 * Extracts the total size of the Device Tree Blob (DTB) from its header 
 * and reserves its memory region to prevent the allocator from overwriting it.
 */
void reserve_dtb(const void* fdt_ptr) {
    struct fdt_header* header = (struct fdt_header*)fdt_ptr;
    unsigned int size = bswap32(header->totalsize);
    unsigned long start = (unsigned long)fdt_ptr;
    early_reserve(start, (unsigned long)size);
}