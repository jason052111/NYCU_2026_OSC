#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>
#include <stddef.h>

#define MAX_RESERVED_REGIONS 32
#define PAGE_SIZE 4096
#define MAX_ORDER 10
#define POOL_COUNT 8

#define NUM_PAGES ((PHYSICAL_MEM_END - PHYSICAL_MEM_START) / PAGE_SIZE)
#define MAX_ALLOC_SIZE ((1 << MAX_ORDER) * PAGE_SIZE)

extern unsigned long PHYSICAL_MEM_START;
extern unsigned long PHYSICAL_MEM_END;

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

extern struct chunk_pool chunk_pools[POOL_COUNT];
extern struct page* mem_map;
extern struct mem_region reserved_regions[MAX_RESERVED_REGIONS];
extern int num_reserved_regions;
extern struct page* free_area[MAX_ORDER + 1];

struct page* physical_2_page(unsigned long addr);
unsigned long page_2_physical(struct page* p);
void list_push_front(int order, struct page* p);
void list_remove(int order, struct page* p);
struct page* list_pop_front(int order);
struct page* get_buddy(struct page* p, unsigned int order);
struct page* alloc_pages(unsigned int order);
void free_pages(struct page* p);
void* allocate(unsigned long size);
void free(void* ptr);
void memory_reserve(unsigned long base, unsigned long size);
void early_reserve(unsigned long start, unsigned long size);
void all_mem_addr_size(unsigned long dtb_addr);
void allocate_mem_map(void);
void mem_init(unsigned long dtb_addr);

#endif