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
    for (int shift = 28; shift >= 0; shift -= 4)
        uart_putc(hex[(val >> shift) & 0xF]);
}

static void uart_puthex8(uint8_t val) {
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(val >> 4) & 0xF]);
    uart_putc(hex[val & 0xF]);
}

/* --- Construir estado inicial --- */
static void build_state(uint32_t *state, uint32_t counter,
                        uint32_t n0, uint32_t n1, uint32_t n2) {
    state[0]  = 0x61707865;
    state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;
    state[3]  = 0x6b206574;
    state[4]  = 0x03020100;
    state[5]  = 0x07060504;
    state[6]  = 0x0b0a0908;
    state[7]  = 0x0f0e0d0c;
    state[8]  = 0x13121110;
    state[9]  = 0x17161514;
    state[10] = 0x1b1a1918;
    state[11] = 0x1f1e1d1c;
    state[12] = counter;
    state[13] = n0;
    state[14] = n1;
    state[15] = n2;
}

/* --- Test 1: quarter_round --- */
static void test_quarter_round(void) {
    uint32_t state[4];
    state[0] = 0x11111111;
    state[1] = 0x01020304;
    state[2] = 0x9b8d6f43;
    state[3] = 0x01234567;

    uart_puts("=== TEST QUARTER ROUND ===\n");
    uart_puts("Antes:\n");
    uart_puts("a = "); uart_puthex32(state[0]); uart_puts("\n");
    uart_puts("b = "); uart_puthex32(state[1]); uart_puts("\n");
    uart_puts("c = "); uart_puthex32(state[2]); uart_puts("\n");
    uart_puts("d = "); uart_puthex32(state[3]); uart_puts("\n");

    chacha20_quarter_round(state, 0, 1, 2, 3);

    uart_puts("Despues:\n");
    uart_puts("a = "); uart_puthex32(state[0]); uart_puts("\n");
    uart_puts("b = "); uart_puthex32(state[1]); uart_puts("\n");
    uart_puts("c = "); uart_puthex32(state[2]); uart_puts("\n");
    uart_puts("d = "); uart_puthex32(state[3]); uart_puts("\n");

    uart_puts("Esperado:\n");
    uart_puts("a = 0xea2a92f4\n");
    uart_puts("b = 0xcb1cf8ce\n");
    uart_puts("c = 0x4581472e\n");
    uart_puts("d = 0x5881c4bb\n");
}

/* --- Test 2: chacha20_block --- */
static void test_chacha20_block(void) {
    uint32_t state[16];
    build_state(state, 1, 0x09000000, 0x4a000000, 0x00000000);

    uint8_t output[64];

    uart_puts("\n=== TEST CHACHA20 BLOCK ===\n");
    uart_puts("Estado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n");
        else uart_puts("  ");
    }

    chacha20_block(state, output);

    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(output[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado (RFC 8439 seccion 2.3.2):\n");
    uart_puts("10 f1 e7 e4 d1 3b 59 15 50 0f dd 1f a3 20 71 c4\n");
    uart_puts("c7 d1 f4 c7 33 c0 68 03 04 22 aa 9a c3 d4 6c 4e\n");
    uart_puts("d2 82 64 46 07 9f aa 09 14 c2 d7 05 d9 8b 02 a2\n");
    uart_puts("b5 12 9c d1 de 16 4e b9 cb d0 83 e8 a2 50 3c 4e\n");

    uint8_t expected[64];
    expected[0]  = 0x10; expected[1]  = 0xf1; expected[2]  = 0xe7; expected[3]  = 0xe4;
    expected[4]  = 0xd1; expected[5]  = 0x3b; expected[6]  = 0x59; expected[7]  = 0x15;
    expected[8]  = 0x50; expected[9]  = 0x0f; expected[10] = 0xdd; expected[11] = 0x1f;
    expected[12] = 0xa3; expected[13] = 0x20; expected[14] = 0x71; expected[15] = 0xc4;
    expected[16] = 0xc7; expected[17] = 0xd1; expected[18] = 0xf4; expected[19] = 0xc7;
    expected[20] = 0x33; expected[21] = 0xc0; expected[22] = 0x68; expected[23] = 0x03;
    expected[24] = 0x04; expected[25] = 0x22; expected[26] = 0xaa; expected[27] = 0x9a;
    expected[28] = 0xc3; expected[29] = 0xd4; expected[30] = 0x6c; expected[31] = 0x4e;
    expected[32] = 0xd2; expected[33] = 0x82; expected[34] = 0x64; expected[35] = 0x46;
    expected[36] = 0x07; expected[37] = 0x9f; expected[38] = 0xaa; expected[39] = 0x09;
    expected[40] = 0x14; expected[41] = 0xc2; expected[42] = 0xd7; expected[43] = 0x05;
    expected[44] = 0xd9; expected[45] = 0x8b; expected[46] = 0x02; expected[47] = 0xa2;
    expected[48] = 0xb5; expected[49] = 0x12; expected[50] = 0x9c; expected[51] = 0xd1;
    expected[52] = 0xde; expected[53] = 0x16; expected[54] = 0x4e; expected[55] = 0xb9;
    expected[56] = 0xcb; expected[57] = 0xd0; expected[58] = 0x83; expected[59] = 0xe8;
    expected[60] = 0xa2; expected[61] = 0x50; expected[62] = 0x3c; expected[63] = 0x4e;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (output[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("Verificacion block: CORRECTO\n");
    else    uart_puts("Verificacion block: ERROR\n");
}

/* --- Programa principal --- */
int main(void) {
    test_quarter_round();
    test_chacha20_block();

    while (1) {}
    return 0;
}
