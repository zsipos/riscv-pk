// See LICENSE for license details.

#include <string.h>
#include "uart_lr.h"
#include "fdt.h"

volatile unsigned long *uart_lr;

#define UART_REG_RXTX       0
#define UART_REG_TXFULL     1
#define UART_REG_RXEMPTY    2
#define UART_REG_EV_STATUS  3
#define UART_REG_EV_PENDING 4
#define UART_REG_EV_ENABLE  5

void uart_lr_putchar(uint8_t c)
{
    while ((uart_lr[UART_REG_TXFULL] & 0x01)); // wait while tx-buffer full
    uart_lr[UART_REG_RXTX] = c;
}

int uart_lr_getchar()
{
    int c = -1;
    if (!(uart_lr[UART_REG_RXEMPTY] & 0x01)) { // if rx-buffer not empty
        c = uart_lr[UART_REG_RXTX];
        uart_lr[UART_REG_EV_PENDING] = 0x02; // ack (UART_EV_RX)
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
    uart_lr = (void *)(uintptr_t)scan->reg;
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
