#include "vm.h"
#include "buddy.h"
#include "fdt.h"
#include "video.h"
#include "uart.h"

#define LINEAR_MAP_GIB 4

unsigned long* final_kernel_pgd = 0;

static unsigned int page_ref[MAX_TRACKED_PAGES];

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    kernel_pgd[ENTRIES_PER_TABLE] = { 0 };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    kernel_pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };

unsigned long kva_or_pa_to_pa(unsigned long addr)
{
    if (addr >= PAGE_OFFSET) {
        return addr - PAGE_OFFSET;
    }
    return addr;
}

void setup_vm(void)
{
    /*
     * Map PA 0x00000000 ~ 0xffffffff.
     *
     * Identity mapping:
     *   VA 0x00000000 ~ 0xffffffff
     *   -> PA 0x00000000 ~ 0xffffffff
     *
     * Higher-half mapping:
     *   VA PAGE_OFFSET ~ PAGE_OFFSET + 0xffffffff
     *   -> PA 0x00000000 ~ 0xffffffff
     */
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pa_base = (unsigned long)i * PGD_SIZE;
        unsigned long pmd_pa = (unsigned long)kernel_pmd[i];

        kernel_pgd[i] = MAKE_PTE(pmd_pa, PTE_V);
        kernel_pgd[PGD_INDEX(PAGE_OFFSET) + i] = MAKE_PTE(pmd_pa, PTE_V);

        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = pa_base + (unsigned long)j * PMD_SIZE;
            kernel_pmd[i][j] = MAKE_PTE(pa, PROT_KERNEL);
        }
    }

    unsigned long pgd_pa = (unsigned long)kernel_pgd;
    unsigned long satp_val = MAKE_SATP(pgd_pa);

    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(satp_val)
        : "memory"
    );
}

void drop_identity_map(void)
{
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        kernel_pgd[i] = 0;
    }

    asm volatile("sfence.vma zero, zero" ::: "memory");
}

static void clear_page_table(unsigned long* table)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        table[i] = 0;
    }
}

static unsigned long* alloc_page_table(void)
{
    unsigned long* table = (unsigned long*)allocate(PAGE_SIZE);

    if (table == 0) {
        return 0;
    }

    clear_page_table(table);
    return table;
}

static unsigned long page_index(unsigned long pa)
{
    return pa >> 12;
}

void map_pages(unsigned long* pgd,
               unsigned long va,
               unsigned long pa,
               unsigned long size,
               unsigned long prot)
{
    unsigned long start_va = PAGE_ALIGN_DOWN(va);
    unsigned long end_va = PAGE_ALIGN_UP(va + size);
    unsigned long curr_pa = PAGE_ALIGN_DOWN(pa);

    for (unsigned long curr_va = start_va;
         curr_va < end_va;
         curr_va += PAGE_SIZE, curr_pa += PAGE_SIZE) {

        unsigned long pgd_idx = PGD_INDEX(curr_va);
        unsigned long pmd_idx = PMD_INDEX(curr_va);
        unsigned long pte_idx = PTE_INDEX(curr_va);

        unsigned long* pmd;
        unsigned long* pte;

        if (!(pgd[pgd_idx] & PTE_V)) {
            pmd = alloc_page_table();
            if (pmd == 0) return;
            pgd[pgd_idx] = MAKE_PTE(VA2PA((unsigned long)pmd), PTE_V);
        } else {
            pmd = (unsigned long*)PA2VA(PTE2PA(pgd[pgd_idx]));
        }

        if (!(pmd[pmd_idx] & PTE_V)) {
            pte = alloc_page_table();
            if (pte == 0) return;
            pmd[pmd_idx] = MAKE_PTE(VA2PA((unsigned long)pte), PTE_V);
        } else {
            pte = (unsigned long*)PA2VA(PTE2PA(pmd[pmd_idx]));
        }

        pte[pte_idx] = MAKE_PTE(curr_pa, prot);
    }
}

unsigned long* create_kernel_pgd(void)
{
    return alloc_page_table();
}

unsigned long* create_user_pgd(void)
{
    unsigned long* user_pgd = alloc_page_table();

    if (user_pgd == 0) {
        return 0;
    }

    /*
     * Copy kernel high-half mapping.
     *
     * Trap from user mode to kernel mode does not automatically
     * switch page tables, so every user PGD must contain kernel
     * high-half mappings.
     */
    for (int i = PGD_INDEX(PAGE_OFFSET); i < ENTRIES_PER_TABLE; i++) {
        user_pgd[i] = final_kernel_pgd[i];
    }

    return user_pgd;
}

void switch_vm(unsigned long* pgd)
{
    unsigned long pgd_pa = VA2PA((unsigned long)pgd);
    unsigned long satp_val = MAKE_SATP(pgd_pa);

    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(satp_val)
        : "memory"
    );

}

void debug_pte(unsigned long* pgd, unsigned long va)
{
    unsigned long pgd_idx = PGD_INDEX(va);
    unsigned long pmd_idx = PMD_INDEX(va);
    unsigned long pte_idx = PTE_INDEX(va);

    uart_puts("VA = ");
    uart_hex(va);
    uart_puts("\n");

    uart_puts("PGD = ");
    uart_hex(pgd[pgd_idx]);
    uart_puts("\n");

    unsigned long* pmd = (unsigned long*)PA2VA(PTE2PA(pgd[pgd_idx]));
    uart_puts("PMD = ");
    uart_hex(pmd[pmd_idx]);
    uart_puts("\n");

    unsigned long* pte = (unsigned long*)PA2VA(PTE2PA(pmd[pmd_idx]));
    uart_puts("PTE = ");
    uart_hex(pte[pte_idx]);
    uart_puts("\n");
}

void setup_final_vm(unsigned long dtb_addr)
{
    final_kernel_pgd = create_kernel_pgd();

    /* 1. map normal RAM */
    map_pages(final_kernel_pgd,
              PA2VA(PHYSICAL_MEM_START),
              PHYSICAL_MEM_START,
              PHYSICAL_MEM_END - PHYSICAL_MEM_START,
              PROT_KERNEL);

    /* 2. map UART as MMIO */
    unsigned long uart_pa = get_address((const void*)dtb_addr,
                                        "/soc/serial",
                                        "reg");

    unsigned long uart_size = get_size((const void*)dtb_addr,
                                        "/soc/serial",
                                        "reg");

    map_pages(final_kernel_pgd,
              PA2VA(uart_pa),
              uart_pa,
              uart_size,
              PROT_MMIO);

    /* 3. map PLIC as MMIO */
    unsigned long plic_pa = get_address((const void*)dtb_addr,
                                        "/soc/interrupt-controller",
                                        "reg");

    unsigned long plic_size = get_size((const void*)dtb_addr,
                                       "/soc/interrupt-controller",
                                       "reg");

    map_pages(final_kernel_pgd,
              PA2VA(plic_pa),
              plic_pa,
              plic_size,
              PROT_MMIO);

    /* 4. map framebuffer as MMIO */
    unsigned long fb_size = FB_WIDTH * FB_HEIGHT * 4;

    map_pages(final_kernel_pgd,
              PA2VA(FB_BASE_PA),
              FB_BASE_PA,
              fb_size,
              PROT_MMIO);

    /* 5. switch to final 4KB page table */
    switch_vm(final_kernel_pgd);
}

void page_ref_set(unsigned long pa, unsigned int count)
{
    page_ref[page_index(pa)] = count;
}

void page_ref_inc(unsigned long pa)
{
    page_ref[page_index(pa)]++;
}

void page_ref_dec(unsigned long pa)
{
    if (page_ref[page_index(pa)] > 0) {
        page_ref[page_index(pa)]--;
    }
}

unsigned int page_ref_get(unsigned long pa)
{
    return page_ref[page_index(pa)];
}

unsigned long* lookup_pte(unsigned long* pgd, unsigned long va)
{
    unsigned long pgd_idx = PGD_INDEX(va);
    unsigned long pmd_idx = PMD_INDEX(va);
    unsigned long pte_idx = PTE_INDEX(va);

    if (!(pgd[pgd_idx] & PTE_V)) {
        return 0;
    }

    unsigned long* pmd = (unsigned long*)PA2VA(PTE2PA(pgd[pgd_idx]));

    if (!(pmd[pmd_idx] & PTE_V)) {
        return 0;
    }

    unsigned long* pte = (unsigned long*)PA2VA(PTE2PA(pmd[pmd_idx]));

    return &pte[pte_idx];
}