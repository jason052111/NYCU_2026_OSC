#include <stdint.h>
#include <stddef.h>

extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_int(int num);
extern int hextoi(const char* s, int n);
extern char* itoa(char *str, int num);
extern const void* align_up(const void* ptr, size_t align);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, int n);
extern void uart_puts_left_aligned(const char *data, int width);
extern unsigned long get_address(const void* dtb_addr, const char *node_path, const char *prop_name);

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
 * Retrieve the physical memory address where the Initrd is loaded.
 * It reads the "/chosen" node's "linux,initrd-start" property from the DTB.
 */
unsigned long initrd_address(unsigned long dtb_addr) {
    const void *fdt = (const void *)dtb_addr;
    return get_address(fdt, "/chosen", "linux,initrd-start");
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