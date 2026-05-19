#ifndef TOOL_H
#define TOOL_H

#include <stdint.h>
#include <stddef.h>

uint32_t bswap32(uint32_t x);
int hextoi(const char* s, int n);
char* itoa(char* str, int num);
void reverse(char* str, int length);
const void* align_up(const void* ptr, size_t align);
size_t strlen(const char* s);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, int n);
char* strchr(const char* s, int c);
const char* get_argument(const char* input);
int atoi_simple(const char* s);
char* strdup_simple(const char* src);
void* memcpy(void* dst, const void* src, int n);
void mem_copy(void* dst, const void* src, unsigned long size);
void mem_zero(void* ptr, unsigned long size);

#endif