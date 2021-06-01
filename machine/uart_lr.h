// See LICENSE for license details.

#ifndef _RISCV_UARTLR_H
#define _RISCV_UARTLR_H

#include <stdint.h>

extern volatile void *uart_lr;

void uart_lr_putchar(uint8_t ch);
int uart_lr_getchar();
void query_uart_lr(uintptr_t dtb);

#endif
