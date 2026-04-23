#include <stdint.h>
#include <stddef.h>

#ifndef OPIORQEMU
  #define OPIORQEMU 0
#endif

extern uint32_t bswap32(uint32_t x);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_int(int num);
extern int hextoi(const char* s, int n);
extern char* itoa(char *str, int num);
extern const void* align_up(const void* ptr, size_t align);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, int n);
extern void uart_puts_left_aligned(const char *data, int width);
extern int fdt_path_offset(const void* fdt, const char* path);
extern const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp);
extern void early_reserve(unsigned long start, unsigned long size);

struct cpio_t {
    char magic[6];      // Magic number: "070701" indicates newc format
    char ino[8];        // Inode number
    char mode[8];       // File mode and type
    char uid[8];        // User ID
    char gid[8];        // Group ID
    char nlink[8];      // Number of hard links
    char mtime[8];      // Modification time
    char filesize[8];   // Size of the file data in bytes
    char devmajor[8];   // Major device number
    char devminor[8];   // Minor device number
    char rdevmajor[8];  // Major device number (for device files)
    char rdevminor[8];  // Minor device number (for device files)
    char namesize[8];   // Size of the filename
    char check[8];      // Checksum
};
/*
 * Parses the "/chosen" node in the DTB to locate the initial RAM disk (initramfs).
 * Extracts its start and end addresses (handling 32/64-bit formats) and calls 
 * early_reserve() to protect the initramfs region from being overwritten.
 */
void reserve_initramfs(unsigned long dtb_addr) {
    const void *fdt = (const void *)dtb_addr;
    int node = fdt_path_offset(fdt, "/chosen");
    if (node >= 0) {
        int len = 0;
        const uint32_t *start_reg = fdt_getprop(fdt, node, "linux,initrd-start", &len);
        const uint32_t *end_reg = fdt_getprop(fdt, node, "linux,initrd-end", &len);
        unsigned long start_addr = 0;
        unsigned long end_addr = 0;

        if (OPIORQEMU == 0) {
            start_addr = bswap32(start_reg[0]);
            end_addr = bswap32(end_reg[0]);
        } else if (OPIORQEMU == 1) {
            uint64_t start_high = bswap32(start_reg[0]);
            uint64_t start_low  = bswap32(start_reg[1]);
            uint64_t end_high = bswap32(end_reg[0]);
            uint64_t end_low  = bswap32(end_reg[1]);
            start_addr = (start_high << 32) | start_low;
            end_addr = (end_high << 32) | end_low;
        } 

        early_reserve(start_addr, end_addr - start_addr);
    }
}
/*
 * Retrieve the physical memory address where the Initrd is loaded.
 * It reads the "/chosen" node's "linux,initrd-start" property from the DTB.
 */
unsigned long initrd_address(unsigned long dtb_addr) {
    const void *fdt = (const void *)dtb_addr;

    int node = fdt_path_offset(fdt, "/chosen");
    if (node >= 0) {
        int len = 0;
        const uint32_t *reg = fdt_getprop(fdt, node, "linux,initrd-start", &len);
        unsigned long addr = 0;

        // Parse the address based on how many cells the parent specified
        if (OPIORQEMU == 0) {
            addr = bswap32(reg[0]);
        } else if (OPIORQEMU == 1) {
            uint64_t high = bswap32(reg[0]);
            uint64_t low  = bswap32(reg[1]);
            addr = (high << 32) | low;
        } 
        return (unsigned long)addr; 
    }

    return -1;
}
/*
 * List all files contained within the CPIO archive.
 */
void initrd_list(const void* rd) {
    if (rd == 0) {
        uart_puts("Error: Initrd address is NULL (0)!\n");
        return;
    }
    const char *ptr = (const char *)rd;
    int total_num = 0 ;
    while (1) {
        struct cpio_t *header = (struct cpio_t *)ptr;
        // Check if the magic number matches the CPIO newc format
        if (strncmp(header->magic, "070701", 6) != 0) {
            break;
        }
        // Convert ASCII hex strings to integers
        int namesize = hextoi(header->namesize, 8);
        int filesize = hextoi(header->filesize, 8);
        // The filename immediately follows the header
        const char *current_filename = ptr + sizeof(struct cpio_t);
        // CPIO standard defines "TRAILER!!!" as the end-of-archive marker
        if (strcmp(current_filename, "TRAILER!!!") == 0) {
            uart_puts("Total ");
            uart_int(total_num);
            uart_puts(" files.\n");
            break;
        }

        char* size_str = "\0";
        uart_puts_left_aligned(itoa(size_str, filesize), 7);
        uart_puts(" ");
        uart_puts(current_filename);
        uart_puts("\n");
        // CPIO format requires file data to be 4-byte aligned
        const char *current_filedata = (const char *)align_up(current_filename + namesize, 4);
        // The next header starts after the file data, also 4-byte aligned
        ptr = (const char *)align_up(current_filedata + filesize, 4);
        total_num++;
    }
}
/*
 * Find a specific file by name in the CPIO archive and print its contents.
 */
void initrd_cat(const void* rd, const char* filename) {
    const char *ptr = (const char *)rd;
    while (1) {
        struct cpio_t *header = (struct cpio_t *)ptr;
        if (strncmp(header->magic, "070701", 6) != 0) {
            break;
        }

        int namesize = hextoi(header->namesize, 8);
        int filesize = hextoi(header->filesize, 8);
        const char *current_filename = ptr + sizeof(struct cpio_t);
        // File not found before the end of the archive
        if (strcmp(current_filename, "TRAILER!!!") == 0) {
            break;
        }

        const char *current_filedata = (const char *)align_up(current_filename + namesize, 4);
        // If the filename matches exactly, print its data byte by byte
        if (strcmp(current_filename, filename) == 0) {
            for (int i = 0; i < filesize; i++) {
                uart_putc(*current_filedata++);
            }
            return; 
        }
        // Skip to the next header
        ptr = (const char *)align_up(current_filedata + filesize, 4);
    }
}