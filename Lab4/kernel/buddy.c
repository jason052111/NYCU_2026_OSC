#include <stdint.h>
#include <stddef.h>

#define MAX_RESERVED_REGIONS 32
#define PAGE_SIZE 4096
#define MAX_ORDER 10
#define POOL_COUNT 8
#define NUM_PAGES ((PHYSICAL_MEM_END - PHYSICAL_MEM_START) / PAGE_SIZE)
#define MAX_ALLOC_SIZE ((1 << MAX_ORDER) * PAGE_SIZE)

unsigned long PHYSICAL_MEM_START = 0xFFFFFFFFFFFFFFFF;
unsigned long PHYSICAL_MEM_END   = 0xFFFFFFFFFFFFFFFF;

extern char _start[]; 
extern char _end[];
extern void uart_puts(const char* s);
extern void uart_int(int num);
extern void uart_hex(unsigned long h);
extern void dtb_reg_reserve(const void* dtb_addr);
extern void reserve_dtb(const void* fdt_ptr);
extern void dtb_static_reserve(const void* dtb_addr);
extern void reserve_initramfs(unsigned long dtb_addr);
extern unsigned long get_address(const void* dtb_addr, const char *node_path, const char *prop_name);
extern unsigned long get_size(const void* dtb_addr, const char *node_path, const char *prop_name);

struct mem_region {
    unsigned long start;
    unsigned long size;
};

struct page {
    int order;
    int refcount;
    int chunk_size;
    struct page* next;
    struct page* prev;
};

struct chunk_pool {
    int chunk_size;
    void* free_list;
};

struct chunk_pool chunk_pools[POOL_COUNT] = {
    {16, NULL}, {32, NULL}, {64, NULL}, {128, NULL},
    {256, NULL}, {512, NULL}, {1024, NULL}, {2048, NULL}
};

struct page* mem_map; 
struct mem_region reserved_regions[MAX_RESERVED_REGIONS];
int num_reserved_regions = 0;
struct page* free_area[MAX_ORDER + 1] = {NULL};

struct page* physical_2_page(unsigned long addr) {
    unsigned long index = (addr - PHYSICAL_MEM_START) / PAGE_SIZE;
    return &mem_map[index];
}

unsigned long page_2_physical(struct page* p) {
    unsigned long index = p - mem_map;
    return PHYSICAL_MEM_START + (index * PAGE_SIZE);
}

void list_push_front(int order, struct page* p) {
    p->prev = NULL;
    p->next = free_area[order];
    
    if (free_area[order] != NULL) {
        free_area[order]->prev = p;
    }
    free_area[order] = p;
}

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

struct page* list_pop_front(int order) {
    if (free_area[order] == NULL) {
        return NULL; 
    }
    struct page* target = free_area[order];
    list_remove(order, target);
    return target;
}

struct page* get_buddy(struct page* p, unsigned int order) {
    unsigned long index = p - mem_map; 
    unsigned long buddy_index = index ^ (1 << order);
    return &mem_map[buddy_index];
}

struct page* alloc_pages(unsigned int order) {
    int current_order = order;
    while (current_order <= MAX_ORDER && free_area[current_order] == NULL) {
        current_order++;
    }

    if (current_order > MAX_ORDER) {
        uart_puts("[Buddy] Error: Memory exhausted!\n");
        return NULL;
    }

    struct page* target_page = list_pop_front(current_order);
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

void free_pages(struct page* p) {
    p->refcount = 0; 
    p->chunk_size = 0;
    while (p->order < MAX_ORDER) {
        struct page* buddy = get_buddy(p, p->order);
        if (buddy->refcount != 0 || buddy->order != p->order) {
            break;
        }

        list_remove(p->order, buddy);
        if (buddy < p) {
            p = buddy;
        }
        p->order++;
    }

    list_push_front(p->order, p);
}

void* allocate(unsigned long size) {
    struct chunk_pool* target_pool = NULL;
    for (int i = 0; i < POOL_COUNT; i++) {
        if (chunk_pools[i].chunk_size >= size) {
            target_pool = &chunk_pools[i];
            break;
        }
    }

    if (target_pool == NULL) {
        int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        int order = 0;
        while ((1 << order) < num_pages) { order++; }
        
        struct page* p = alloc_pages(order);
        // uart_puts("Sucess allocate Page\n");
        if (p == NULL) return NULL; 
        return (void*)page_2_physical(p);
    }

    if (target_pool->free_list == NULL) {
        struct page* p = alloc_pages(0); 
        if (p == NULL) return NULL;      
        p->chunk_size = target_pool->chunk_size; 

        unsigned long page_addr = page_2_physical(p);
        int chunk_count = PAGE_SIZE / target_pool->chunk_size;
        
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

    void* allocated_chunk = target_pool->free_list;
    target_pool->free_list = *(void**)allocated_chunk; 
    // uart_puts("Sucess allocate\n");
    return allocated_chunk;
}

void free(void* ptr) {
    if (ptr == NULL) return;

    unsigned long base_page_addr = (unsigned long)ptr & ~(PAGE_SIZE - 1);
    struct page* p = physical_2_page(base_page_addr);
    
    if (p->chunk_size == 0) {
        free_pages(p);
        // uart_puts("Sucess free Page\n");
        return; 
    }

    int chunk_size = p->chunk_size;
    struct chunk_pool* target_pool = NULL;
    
    for (int i = 0; i < POOL_COUNT; i++) {
        if (chunk_pools[i].chunk_size == chunk_size) {
            target_pool = &chunk_pools[i];
            break;
        }
    }

    if (target_pool != NULL) {
        *(void**)ptr = target_pool->free_list;
        target_pool->free_list = ptr;
    }

    // uart_puts("Sucess free\n");
}

void memory_reserve(unsigned long base, unsigned long size) {
    unsigned long start_pfn = (base - PHYSICAL_MEM_START) / PAGE_SIZE;
    unsigned long end_pfn = (base + size - PHYSICAL_MEM_START + PAGE_SIZE - 1) / PAGE_SIZE;

    for (int order = MAX_ORDER; order >= 0; order--) {
        struct page* curr = free_area[order];
        while (curr != NULL) {
            struct page* next_node = curr->next; 
            unsigned long block_start_pfn = curr - mem_map;
            unsigned long block_end_pfn = block_start_pfn + (1UL << order);

            if (block_end_pfn <= start_pfn || block_start_pfn >= end_pfn) {
            } 
            else if (block_start_pfn >= start_pfn && block_end_pfn <= end_pfn) {
                list_remove(order, curr);
            } 
            else {
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

void early_reserve(unsigned long start, unsigned long size) {
    reserved_regions[num_reserved_regions].start = start;
    reserved_regions[num_reserved_regions].size = size;
    num_reserved_regions++;
}

void all_mem_addr_size(unsigned long dtb_addr){
    unsigned long kernel_base = (unsigned long)_start;
    unsigned long kernel_size = (unsigned long)_end - kernel_base;


    early_reserve(kernel_base, kernel_size);
    reserve_initramfs(dtb_addr);
    reserve_dtb((const void*)dtb_addr);
    dtb_reg_reserve((const void*)dtb_addr);
    dtb_static_reserve((const void*)dtb_addr);

}

void allocate_mem_map() {
    unsigned long map_size = NUM_PAGES * sizeof(struct page);
    unsigned long try_start = PHYSICAL_MEM_START;
    int found = 0;
    while (try_start + map_size <= PHYSICAL_MEM_END) {
        int collision = 0;
        for (int i = 0; i < num_reserved_regions; i++) {
            unsigned long res_start = reserved_regions[i].start;
            unsigned long res_end = res_start + reserved_regions[i].size;
            if (try_start < res_end && (try_start + map_size) > res_start) {
                try_start = (res_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                collision = 1;
                break; 
            }
        }

        if (!collision) {
            found = 1;
            break;
        }
    }
    
    if (!found) {
        uart_puts("Panic: Cannot find enough space for mem_map!\n");
        while(1);
    }

    mem_map = (struct page*)try_start;
    early_reserve(try_start, map_size);
}

void mem_init(unsigned long dtb_addr){
    PHYSICAL_MEM_START = get_address((const void*)dtb_addr, "/memory", "reg");
    unsigned long phy_mem_size = get_size((const void*)dtb_addr, "/memory", "reg");
    PHYSICAL_MEM_END = PHYSICAL_MEM_START + phy_mem_size;
    all_mem_addr_size(dtb_addr);
    allocate_mem_map();

    for (int i = 0; i <= MAX_ORDER; i++) free_area[i] = NULL;
    for (int i = 0; i < POOL_COUNT; i++) chunk_pools[i].free_list = NULL;

    for (size_t i = 0; i < NUM_PAGES; i++) {
        mem_map[i].order = 0;
        mem_map[i].refcount = 0;
        mem_map[i].chunk_size = 0;
        mem_map[i].next = NULL;
        mem_map[i].prev = NULL;
    }

    for (int i = 0; i <= NUM_PAGES - (1 << MAX_ORDER); i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        list_push_front(MAX_ORDER, &mem_map[i]);
    }

    for (int i = 0; i < num_reserved_regions; i++) {
        memory_reserve(reserved_regions[i].start, reserved_regions[i].size);
    }
}


void test() {
    uart_puts("Testing memory allocation...\n");
    char *ptr1 = (char *)allocate(4000);
    char *ptr2 = (char *)allocate(8000);
    char *ptr3 = (char *)allocate(4000);
    char *ptr4 = (char *)allocate(4000);

    free(ptr1);
    free(ptr2);
    free(ptr3);
    free(ptr4);

    /* Test kmalloc */
    uart_puts("Testing dynamic allocator...\n");
    char *kmem_ptr1 = (char *)allocate(16);
    char *kmem_ptr2 = (char *)allocate(32);
    char *kmem_ptr3 = (char *)allocate(64);
    char *kmem_ptr4 = (char *)allocate(128);

    free(kmem_ptr1);
    free(kmem_ptr2);
    free(kmem_ptr3);
    free(kmem_ptr4);

    char *kmem_ptr5 = (char *)allocate(16);
    char *kmem_ptr6 = (char *)allocate(32);

    free(kmem_ptr5);
    free(kmem_ptr6);

    // Test allocate new page if the cache is not enough
    void *kmem_ptr[102];
    for (int i=0; i<100; i++) {
        kmem_ptr[i] = (char *)allocate(128);
    }
    for (int i=0; i<100; i++) {
        free(kmem_ptr[i]);
    }

    // Test exceeding the maximum size
    char *kmem_ptr7 = (char *)allocate(MAX_ALLOC_SIZE + 1);
    if (kmem_ptr7 == NULL) {
        uart_puts("Allocation failed as expected for size > MAX_ALLOC_SIZE\n");
    }
    else {
        uart_puts("Unexpected allocation success for size > MAX_ALLOC_SIZE\n");
        free(kmem_ptr7);
    }
}