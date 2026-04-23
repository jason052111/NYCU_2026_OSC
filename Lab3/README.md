# OSC Lab 3: Memory Management

This is the memory management system I implemented for my Operating Systems course. Developed for both physical hardware (Orange Pi RV2) and QEMU environments, this project features core functionalities ranging from early hardware information parsing to high-performance memory allocation.

## 🚀 Features

### 1. Early Memory Reservation
Before initializing the Buddy System, the system scans and protects critical memory regions.
- **DTB Parsing**: Manually parses the Device Tree Blob to extract `/reserved-memory` and `mem_rsvmap`.
- **Component Protection**: Automatically reserves the memory addresses occupied by the Kernel image, Initramfs, and the DTB itself.
- **Collision Avoidance**: Implements an evasion algorithm to ensure the memory bitmap (`mem_map`) is placed at a safe physical address.

### 2. Buddy System Allocator
Implemented a physical page allocator supporting allocations from Order 0 (4KB) to Order 10 (4MB).
- **Fast Allocation**: Maintains free lists via the `free_area` array.
- **Dynamic Splitting & Merging**: Supports automatic splitting of large memory blocks and merging of adjacent buddies to optimize memory utilization.
- **Page Management**: Manages page states using `struct page`.

### 3. Fixed-size Slab Allocator
Implemented a dynamic pool allocator for memory requests smaller than 4KB.
- **Zero Overhead**: Utilizes the Intrusive Linked List technique by storing the `Next` pointer directly within the free chunk, avoiding extra management memory overhead.
- **Efficiency**: Pre-slices pages for common sizes (e.g., 32, 64, 128 bytes), achieving constant time complexity for allocation and deallocation.

## 🛠 Implementation Details

### Collision Detection
```c
while (try_start + map_size <= PHYSICAL_MEM_END) {
    // Iterate through all reserved regions to ensure mem_map doesn't overwrite the Kernel or DTB
    if (overlap(try_start, map_size, reserved_regions[i])) {
        try_start = align_up(reserved_end);
        continue;
    }
}
```

### Intrusive Free List
In the Slab allocator, we write the pointer directly into the free chunk:
`*(void**)current_chunk = (void*)next_chunk;`
This allows the system to efficiently track free space without requiring additional data structures.

## 🔍 Debugging
The project includes a built-in `dump()` function to monitor the remaining free blocks at each level of the Buddy System in real-time:
```text
========== Free Area Dump ==========
free_area[10] 2
free_area[9] 0
...
free_area[0] 5
====================================
```
