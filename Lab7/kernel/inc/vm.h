#ifndef VM_H
#define VM_H
// Offset used to convert physical addresses into kernel virtual addresses.
#define PAGE_OFFSET   0xffffffc000000000UL

// Virtual address where the user program code begins.
#define USER_CODE_VA        0x0UL
// Highest virtual address of the user stack.
#define USER_STACK_TOP      0x4000000000UL
// Lowest virtual address of the user stack.
#define USER_STACK_BASE     (USER_STACK_TOP - USER_STACK_SIZE)
// Starting virtual address used for user mmap regions.
#define USER_MMAP_BASE  0x100000000UL

// Size of one memory page: 4 KiB.
#define PAGE_SIZE     4096 
// Address range covered by one PMD entry: 2 MiB.
#define PMD_SIZE      (1UL << 21)
// Address range covered by one PGD entry: 1 GiB.
#define PGD_SIZE      (1UL << 30)
// Bit position used to obtain the Sv39 PGD index.
#define PGD_SHIFT     30
// Bit position used to obtain the Sv39 PMD index.
#define PMD_SHIFT     21
// Bit position used to obtain the Sv39 PTE index.
#define PTE_SHIFT     12

// Each Sv39 page table contains 512 entries.
#define ENTRIES_PER_TABLE 512

// Mask used to extract one 9-bit page-table index.
#define VPN_MASK      0x1ffUL   // 9 bits

// Extract the top-level page-table index from a virtual address.
#define PGD_INDEX(va) (((unsigned long)(va) >> PGD_SHIFT) & VPN_MASK)
// Extract the middle-level page-table index from a virtual address.
#define PMD_INDEX(va) (((unsigned long)(va) >> PMD_SHIFT) & VPN_MASK)
// Extract the lowest-level page-table index from a virtual address.
#define PTE_INDEX(va) (((unsigned long)(va) >> PTE_SHIFT) & VPN_MASK)

// Convert a physical address into its kernel high-half virtual address.
#define PA2VA(pa) ((unsigned long)(pa) + PAGE_OFFSET)
// Convert a kernel high-half virtual address into a physical address.
#define VA2PA(va) ((unsigned long)(va) - PAGE_OFFSET)

// Anonymous VMA backed by newly allocated zero-filled pages.
#define VMA_ANON 0
// File-backed VMA initialized from file data during demand paging.
#define VMA_FILE 1

// No memory access is permitted.
#define PROT_NONE   0x0
// The memory region may be read.
#define PROT_READ   0x1
// The memory region may be written.
#define PROT_WRITE  0x2
// Instructions may be executed from the memory region.
#define PROT_EXEC   0x4

// Create an mmap region without a backing file.
#define MAP_ANONYMOUS  0x20
// Allocate and map pages immediately instead of waiting for page faults.
#define MAP_POPULATE   0x8000

// The page-table entry contains a valid mapping.
#define PTE_V  (1UL << 0)
// The mapped page may be read.
#define PTE_R  (1UL << 1)
// The mapped page may be written.
#define PTE_W  (1UL << 2)
// Instructions may be executed from the mapped page.
#define PTE_X (1UL << 3)
// The mapped page may be accessed from user mode.
#define PTE_U (1UL << 4)
// The mapping is shared across address spaces.
#define PTE_G (1UL << 5)
// The page has been accessed.
#define PTE_A (1UL << 6)
// The page has been written to.
#define PTE_D (1UL << 7)

// Default permissions for normal kernel memory.
#define PROT_KERNEL (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
// Default permissions for memory-mapped I/O regions.
#define PROT_MMIO   (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
// Common flags required by valid user-accessible mappings.
#define PROT_USER_BASE (PTE_V | PTE_U | PTE_A | PTE_D)
// Permissions for user code and read-only executable regions.
#define PROT_USER_RX   (PROT_USER_BASE | PTE_R | PTE_X)
// Permissions for writable user data and stack regions.
#define PROT_USER_RW   (PROT_USER_BASE | PTE_R | PTE_W)

// Software-defined flag marking a copy-on-write page.
#define PTE_COW (1UL << 8)
// Extract the lowest ten flag bits from a page-table entry.
#define PTE_FLAGS(pte) ((pte) & 0x3ffUL)
// Maximum number of physical pages tracked by the reference counter.
#define MAX_TRACKED_PAGES (1UL << 20)

// Select the Sv39 address translation mode in the satp register.
#define SATP_SV39         (8UL << 60)
// Construct a satp value using the physical address of the root page table.
#define MAKE_SATP(pgd_pa) (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

// Construct a page-table entry from a physical address and permission flags.
#define MAKE_PTE(pa, flags) (((((unsigned long)(pa)) >> 12) << 10) | (flags))

// Extract the page-aligned physical address from a page-table entry.
#define PTE2PA(pte) ((((unsigned long)(pte)) >> 10) << 12)

// Round an address down to the beginning of its page.
#define PAGE_ALIGN_DOWN(x) ((unsigned long)(x) & ~(PAGE_SIZE - 1))
// Round an address up to the beginning of the next page.
#define PAGE_ALIGN_UP(x)   (((unsigned long)(x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

// Root page table containing the kernel mappings.
extern unsigned long* final_kernel_pgd;
/*
 * Describes one valid virtual-memory region of a user process.
 * A VMA records the address range, access permissions, backing type,
 * and optional file data used for demand paging.
 */
struct vm_area {
    unsigned long start;      // First valid virtual address in this region.
    unsigned long end;        // First address outside the region: [start, end).
    unsigned long prot;       // Page permissions such as PTE_R, PTE_W, and PTE_X.
    int type;                 // VMA_ANON or VMA_FILE.

    void* file_data;          // Kernel address of backing file data for VMA_FILE.
    unsigned long file_size;  // Number of valid bytes in the backing file.

    struct vm_area* next;     // Next VMA in this process's linked list.
};

unsigned long kva_or_pa_to_pa(unsigned long addr);
void setup_vm(void);
void drop_identity_map(void);
unsigned long* create_kernel_pgd(void);
unsigned long* create_user_pgd(void);
void map_pages(unsigned long* pgd, unsigned long va, unsigned long pa, unsigned long size, unsigned long prot);
void switch_vm(unsigned long* pgd);
void setup_final_vm(unsigned long dtb_addr);
void page_ref_set(unsigned long pa, unsigned int count);
void page_ref_inc(unsigned long pa);
void page_ref_dec(unsigned long pa);
unsigned int page_ref_get(unsigned long pa);
unsigned long* lookup_pte(unsigned long* pgd, unsigned long va);

#endif