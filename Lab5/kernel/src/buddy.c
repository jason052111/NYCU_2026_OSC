#include "buddy.h"
#include "initrd.h"
#include "fdt.h"
#include "uart.h"

unsigned long PHYSICAL_MEM_START = 0xFFFFFFFFFFFFFFFF;
unsigned long PHYSICAL_MEM_END   = 0xFFFFFFFFFFFFFFFF;

extern char _start[]; 
extern char _end[];

struct chunk_pool chunk_pools[POOL_COUNT] = {
    {16, NULL}, {32, NULL}, {64, NULL}, {128, NULL},
    {256, NULL}, {512, NULL}, {1024, NULL}, {2048, NULL}
};

struct page* mem_map; 
struct mem_region reserved_regions[MAX_RESERVED_REGIONS];
int num_reserved_regions = 0;
struct page* free_area[MAX_ORDER + 1] = {NULL};
/*
 * Convert a physical address to its corresponding page structure.
 */
struct page* physical_2_page(unsigned long addr) {
    unsigned long index = (addr - PHYSICAL_MEM_START) / PAGE_SIZE;
    return &mem_map[index];
}
/*
 * Convert a page structure back to its physical address.
 */
unsigned long page_2_physical(struct page* p) {
    unsigned long index = p - mem_map;
    return PHYSICAL_MEM_START + (index * PAGE_SIZE);
}
/*
 * Insert a page into the front of the free list of the given order.
 */
void list_push_front(int order, struct page* p) {
    p->prev = NULL;
    p->next = free_area[order];
    
    if (free_area[order] != NULL) {
        free_area[order]->prev = p;
    }
    free_area[order] = p;
}
/*
 * Remove a page from the free list of the given order.
 */
void list_remove(int order, struct page* p) {
    if (p->prev != NULL) {
        p->prev->next = p->next;
    } else {
        free_area[order] = p->next;
    }
    
    if (p->next != NULL) {
        p->next->prev = p->prev;
    }
    
    p->prev = NULL;
    p->next = NULL;
}
/*
 * Remove and return the first page from the free list of the given order.
 */
struct page* list_pop_front(int order) {
    if (free_area[order] == NULL) {
        return NULL; 
    }
    struct page* target = free_area[order];
    list_remove(order, target);
    return target;
}
/*
 * Get the buddy page of a given page at the specified order.
 *
 * The buddy index is calculated by flipping the bit corresponding
 * to the block size of this order.
 */
struct page* get_buddy(struct page* p, unsigned int order) {
    unsigned long index = p - mem_map; 
    unsigned long buddy_index = index ^ (1 << order);
    return &mem_map[buddy_index];
}
/*
 * Allocate a contiguous block of pages with size 2^order pages.
 *
 * If there is no free block at the requested order, this function searches
 * for a larger block and splits it into smaller buddy blocks until the
 * requested order is reached.
 */
struct page* alloc_pages(unsigned int order) {
    int current_order = order;
    // Find the smallest available free block whose order is >= requested order.
    while (current_order <= MAX_ORDER && free_area[current_order] == NULL) {
        current_order++;
    }
    // No available block was found.
    if (current_order > MAX_ORDER) {
        uart_puts("[Buddy] Error: Memory exhausted!\n");
        return NULL;
    }
    // Take one block from the free list.
    struct page* target_page = list_pop_front(current_order);
    /*
     * Split the larger block until it becomes the requested order.
     * Each split creates one buddy block and puts it back into the free list.
     */
    while (current_order > order) {
        current_order--;
        struct page* buddy_page = target_page + (1 << current_order);
        buddy_page->order = current_order;
        buddy_page->refcount = 0;
        list_push_front(current_order, buddy_page);
    }

    target_page->order = order;
    target_page->refcount = 1; 
    target_page->chunk_size = 0;
    target_page->next = NULL;
    target_page->prev = NULL;
    return target_page;
}
/*
 * Free a previously allocated page block.
 *
 * If its buddy block is also free and has the same order, merge them into
 * a larger block. This continues until no merge is possible.
 */
void free_pages(struct page* p) {
    p->refcount = 0; 
    p->chunk_size = 0;
    // Try to merge with buddy blocks.
    while (p->order < MAX_ORDER) {
        struct page* buddy = get_buddy(p, p->order);
        // The buddy can only be merged if it is free and has the same order.
        if (buddy->refcount != 0 || buddy->order != p->order) {
            break;
        }
        // Remove the buddy from the free list before merging.
        list_remove(p->order, buddy);
        // The merged block starts at the lower address.
        if (buddy < p) {
            p = buddy;
        }
        p->order++;
    }
    // Put the final merged block back into the free list.
    list_push_front(p->order, p);
}
/*
 * Allocate memory with byte-level size.
 *
 * Small allocations use fixed-size chunk pools.
 * Large allocations use the buddy allocator directly.
 */
void* allocate(unsigned long size) {
    struct chunk_pool* target_pool = NULL;
    // Find the smallest chunk pool that can fit the requested size.
    for (int i = 0; i < POOL_COUNT; i++) {
        if (chunk_pools[i].chunk_size >= size) {
            target_pool = &chunk_pools[i];
            break;
        }
    }
    // If no chunk pool is large enough, allocate full pages directly.
    if (target_pool == NULL) {
        int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        int order = 0;
        // Find the smallest order such that 2^order pages can fit size.
        while ((1 << order) < num_pages) { order++; }
        struct page* p = alloc_pages(order);
        if (p == NULL) return NULL; 
        return (void*)page_2_physical(p);
    }
    /*
     * If the selected chunk pool has no free chunks, allocate one page
     * and split it into fixed-size chunks.
     */
    if (target_pool->free_list == NULL) {
        struct page* p = alloc_pages(0); 
        if (p == NULL) return NULL;      
        /*
         * Store the chunk size in the page metadata so free() knows this
         * page belongs to a chunk pool.
         */
        p->chunk_size = target_pool->chunk_size; 

        unsigned long page_addr = page_2_physical(p);
        int chunk_count = PAGE_SIZE / target_pool->chunk_size;
        /*
         * Build a singly linked free list inside the page.
         * Each free chunk stores the address of the next free chunk.
         */
        for (int i = 0; i < chunk_count; i++) {
            unsigned long current_chunk = page_addr + i * target_pool->chunk_size;
            unsigned long next_chunk = current_chunk + target_pool->chunk_size;
            
            if (i == chunk_count - 1) {
                *(void**)current_chunk = NULL; 
            } else {
                *(void**)current_chunk = (void*)next_chunk;
            }
        }
        target_pool->free_list = (void*)page_addr;
    }
    // Pop one chunk from the pool free list.
    void* allocated_chunk = target_pool->free_list;
    target_pool->free_list = *(void**)allocated_chunk; 
    return allocated_chunk;
}
/*
 * Free memory allocated by allocate().
 *
 * If the pointer belongs to a full-page allocation, return it to the buddy
 * allocator. If it belongs to a chunk pool, push it back into that pool's
 * free list.
 */
void free(void* ptr) {
    if (ptr == NULL) return;
    // Find the page that contains this pointer.
    unsigned long base_page_addr = (unsigned long)ptr & ~(PAGE_SIZE - 1);
    struct page* p = physical_2_page(base_page_addr);
    // chunk_size == 0 means this was allocated directly from buddy pages.
    if (p->chunk_size == 0) {
        free_pages(p);
        return; 
    }
    // Otherwise, this page belongs to a fixed-size chunk pool.
    int chunk_size = p->chunk_size;
    struct chunk_pool* target_pool = NULL;
    // Find the corresponding chunk pool.
    for (int i = 0; i < POOL_COUNT; i++) {
        if (chunk_pools[i].chunk_size == chunk_size) {
            target_pool = &chunk_pools[i];
            break;
        }
    }
    // Push the chunk back into the pool free list.
    if (target_pool != NULL) {
        *(void**)ptr = target_pool->free_list;
        target_pool->free_list = ptr;
    }
}
/*
 * Reserve a physical memory region from the buddy allocator.
 *
 * This function removes pages that overlap with the reserved region
 * from the free lists. If a free block only partially overlaps with
 * the reserved region, the block is split into smaller buddy blocks
 * so the reserved part can be removed more accurately.
 */
void memory_reserve(unsigned long base, unsigned long size) {
    // Convert the reserved physical address range into page frame numbers.
    unsigned long start_pfn = (base - PHYSICAL_MEM_START) / PAGE_SIZE;
    unsigned long end_pfn = (base + size - PHYSICAL_MEM_START + PAGE_SIZE - 1) / PAGE_SIZE;
    // Check all free lists from large blocks to small blocks.
    for (int order = MAX_ORDER; order >= 0; order--) {
        struct page* curr = free_area[order];
        while (curr != NULL) {
            struct page* next_node = curr->next; 
            // Calculate the page frame range of this free block.
            unsigned long block_start_pfn = curr - mem_map;
            unsigned long block_end_pfn = block_start_pfn + (1UL << order);
            if (block_end_pfn <= start_pfn || block_start_pfn >= end_pfn) {
                // Case 1: This block does not overlap the reserved region.(Do nothing)
            } 
            else if (block_start_pfn >= start_pfn && block_end_pfn <= end_pfn) {
                // Case 2: This block is completely inside the reserved region.Remove it from the free list.
                list_remove(order, curr);
            } 
            else {
                /*
                * Case 3:
                * This block partially overlaps the reserved region.
                * Split it into two smaller buddy blocks and put both
                * back into the lower-order free list. They will be checked
                * again later.
                */
                list_remove(order, curr);
                int next_order = order - 1;
                struct page* buddy1 = curr;
                struct page* buddy2 = curr + (1 << next_order);
                buddy1->order = next_order;
                buddy2->order = next_order;
                list_push_front(next_order, buddy2);
                list_push_front(next_order, buddy1);
            }

            curr = next_node; 
        }
    }
}
/*
 * Record a memory region that must not be used by the allocator yet.
 *
 * These regions are collected first, and later mem_init() will call
 * memory_reserve() to actually remove them from the buddy free lists.
 */
void early_reserve(unsigned long start, unsigned long size) {
    reserved_regions[num_reserved_regions].start = start;
    reserved_regions[num_reserved_regions].size = size;
    num_reserved_regions++;
}
/*
 * Collect all memory regions that should be reserved before the buddy
 * allocator starts serving allocations.
 *
 * Reserved regions include:
 * - kernel image
 * - initramfs
 * - DTB itself
 * - reserved-memory nodes in the DTB
 * - static DTB memory reservation entries
 */
void all_mem_addr_size(unsigned long dtb_addr){
    unsigned long kernel_base = (unsigned long)_start;
    unsigned long kernel_size = (unsigned long)_end - kernel_base;

    early_reserve(kernel_base, kernel_size);
    reserve_initramfs(dtb_addr);
    reserve_dtb((const void*)dtb_addr);
    dtb_reg_reserve((const void*)dtb_addr);
    dtb_static_reserve((const void*)dtb_addr);
}
/*
 * Allocate space for mem_map.
 *
 * mem_map is the array of struct page metadata used by the buddy allocator.
 * This function searches for a physical memory range that does not overlap
 * any reserved region, then places mem_map there and reserves that region.
 */
void allocate_mem_map() {
    unsigned long map_size = NUM_PAGES * sizeof(struct page);
    unsigned long try_start = PHYSICAL_MEM_START;
    int found = 0;
    // Search physical memory for a free area large enough for mem_map.
    while (try_start + map_size <= PHYSICAL_MEM_END) {
        int collision = 0;
        // Check whether the candidate range overlaps any reserved region.
        for (int i = 0; i < num_reserved_regions; i++) {
            unsigned long res_start = reserved_regions[i].start;
            unsigned long res_end = res_start + reserved_regions[i].size;
            // If overlap exists, move try_start to the end of this reserved region and align it to the next page boundary.
            if (try_start < res_end && (try_start + map_size) > res_start) {
                try_start = (res_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                collision = 1;
                break; 
            }
        }
        // No overlap means this region can be used for mem_map.
        if (!collision) {
            found = 1;
            break;
        }
    }
    // If no suitable place is found, the kernel cannot initialize memory.
    if (!found) {
        uart_puts("Panic: Cannot find enough space for mem_map!\n");
        while(1);
    }
    // Place mem_map at the selected physical address and reserve it.
    mem_map = (struct page*)try_start;
    early_reserve(try_start, map_size);
}
/*
 * Initialize the physical memory allocator.
 *
 * This function:
 * 1. Reads physical memory range from the DTB.
 * 2. Collects reserved memory regions.
 * 3. Allocates and initializes mem_map.
 * 4. Initializes buddy free lists and chunk pools.
 * 5. Adds all memory pages into the buddy allocator.
 * 6. Removes reserved regions from the free lists.
 */
void mem_init(unsigned long dtb_addr){
    // Get physical memory start address and size from the DTB.
    PHYSICAL_MEM_START = get_address((const void*)dtb_addr, "/memory", "reg");
    unsigned long phy_mem_size = get_size((const void*)dtb_addr, "/memory", "reg");
    PHYSICAL_MEM_END = PHYSICAL_MEM_START + phy_mem_size;
    // Collect all reserved regions before building the free lists.
    all_mem_addr_size(dtb_addr);
    // Allocate metadata array for all pages.
    allocate_mem_map();
    // Initialize buddy free lists.
    for (int i = 0; i <= MAX_ORDER; i++) free_area[i] = NULL;
    // Initialize small chunk allocation pools.
    for (int i = 0; i < POOL_COUNT; i++) chunk_pools[i].free_list = NULL;
    // Initialize each page metadata entry.
    for (size_t i = 0; i < NUM_PAGES; i++) {
        mem_map[i].order = 0;
        mem_map[i].refcount = 0;
        mem_map[i].chunk_size = 0;
        mem_map[i].next = NULL;
        mem_map[i].prev = NULL;
    }
    // Add physical memory to the buddy allocator as MAX_ORDER blocks.
    for (int i = 0; i <= NUM_PAGES - (1 << MAX_ORDER); i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        list_push_front(MAX_ORDER, &mem_map[i]);
    }
    // Remove reserved regions from the buddy free lists.
    for (int i = 0; i < num_reserved_regions; i++) {
        memory_reserve(reserved_regions[i].start, reserved_regions[i].size);
    }
}
