#include <stdint.h>
#include <stddef.h>

void reverse(char *str, int length);
/*
 * Swap the byte order of a 32-bit unsigned integer.
 */
uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) <<  8) |
           ((x & 0x00FF0000U) >>  8) |
           ((x & 0xFF000000U) >> 24);
}
/*
 * Convert a hexadecimal string to an integer.
 * 'n' specifies the maximum number of characters to parse.
 * Note: This implementation assumes uppercase letters ('A'-'F').
 */
int hextoi(const char* s, int n) {
    int r = 0;
    while (n-- > 0) {
        r = r << 4;
        if (*s >= 'A')
            r += *s++ - 'A' + 10;
        else if (*s >= '0')
            r += *s++ - '0';
    }
    return r;
}
/*
 * Convert a signed integer to a null-terminated string.
 * Handles negative numbers and the zero case explicitly.
 */
char* itoa(char *str, int num) {
    int i = 0;
    int is_negative = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    if (num < 0) {
        is_negative = 1;
        num = -num; 
    }

    while (num != 0) {
        int rem = num % 10;
        str[i++] = rem + '0';
        num = num / 10;
    }

    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0'; 
    reverse(str, i);
    return str;
}
/*
 * Reverse a given string in-place.
 */
void reverse(char *str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}
/*
 * Align a memory pointer upwards to the nearest multiple of 'align'.
 * 'align' must be a power of 2 (e.g., 4, 8, 16).
 * Extremely important for FDT parsing where blocks must be 4-byte aligned.
 */
const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}
/*
 * Calculate the length of a null-terminated C-string.
 */
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}
/*
 * Compare two strings. 
 * Returns 0 if they are identical, otherwise the difference between the first non-matching characters.
 */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}
/*
 * Compare up to 'n' characters of two strings.
 */
int strncmp(const char *s1, const char *s2, int n) {
    if (n == 0) return 0;
    do {
        if (*s1 != *s2++)
            return *(const unsigned char *)s1 - *(const unsigned char *)--s2;
        if (*s1++ == 0)
            break;
    } while (--n != 0);
    return 0;
}
/*
 * Find the first occurrence of a character 'c' in a string 's'.
 * Returns a pointer to the matching character, or NULL if not found.
 */
char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) {
            return NULL;
        }
    }
    return (char *)s;
}
/*
 * Simple Command-Line Interface helper.
 * Skips the first word and trailing spaces to return a pointer 
 * to the start of the argument string.
 * Example: "cat osc.txt" -> returns pointer to "osc.txt".
 */
const char* get_argument(const char *input) {
    int i = 0;

    while (input[i] != '\0' && input[i] != ' ') {
        i++;
    }

    if (input[i] == '\0') {
        return &input[i]; 
    }

    while (input[i] == ' ') {
        i++;
    }

    return &input[i];
}