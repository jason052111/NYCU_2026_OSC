#ifndef VM_H
#define VM_H

#define PAGE_OFFSET   0xffffffc000000000UL

#define USER_CODE_VA        0x0UL
#define USER_STACK_TOP      0x4000000000UL
#define USER_STACK_BASE     (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_MMAP_BASE  0x100000000UL

#define PAGE_SIZE     4096 
#define PMD_SIZE      (1UL << 21)
#define PGD_SIZE      (1UL << 30)

#define PGD_SHIFT     30
#define PMD_SHIFT     21
#define PTE_SHIFT     12

#define ENTRIES_PER_TABLE 512

#define VPN_MASK      0x1ffUL   // 9 bits

#define PGD_INDEX(va) (((unsigned long)(va) >> PGD_SHIFT) & VPN_MASK)
#define PMD_INDEX(va) (((unsigned long)(va) >> PMD_SHIFT) & VPN_MASK)
#define PTE_INDEX(va) (((unsigned long)(va) >> PTE_SHIFT) & VPN_MASK)

#define PA2VA(pa) ((unsigned long)(pa) + PAGE_OFFSET)
#define VA2PA(va) ((unsigned long)(va) - PAGE_OFFSET)

#define VMA_ANON 0
#define VMA_FILE 1

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_ANONYMOUS  0x20
#define MAP_POPULATE   0x8000

/* RISC-V Sv39 PTE bits */
#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)

/* Basic1 permissions */
#define PROT_KERNEL (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PROT_MMIO   (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PROT_USER_BASE (PTE_V | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RX   (PROT_USER_BASE | PTE_R | PTE_X)
#define PROT_USER_RW   (PROT_USER_BASE | PTE_R | PTE_W)

#define PTE_COW (1UL << 8)
#define PTE_FLAGS(pte) ((pte) & 0x3ffUL)
#define MAX_TRACKED_PAGES (1UL << 20)

#define SATP_SV39         (8UL << 60)
#define MAKE_SATP(pgd_pa) (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

#define MAKE_PTE(pa, flags) (((((unsigned long)(pa)) >> 12) << 10) | (flags))

#define PTE2PA(pte) ((((unsigned long)(pte)) >> 10) << 12)

#define PAGE_ALIGN_DOWN(x) ((unsigned long)(x) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x)   (((unsigned long)(x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

extern unsigned long* final_kernel_pgd;

struct vm_area {
    unsigned long start;
    unsigned long end;
    unsigned long prot;
    int type;

    void* file_data;
    unsigned long file_size;

    struct vm_area* next;
};

unsigned long kva_or_pa_to_pa(unsigned long addr);
void setup_vm(void);
void drop_identity_map(void);
unsigned long* create_kernel_pgd(void);
unsigned long* create_user_pgd(void);
void map_pages(unsigned long* pgd,
               unsigned long va,
               unsigned long pa,
               unsigned long size,
               unsigned long prot);
void switch_vm(unsigned long* pgd);
void setup_final_vm(unsigned long dtb_addr);
void page_ref_set(unsigned long pa, unsigned int count);
void page_ref_inc(unsigned long pa);
void page_ref_dec(unsigned long pa);
unsigned int page_ref_get(unsigned long pa);
unsigned long* lookup_pte(unsigned long* pgd, unsigned long va);

#endif