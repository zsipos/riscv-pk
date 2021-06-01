// See LICENSE for license details.

#include <string.h>
#include "uart_lr.h"
#include "fdt.h"
#include "csroffsets.h"

volatile void *uart_lr;

static inline uint8_t reg_read(unsigned int reg) 
{
	return *((volatile uint32_t*)(uart_lr+reg));
}

static inline void reg_write(unsigned int reg, uint8_t val)
{
	*((volatile uint32_t*)(uart_lr+reg)) = val;
}

void uart_lr_putchar(uint8_t c)
{
    while ((reg_read(LITEX_UART_TXFULL_REG) & 0x01)); // wait while tx-buffer full
    reg_write(LITEX_UART_RXTX_REG, c);
}

int uart_lr_getchar()
{
    int c = -1;
    if (!(reg_read(LITEX_UART_RXEMPTY_REG) & 0x01)) { // if rx-buffer not empty
        c = reg_read(LITEX_UART_RXTX_REG);
        reg_write(LITEX_UART_EV_PENDING_REG, 0x02); // ack 
    }
    return c;
}

struct uart_lr_scan
{
    int compat;
    uint64_t reg;
};

static void uart_lr_open(const struct fdt_scan_node *node, void *extra)
{
    struct uart_lr_scan *scan = (struct uart_lr_scan *)extra;
    memset(scan, 0, sizeof(*scan));
}

static void uart_lr_prop(const struct fdt_scan_prop *prop, void *extra)
{
    struct uart_lr_scan *scan = (struct uart_lr_scan *)extra;
    if (!strcmp(prop->name, "compatible") &&
        !strcmp((const char *)prop->value, "litex,uart0")) {
        scan->compat = 1;
    } else if (!strcmp(prop->name, "reg")) {
        fdt_get_address(prop->node->parent, prop->value, &scan->reg);
    }
}

static void uart_lr_done(const struct fdt_scan_node *node, void *extra)
{
    struct uart_lr_scan *scan = (struct uart_lr_scan *)extra;
    if (!scan->compat || !scan->reg || uart_lr)
        return;

    // Initialize LiteX UART
    uart_lr = (void *)scan->reg;
    // FIXME: the BIOS already initialized the registers, should we re-init?
}

void query_uart_lr(uintptr_t fdt)
{
    struct fdt_cb cb;
    struct uart_lr_scan scan;

    memset(&cb, 0, sizeof(cb));
    cb.open = uart_lr_open;
    cb.prop = uart_lr_prop;
    cb.done = uart_lr_done;
    cb.extra = &scan;

    fdt_scan(fdt, &cb);
}
