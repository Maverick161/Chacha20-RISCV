#include "chacha20.h"

#define UART_BASE 0x10000000UL
static volatile uint8_t *const uart = (volatile uint8_t *)UART_BASE;

static void uart_putc(char c) {
    *uart = (uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_puthex32(uint32_t val) {
    static const char hex[] = "0123456789abcdef";
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex[(val >> shift) & 0xF]);
    }
}

int main(void) {
    uint32_t state[4] = {
        0x11111111,   // state[0] = a
        0x01020304,   // state[1] = b
        0x9b8d6f43,   // state[2] = c
        0x01234567    // state[3] = d
    };

    uart_puts("Antes del quarter round:\n");
    uart_puts("a = "); uart_puthex32(state[0]); uart_puts("\n");
    uart_puts("b = "); uart_puthex32(state[1]); uart_puts("\n");
    uart_puts("c = "); uart_puthex32(state[2]); uart_puts("\n");
    uart_puts("d = "); uart_puthex32(state[3]); uart_puts("\n");

    // Pasar el arreglo + los índices
    chacha20_quarter_round(state, 0, 1, 2, 3);

    uart_puts("\nDespues del quarter round:\n");
    uart_puts("a = "); uart_puthex32(state[0]); uart_puts("\n");
    uart_puts("b = "); uart_puthex32(state[1]); uart_puts("\n");
    uart_puts("c = "); uart_puthex32(state[2]); uart_puts("\n");
    uart_puts("d = "); uart_puthex32(state[3]); uart_puts("\n");

    uart_puts("\nEsperado:\n");
    uart_puts("a = 0xea2a92f4\n");
    uart_puts("b = 0xcb1cf8ce\n");
    uart_puts("c = 0x4581472e\n");
    uart_puts("d = 0x5881c4bb\n");

    while (1) {}

    return 0;
}