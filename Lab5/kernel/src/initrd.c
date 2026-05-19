#include "initrd.h"
#include "fdt.h"
#include "config.h"
#include "tool.h"
#include "uart.h"
#include "buddy.h"

unsigned long INITRD_BASE = 0xFFFFFFFF ;

void set_initrd_address(unsigned long dtb_addr){
    INITRD_BASE = initrd_address(dtb_addr);
}
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
/*
 * Load a user program from the initramfs by filename.
 * This function scans the CPIO archive, finds the matching file,
 * allocates memory for it, copies the file content into memory,
 * and returns the loaded program address. It also stores the file size
 * through `size` if `size` is not NULL.
 */
void* load_user_program(const char* filename, unsigned long* size) {
    if (filename == 0 || filename[0] == '\0') {
        uart_puts("load_user_program: empty filename\n");
        return 0;
    }

    const char* ptr = (const char*)INITRD_BASE;

    if (ptr == 0) {
        uart_puts("load_user_program: INITRD_BASE is NULL\n");
        return 0;
    }

    while (1) {
        struct cpio_t* header = (struct cpio_t*)ptr;

        if (strncmp(header->magic, "070701", 6) != 0) {
            break;
        }

        int namesize = hextoi(header->namesize, 8);
        int filesize = hextoi(header->filesize, 8);

        const char* current_filename = ptr + sizeof(struct cpio_t);

        if (strcmp(current_filename, "TRAILER!!!") == 0) {
            break;
        }

        const char* current_filedata = (const char*)align_up(current_filename + namesize, 4);

        if (strcmp(current_filename, filename) == 0) {
            void* prog_addr = allocate(filesize);

            if (prog_addr == 0) {
                uart_puts("load_user_program: allocate failed\n");
                return 0;
            }

            char* dst = (char*)prog_addr;
            const char* src = current_filedata;

            for (int i = 0; i < filesize; i++) {
                dst[i] = src[i];
            }

            if (size != 0) *size = filesize;

            return prog_addr;
        }

        ptr = (const char*)align_up(current_filedata + filesize, 4);
    }

    uart_puts("load_user_program: file not found: ");
    uart_puts(filename);
    uart_puts("\n");

    return 0;
}