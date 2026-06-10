#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

#if OPIORQEMU
  #define UART_OFF(x) ((x) << 2)
#else
  #define UART_OFF(x) (x)
#endif

#define UART_BUF_SIZE 128

extern unsigned long UART_IRQ;
extern unsigned long UART_BASE;
extern unsigned long PLIC_BASE;

#define UART_RBR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x0)))
#define UART_THR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x0)))
#define UART_IER ((volatile unsigned char*)(UART_BASE + UART_OFF(0x1)))
#define UART_IIR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x2)))
#define UART_MCR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x4)))
#define UART_LSR ((volatile unsigned char*)(UART_BASE + UART_OFF(0x5)))

#define LSR_DR   (1 << 0)
#define LSR_TDRQ (1 << 5)

#define PLIC_PRIORITY(irq)   ((volatile unsigned int*)(PLIC_BASE + (irq) * 4))
#define PLIC_ENABLE(hart)    ((volatile unsigned int*)(PLIC_BASE + 0x002080 + (hart) * 0x0100))
#define PLIC_THRESHOLD(hart) ((volatile unsigned int*)(PLIC_BASE + 0x201000 + (hart) * 0x2000))
#define PLIC_CLAIM(hart)    ((volatile unsigned int*)(PLIC_BASE + 0x201004 + (hart) * 0x2000))

volatile unsigned char* uart_rbr_addr(void);

int uart_buf_is_empty(void);
int uart_buf_is_full(void);
void uart_buf_push(char c);
char uart_buf_pop(void);
char uart_getc(void);
void uart_putc(char c);
void uart_puts(const char* s);
void uart_puts_left_aligned(const char* data, int width);
void uart_hex(unsigned long h);
void uart_int(int num);
void uart_init(unsigned long dtb_addr);
void plic_init(unsigned long hartid);
int plic_claim(unsigned long hartid);
void plic_complete(int irq, unsigned long hartid);

#endif