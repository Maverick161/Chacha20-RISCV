/* Incluimos nuestro header con las funciones de chacha20 */
#include "chacha20.h"

/* Direccion de memoria donde esta el UART en la maquina virtual QEMU virt */
#define UART_BASE 0x10000000UL

/* Puntero al registro del UART, volatile porque el hardware puede cambiar su valor */
static volatile uint8_t *const uart = (volatile uint8_t *)UART_BASE;

/* Escribe un solo caracter mandando el byte directo al registro del UART */
static void uart_putc(char c) {
    *uart = (uint8_t)c;
}

/* Escribe una cadena caracter por caracter
 * Si encuentra un salto de linea '\n' manda primero '\r' (necesario en terminales serie) */
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r'); /* terminales serie esperan \r\n, no solo \n */
        uart_putc(*s++);                 /* manda el caracter y avanza el puntero */
    }
}

/* Imprime un numero de 32 bits en hexadecimal con prefijo 0x
 * Recorre los 8 nibbles de mas significativo a menos significativo */
static void uart_puthex32(uint32_t val) {
    static const char hex[] = "0123456789abcdef"; /* tabla de digitos hex */
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4)  /* shift baja de 28 a 0 de 4 en 4 */
        uart_putc(hex[(val >> shift) & 0xF]);      /* extrae el nibble y lo convierte a caracter */
}

/* Imprime un byte en hexadecimal sin prefijo (ej: "a3")
 * Separa el nibble alto y el bajo */
static void uart_puthex8(uint8_t val) {
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(val >> 4) & 0xF]); /* nibble alto: bits 7-4 */
    uart_putc(hex[val & 0xF]);        /* nibble bajo: bits 3-0 */
}

/* Llena el array state[16] con el estado inicial de ChaCha20
 * Estructura: [constantes(4)] [clave(8)] [counter(1)] [nonce(3)]
 * Esta version usa la clave fija del RFC 8439 seccion 2.3.2 */
static void build_state(uint32_t *state, uint32_t counter,
                        uint32_t n0, uint32_t n1, uint32_t n2) {
    /* Las 4 constantes magicas de ChaCha20: "expa", "nd 3", "2-by", "te k" en ASCII */
    state[0]  = 0x61707865;
    state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;
    state[3]  = 0x6b206574;
    /* Clave de 256 bits (32 bytes) cargada en little-endian, valores del RFC */
    state[4]  = 0x03020100;
    state[5]  = 0x07060504;
    state[6]  = 0x0b0a0908;
    state[7]  = 0x0f0e0d0c;
    state[8]  = 0x13121110;
    state[9]  = 0x17161514;
    state[10] = 0x1b1a1918;
    state[11] = 0x1f1e1d1c;
    /* Contador de bloque: cuantos bloques de 64 bytes se han generado */
    state[12] = counter;
    /* Nonce de 96 bits (3 palabras de 32 bits) */
    state[13] = n0;
    state[14] = n1;
    state[15] = n2;
}

/* ==========================================================================
 * TEST: quarter round
 * Verifica la operacion basica de ChaCha20: mezcla de 4 palabras con
 * sumas, XORs y rotaciones. Es el bloque fundamental del algoritmo.
 * ========================================================================== */
static void test_quarter_round(void) {
    /* Valores de entrada del ejemplo del RFC 8439 seccion 2.1.1 */
    uint32_t state[4];
    state[0] = 0x11111111;
    state[1] = 0x01020304;
    state[2] = 0x9b8d6f43;
    state[3] = 0x01234567;

    uart_puts("=== TEST QUARTER ROUND ===\n");

    /* Mostramos los valores antes de aplicar la operacion */
    uart_puts("Antes:\n");
    uart_puts("a = "); uart_puthex32(state[0]); uart_puts("\n");
    uart_puts("b = "); uart_puthex32(state[1]); uart_puts("\n");
    uart_puts("c = "); uart_puthex32(state[2]); uart_puts("\n");
    uart_puts("d = "); uart_puthex32(state[3]); uart_puts("\n");

    /* Ejecutamos el quarter round sobre los indices 0,1,2,3 del array */
    chacha20_quarter_round(state, 0, 1, 2, 3);

    /* Mostramos el resultado y el valor esperado segun el RFC para comparar */
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

/* ==========================================================================
 * TEST: chacha20_block (RFC 8439 seccion 2.3.2)
 * Verifica que la funcion de bloque completa genera el keystream correcto.
 * Aplica 20 rondas al estado inicial y suma el resultado con el estado original.
 * ========================================================================== */
static void test_chacha20_block(void) {
    uint32_t state[16];

    /* Construimos el estado con counter=1 y el nonce del RFC seccion 2.3.2 */
    build_state(state, 1, 0x09000000, 0x4a000000, 0x00000000);

    /* Buffer donde se guardaran los 64 bytes de keystream generados */
    uint8_t output[64];

    uart_puts("\n=== TEST CHACHA20 BLOCK ===\n");

    /* Imprimimos el estado inicial en formato de 4 columnas para facilitar lectura */
    uart_puts("Estado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n"); /* salto de linea cada 4 palabras */
        else uart_puts("  ");
    }

    /* Generamos el bloque de 64 bytes de keystream */
    chacha20_block(state, output);

    /* Imprimimos el keystream generado en grupos de 16 bytes por linea */
    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(output[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    /* Valor de referencia del RFC para comparar visualmente */
    uart_puts("\nEsperado (RFC 8439 seccion 2.3.2):\n");
    uart_puts("10 f1 e7 e4 d1 3b 59 15 50 0f dd 1f a3 20 71 c4\n");
    uart_puts("c7 d1 f4 c7 33 c0 68 03 04 22 aa 9a c3 d4 6c 4e\n");
    uart_puts("d2 82 64 46 07 9f aa 09 14 c2 d7 05 d9 8b 02 a2\n");
    uart_puts("b5 12 9c d1 de 16 4e b9 cb d0 83 e8 a2 50 3c 4e\n");

    /* Array con los bytes esperados para la verificacion automatica */
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

    /* Comparamos byte a byte; si alguno difiere marcamos ok=0 */
    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (output[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("Verificacion block: CORRECTO\n");
    else    uart_puts("Verificacion block: ERROR\n");
}

/* ==========================================================================
 * TEST: chacha20_encrypt + descifrado (RFC 8439 seccion 2.4.2)
 * Cifra 114 bytes de texto plano y luego descifra el resultado.
 * Como ChaCha20 es un cifrado de flujo, cifrar y descifrar usan la misma
 * funcion: XOR con el keystream. Aplicarlo dos veces devuelve el original.
 * ========================================================================== */
static void test_chacha20_encrypt(void) {
    /* Texto plano: "Ladies and Gentlemen of the class of '99:..." en ASCII */
    uint8_t plaintext[114];
    plaintext[0]  = 0x4c; plaintext[1]  = 0x61; plaintext[2]  = 0x64;
    plaintext[3]  = 0x69; plaintext[4]  = 0x65; plaintext[5]  = 0x73;
    plaintext[6]  = 0x20; plaintext[7]  = 0x61; plaintext[8]  = 0x6e;
    plaintext[9]  = 0x64; plaintext[10] = 0x20; plaintext[11] = 0x47;
    plaintext[12] = 0x65; plaintext[13] = 0x6e; plaintext[14] = 0x74;
    plaintext[15] = 0x6c; plaintext[16] = 0x65; plaintext[17] = 0x6d;
    plaintext[18] = 0x65; plaintext[19] = 0x6e; plaintext[20] = 0x20;
    plaintext[21] = 0x6f; plaintext[22] = 0x66; plaintext[23] = 0x20;
    plaintext[24] = 0x74; plaintext[25] = 0x68; plaintext[26] = 0x65;
    plaintext[27] = 0x20; plaintext[28] = 0x63; plaintext[29] = 0x6c;
    plaintext[30] = 0x61; plaintext[31] = 0x73; plaintext[32] = 0x73;
    plaintext[33] = 0x20; plaintext[34] = 0x6f; plaintext[35] = 0x66;
    plaintext[36] = 0x20; plaintext[37] = 0x27; plaintext[38] = 0x39;
    plaintext[39] = 0x39; plaintext[40] = 0x3a; plaintext[41] = 0x20;
    plaintext[42] = 0x49; plaintext[43] = 0x66; plaintext[44] = 0x20;
    plaintext[45] = 0x49; plaintext[46] = 0x20; plaintext[47] = 0x63;
    plaintext[48] = 0x6f; plaintext[49] = 0x75; plaintext[50] = 0x6c;
    plaintext[51] = 0x64; plaintext[52] = 0x20; plaintext[53] = 0x6f;
    plaintext[54] = 0x66; plaintext[55] = 0x66; plaintext[56] = 0x65;
    plaintext[57] = 0x72; plaintext[58] = 0x20; plaintext[59] = 0x79;
    plaintext[60] = 0x6f; plaintext[61] = 0x75; plaintext[62] = 0x20;
    plaintext[63] = 0x6f; plaintext[64] = 0x6e; plaintext[65] = 0x6c;
    plaintext[66] = 0x79; plaintext[67] = 0x20; plaintext[68] = 0x6f;
    plaintext[69] = 0x6e; plaintext[70] = 0x65; plaintext[71] = 0x20;
    plaintext[72] = 0x74; plaintext[73] = 0x69; plaintext[74] = 0x70;
    plaintext[75] = 0x20; plaintext[76] = 0x66; plaintext[77] = 0x6f;
    plaintext[78] = 0x72; plaintext[79] = 0x20; plaintext[80] = 0x74;
    plaintext[81] = 0x68; plaintext[82] = 0x65; plaintext[83] = 0x20;
    plaintext[84] = 0x66; plaintext[85] = 0x75; plaintext[86] = 0x74;
    plaintext[87] = 0x75; plaintext[88] = 0x72; plaintext[89] = 0x65;
    plaintext[90] = 0x2c; plaintext[91] = 0x20; plaintext[92] = 0x73;
    plaintext[93] = 0x75; plaintext[94] = 0x6e; plaintext[95] = 0x73;
    plaintext[96] = 0x63; plaintext[97] = 0x72; plaintext[98] = 0x65;
    plaintext[99] = 0x65; plaintext[100] = 0x6e; plaintext[101] = 0x20;
    plaintext[102] = 0x77; plaintext[103] = 0x6f; plaintext[104] = 0x75;
    plaintext[105] = 0x6c; plaintext[106] = 0x64; plaintext[107] = 0x20;
    plaintext[108] = 0x62; plaintext[109] = 0x65; plaintext[110] = 0x20;
    plaintext[111] = 0x69; plaintext[112] = 0x74; plaintext[113] = 0x2e;

    /* Buffer para el texto cifrado */
    uint8_t ciphertext[114];
    /* Buffer para el texto descifrado, deberia ser igual al plaintext original */
    uint8_t decrypted[114];

    uart_puts("\n=== TEST CHACHA20 ENCRYPT ===\n");

    /* --- Cifrado ---
     * Usamos counter=1 y el nonce del RFC seccion 2.4.2 */
    uint32_t state_enc[16];
    build_state(state_enc, 1, 0x00000000, 0x4a000000, 0x00000000);
    chacha20_encrypt(state_enc, plaintext, ciphertext, 114); /* cifra los 114 bytes */

    /* Mostramos el ciphertext generado */
    uart_puts("Ciphertext generado:\n");
    for (int i = 0; i < 114; i++) {
        uart_puthex8(ciphertext[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }
    uart_puts("\n");

    /* Valor de referencia del RFC para comparacion visual */
    uart_puts("Esperado (RFC 8439 seccion 2.4.2):\n");
    uart_puts("6e 2e 35 9a 25 68 f9 80 41 ba 07 28 dd 0d 69 81\n");
    uart_puts("e9 7e 7a ec 1d 43 60 c2 0a 27 af cc fd 9f ae 0b\n");
    uart_puts("f9 1b 65 c5 52 47 33 ab 8f 59 3d ab cd 62 b3 57\n");
    uart_puts("16 39 d6 24 e6 51 52 ab 8f 53 0c 35 9f 08 61 d8\n");
    uart_puts("07 ca 0d bf 50 0d 6a 61 56 a3 8e 08 8a 22 b6 5e\n");
    uart_puts("52 bc 51 4d 16 cc f8 06 81 8c e9 1a b7 79 37 36\n");
    uart_puts("5a f9 0b bf 74 a3 5b e6 b4 0b 8e ed f2 78 5e 42\n");
    uart_puts("87 4d\n");

    /* --- Descifrado ---
     * Reiniciamos el estado con los mismos parametros (misma clave, nonce, counter)
     * y volvemos a aplicar encrypt sobre el ciphertext: el XOR se cancela solo */
    uint32_t state_dec[16];
    build_state(state_dec, 1, 0x00000000, 0x4a000000, 0x00000000);
    chacha20_encrypt(state_dec, ciphertext, decrypted, 114); /* descifra los 114 bytes */

    /* Mostramos el resultado del descifrado, debe coincidir con el plaintext */
    uart_puts("\nDescifrado (debe coincidir con plaintext):\n");
    for (int i = 0; i < 114; i++) {
        uart_puthex8(decrypted[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }
    uart_puts("\n");

    /* Verificacion: comparamos decrypted con el plaintext original byte a byte */
    int ok = 1;
    for (int i = 0; i < 114; i++) {
        if (decrypted[i] != plaintext[i]) { ok = 0; }
    }
    if (ok) uart_puts("Verificacion encrypt: CORRECTO\n");
    else    uart_puts("Verificacion encrypt: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR #1 (RFC 8439 Apendice A.1)
 * Caso mas simple: clave, nonce y counter todos a cero.
 * Sirve para verificar que las constantes y las rondas funcionan desde cero.
 * ========================================================================== */
static void test_tv1_block(void) {
    uint32_t state[16];
    uint8_t  keystream[64];
    uint8_t  expected[64];

    /* Constantes "expand 32-byte k" */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    /* Clave: 32 bytes todos a 0x00 */
    state[4]  = 0x00000000;  state[5]  = 0x00000000;
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x00000000;
    /* Counter = 0: primer bloque */
    state[12] = 0x00000000;
    /* Nonce: 12 bytes todos a 0x00 */
    state[13] = 0x00000000;  state[14] = 0x00000000;
    state[15] = 0x00000000;

    uart_puts("\n=== TEST VECTOR #1 (RFC 8439 Apendice A.1) ===\n");
    uart_puts("Clave  : 32 bytes 0x00\n");
    uart_puts("Nonce  : 12 bytes 0x00\n");
    uart_puts("Counter: 0\n");

    /* Mostramos el estado inicial antes de las rondas */
    uart_puts("\nEstado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n");
        else uart_puts("  ");
    }

    /* Generamos el keystream aplicando las 20 rondas */
    chacha20_block(state, keystream);

    /* Mostramos el keystream resultante */
    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(keystream[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado:\n");
    uart_puts("76 b8 e0 ad a0 f1 3d 90 40 5d 6a e5 53 86 bd 28\n");
    uart_puts("bd d2 19 b8 a0 8d ed 1a a8 36 ef cc 8b 77 0d c7\n");
    uart_puts("da 41 59 7c 51 57 48 8d 77 24 e0 3f b8 d8 4a 37\n");
    uart_puts("6a 43 b8 f4 15 18 a1 1c c3 87 b6 69 b2 ee 65 86\n");

    /* Bytes esperados para verificacion automatica */
    expected[0]  = 0x76; expected[1]  = 0xb8; expected[2]  = 0xe0; expected[3]  = 0xad;
    expected[4]  = 0xa0; expected[5]  = 0xf1; expected[6]  = 0x3d; expected[7]  = 0x90;
    expected[8]  = 0x40; expected[9]  = 0x5d; expected[10] = 0x6a; expected[11] = 0xe5;
    expected[12] = 0x53; expected[13] = 0x86; expected[14] = 0xbd; expected[15] = 0x28;
    expected[16] = 0xbd; expected[17] = 0xd2; expected[18] = 0x19; expected[19] = 0xb8;
    expected[20] = 0xa0; expected[21] = 0x8d; expected[22] = 0xed; expected[23] = 0x1a;
    expected[24] = 0xa8; expected[25] = 0x36; expected[26] = 0xef; expected[27] = 0xcc;
    expected[28] = 0x8b; expected[29] = 0x77; expected[30] = 0x0d; expected[31] = 0xc7;
    expected[32] = 0xda; expected[33] = 0x41; expected[34] = 0x59; expected[35] = 0x7c;
    expected[36] = 0x51; expected[37] = 0x57; expected[38] = 0x48; expected[39] = 0x8d;
    expected[40] = 0x77; expected[41] = 0x24; expected[42] = 0xe0; expected[43] = 0x3f;
    expected[44] = 0xb8; expected[45] = 0xd8; expected[46] = 0x4a; expected[47] = 0x37;
    expected[48] = 0x6a; expected[49] = 0x43; expected[50] = 0xb8; expected[51] = 0xf4;
    expected[52] = 0x15; expected[53] = 0x18; expected[54] = 0xa1; expected[55] = 0x1c;
    expected[56] = 0xc3; expected[57] = 0x87; expected[58] = 0xb6; expected[59] = 0x69;
    expected[60] = 0xb2; expected[61] = 0xee; expected[62] = 0x65; expected[63] = 0x86;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (keystream[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("\nVerificacion TV#1: CORRECTO\n");
    else    uart_puts("\nVerificacion TV#1: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR #2 (RFC 8439 Apendice A.1)
 * Igual que TV#1 pero con counter=1.
 * Verifica que cambiar solo el counter produce un keystream completamente distinto.
 * ========================================================================== */
static void test_tv2_block(void) {
    uint32_t state[16];
    uint8_t  keystream[64];
    uint8_t  expected[64];

    /* Constantes */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    /* Clave: 32 bytes todos a 0x00 */
    state[4]  = 0x00000000;  state[5]  = 0x00000000;
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x00000000;
    /* Counter = 1: segundo bloque, unica diferencia con TV#1 */
    state[12] = 0x00000001;
    /* Nonce: 12 bytes todos a 0x00 */
    state[13] = 0x00000000;  state[14] = 0x00000000;
    state[15] = 0x00000000;

    uart_puts("\n=== TEST VECTOR #2 (RFC 8439 Apendice A.1) ===\n");
    uart_puts("Clave  : 32 bytes 0x00\n");
    uart_puts("Nonce  : 12 bytes 0x00\n");
    uart_puts("Counter: 1\n");

    uart_puts("\nEstado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n");
        else uart_puts("  ");
    }

    chacha20_block(state, keystream);

    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(keystream[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado:\n");
    uart_puts("9f 07 e7 be 55 51 38 7a 98 ba 97 7c 73 2d 08 0d\n");
    uart_puts("cb 0f 29 a0 48 e3 65 69 12 c6 53 3e 32 ee 7a ed\n");
    uart_puts("29 b7 21 76 9c e6 4e 43 d5 71 33 b0 74 d8 39 d5\n");
    uart_puts("31 ed 1f 28 51 0a fb 45 ac e1 0a 1f 4b 79 4d 6f\n");

    expected[0]  = 0x9f; expected[1]  = 0x07; expected[2]  = 0xe7; expected[3]  = 0xbe;
    expected[4]  = 0x55; expected[5]  = 0x51; expected[6]  = 0x38; expected[7]  = 0x7a;
    expected[8]  = 0x98; expected[9]  = 0xba; expected[10] = 0x97; expected[11] = 0x7c;
    expected[12] = 0x73; expected[13] = 0x2d; expected[14] = 0x08; expected[15] = 0x0d;
    expected[16] = 0xcb; expected[17] = 0x0f; expected[18] = 0x29; expected[19] = 0xa0;
    expected[20] = 0x48; expected[21] = 0xe3; expected[22] = 0x65; expected[23] = 0x69;
    expected[24] = 0x12; expected[25] = 0xc6; expected[26] = 0x53; expected[27] = 0x3e;
    expected[28] = 0x32; expected[29] = 0xee; expected[30] = 0x7a; expected[31] = 0xed;
    expected[32] = 0x29; expected[33] = 0xb7; expected[34] = 0x21; expected[35] = 0x76;
    expected[36] = 0x9c; expected[37] = 0xe6; expected[38] = 0x4e; expected[39] = 0x43;
    expected[40] = 0xd5; expected[41] = 0x71; expected[42] = 0x33; expected[43] = 0xb0;
    expected[44] = 0x74; expected[45] = 0xd8; expected[46] = 0x39; expected[47] = 0xd5;
    expected[48] = 0x31; expected[49] = 0xed; expected[50] = 0x1f; expected[51] = 0x28;
    expected[52] = 0x51; expected[53] = 0x0a; expected[54] = 0xfb; expected[55] = 0x45;
    expected[56] = 0xac; expected[57] = 0xe1; expected[58] = 0x0a; expected[59] = 0x1f;
    expected[60] = 0x4b; expected[61] = 0x79; expected[62] = 0x4d; expected[63] = 0x6f;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (keystream[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("\nVerificacion TV#2: CORRECTO\n");
    else    uart_puts("\nVerificacion TV#2: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR #3 (RFC 8439 Apendice A.1)
 * Clave casi cero pero con el ultimo byte (byte 31) = 0x01.
 * Verifica sensibilidad: un solo bit diferente en la clave produce salida distinta.
 * Nota little-endian: byte 31 es el mas significativo de state[11] -> 0x01000000
 * ========================================================================== */
static void test_tv3_block(void) {
    uint32_t state[16];
    uint8_t  keystream[64];
    uint8_t  expected[64];

    /* Constantes */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    /* Clave: bytes 0-30 = 0x00, byte 31 = 0x01
     * state[11] cubre bytes 28-31 de la clave en little-endian */
    state[4]  = 0x00000000;  state[5]  = 0x00000000;
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x01000000; /* byte 31=0x01 queda en bits 31-24 */
    /* Counter = 1 */
    state[12] = 0x00000001;
    /* Nonce: 12 bytes a 0x00 */
    state[13] = 0x00000000;  state[14] = 0x00000000;
    state[15] = 0x00000000;

    uart_puts("\n=== TEST VECTOR #3 (RFC 8439 Apendice A.1) ===\n");
    uart_puts("Clave  : 31 bytes 0x00 + 0x01\n");
    uart_puts("Nonce  : 12 bytes 0x00\n");
    uart_puts("Counter: 1\n");

    uart_puts("\nEstado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n");
        else uart_puts("  ");
    }

    chacha20_block(state, keystream);

    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(keystream[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado:\n");
    uart_puts("3a eb 52 24 ec f8 49 92 9b 9d 82 8d b1 ce d4 dd\n");
    uart_puts("83 20 25 e8 01 8b 81 60 b8 22 84 f3 c9 49 aa 5a\n");
    uart_puts("8e ca 00 bb b4 a7 3b da d1 92 b5 c4 2f 73 f2 fd\n");
    uart_puts("4e 27 36 44 c8 b3 61 25 a6 4a dd eb 00 6c 13 a0\n");

    expected[0]  = 0x3a; expected[1]  = 0xeb; expected[2]  = 0x52; expected[3]  = 0x24;
    expected[4]  = 0xec; expected[5]  = 0xf8; expected[6]  = 0x49; expected[7]  = 0x92;
    expected[8]  = 0x9b; expected[9]  = 0x9d; expected[10] = 0x82; expected[11] = 0x8d;
    expected[12] = 0xb1; expected[13] = 0xce; expected[14] = 0xd4; expected[15] = 0xdd;
    expected[16] = 0x83; expected[17] = 0x20; expected[18] = 0x25; expected[19] = 0xe8;
    expected[20] = 0x01; expected[21] = 0x8b; expected[22] = 0x81; expected[23] = 0x60;
    expected[24] = 0xb8; expected[25] = 0x22; expected[26] = 0x84; expected[27] = 0xf3;
    expected[28] = 0xc9; expected[29] = 0x49; expected[30] = 0xaa; expected[31] = 0x5a;
    expected[32] = 0x8e; expected[33] = 0xca; expected[34] = 0x00; expected[35] = 0xbb;
    expected[36] = 0xb4; expected[37] = 0xa7; expected[38] = 0x3b; expected[39] = 0xda;
    expected[40] = 0xd1; expected[41] = 0x92; expected[42] = 0xb5; expected[43] = 0xc4;
    expected[44] = 0x2f; expected[45] = 0x73; expected[46] = 0xf2; expected[47] = 0xfd;
    expected[48] = 0x4e; expected[49] = 0x27; expected[50] = 0x36; expected[51] = 0x44;
    expected[52] = 0xc8; expected[53] = 0xb3; expected[54] = 0x61; expected[55] = 0x25;
    expected[56] = 0xa6; expected[57] = 0x4a; expected[58] = 0xdd; expected[59] = 0xeb;
    expected[60] = 0x00; expected[61] = 0x6c; expected[62] = 0x13; expected[63] = 0xa0;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (keystream[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("\nVerificacion TV#3: CORRECTO\n");
    else    uart_puts("\nVerificacion TV#3: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR #4 (RFC 8439 Apendice A.1)
 * Clave casi cero pero con el byte 1 = 0xff, counter=2.
 * Verifica sensibilidad en otro byte de la clave y con counter mayor.
 * Nota little-endian: byte 1 es el segundo byte de state[4] -> 0x0000ff00
 * ========================================================================== */
static void test_tv4_block(void) {
    uint32_t state[16];
    uint8_t  keystream[64];
    uint8_t  expected[64];

    /* Constantes */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    /* Clave: byte 1 = 0xff, resto 0x00
     * state[4] cubre bytes 0-3 de la clave en little-endian */
    state[4]  = 0x0000ff00;  state[5]  = 0x00000000; /* byte 1=0xff queda en bits 15-8 */
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x00000000;
    /* Counter = 2: tercer bloque */
    state[12] = 0x00000002;
    /* Nonce: 12 bytes a 0x00 */
    state[13] = 0x00000000;  state[14] = 0x00000000;
    state[15] = 0x00000000;

    uart_puts("\n=== TEST VECTOR #4 (RFC 8439 Apendice A.1) ===\n");
    uart_puts("Clave  : byte[1]=0xff, resto 0x00\n");
    uart_puts("Nonce  : 12 bytes 0x00\n");
    uart_puts("Counter: 2\n");

    uart_puts("\nEstado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n");
        else uart_puts("  ");
    }

    chacha20_block(state, keystream);

    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(keystream[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado:\n");
    uart_puts("72 d5 4d fb f1 2e c4 4b 36 26 92 df 94 13 7f 32\n");
    uart_puts("8f ea 8d a7 39 90 26 5e c1 bb be a1 ae 9a f0 ca\n");
    uart_puts("13 b2 5a a2 6c b4 a6 48 cb 9b 9d 1b e6 5b 2c 09\n");
    uart_puts("24 a6 6c 54 d5 45 ec 1b 73 74 f4 87 2e 99 f0 96\n");

    expected[0]  = 0x72; expected[1]  = 0xd5; expected[2]  = 0x4d; expected[3]  = 0xfb;
    expected[4]  = 0xf1; expected[5]  = 0x2e; expected[6]  = 0xc4; expected[7]  = 0x4b;
    expected[8]  = 0x36; expected[9]  = 0x26; expected[10] = 0x92; expected[11] = 0xdf;
    expected[12] = 0x94; expected[13] = 0x13; expected[14] = 0x7f; expected[15] = 0x32;
    expected[16] = 0x8f; expected[17] = 0xea; expected[18] = 0x8d; expected[19] = 0xa7;
    expected[20] = 0x39; expected[21] = 0x90; expected[22] = 0x26; expected[23] = 0x5e;
    expected[24] = 0xc1; expected[25] = 0xbb; expected[26] = 0xbe; expected[27] = 0xa1;
    expected[28] = 0xae; expected[29] = 0x9a; expected[30] = 0xf0; expected[31] = 0xca;
    expected[32] = 0x13; expected[33] = 0xb2; expected[34] = 0x5a; expected[35] = 0xa2;
    expected[36] = 0x6c; expected[37] = 0xb4; expected[38] = 0xa6; expected[39] = 0x48;
    expected[40] = 0xcb; expected[41] = 0x9b; expected[42] = 0x9d; expected[43] = 0x1b;
    expected[44] = 0xe6; expected[45] = 0x5b; expected[46] = 0x2c; expected[47] = 0x09;
    expected[48] = 0x24; expected[49] = 0xa6; expected[50] = 0x6c; expected[51] = 0x54;
    expected[52] = 0xd5; expected[53] = 0x45; expected[54] = 0xec; expected[55] = 0x1b;
    expected[56] = 0x73; expected[57] = 0x74; expected[58] = 0xf4; expected[59] = 0x87;
    expected[60] = 0x2e; expected[61] = 0x99; expected[62] = 0xf0; expected[63] = 0x96;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (keystream[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("\nVerificacion TV#4: CORRECTO\n");
    else    uart_puts("\nVerificacion TV#4: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR #5 (RFC 8439 Apendice A.1)
 * Clave cero pero con el ultimo byte del nonce (byte 11) = 0x02.
 * Verifica sensibilidad en el nonce: cambiar un byte del nonce cambia todo.
 * Nota little-endian: byte 11 es el mas significativo de state[15] -> 0x02000000
 * ========================================================================== */
static void test_tv5_block(void) {
    uint32_t state[16];
    uint8_t  keystream[64];
    uint8_t  expected[64];

    /* Constantes */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    /* Clave: 32 bytes todos a 0x00 */
    state[4]  = 0x00000000;  state[5]  = 0x00000000;
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x00000000;
    /* Counter = 0 */
    state[12] = 0x00000000;
    /* Nonce: bytes 0-10 = 0x00, byte 11 = 0x02
     * state[15] cubre bytes 9-11 del nonce en little-endian */
    state[13] = 0x00000000;  state[14] = 0x00000000;
    state[15] = 0x02000000; /* byte 11=0x02 queda en bits 31-24 */

    uart_puts("\n=== TEST VECTOR #5 (RFC 8439 Apendice A.1) ===\n");
    uart_puts("Clave  : 32 bytes 0x00\n");
    uart_puts("Nonce  : 11 bytes 0x00 + 0x02\n");
    uart_puts("Counter: 0\n");

    uart_puts("\nEstado inicial:\n");
    for (int i = 0; i < 16; i++) {
        uart_puthex32(state[i]);
        if ((i + 1) % 4 == 0) uart_puts("\n");
        else uart_puts("  ");
    }

    chacha20_block(state, keystream);

    uart_puts("\nKeystream generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(keystream[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado:\n");
    uart_puts("c2 c6 4d 37 8c d5 36 37 4a e2 04 b9 ef 93 3f cd\n");
    uart_puts("1a 8b 22 88 b3 df a4 96 72 ab 76 5b 54 ee 27 c7\n");
    uart_puts("8a 97 0e 0e 95 5c 14 f3 a8 8e 74 1b 97 c2 86 f7\n");
    uart_puts("5f 8f c2 99 e8 14 83 62 fa 19 8a 39 53 1b ed 6d\n");

    expected[0]  = 0xc2; expected[1]  = 0xc6; expected[2]  = 0x4d; expected[3]  = 0x37;
    expected[4]  = 0x8c; expected[5]  = 0xd5; expected[6]  = 0x36; expected[7]  = 0x37;
    expected[8]  = 0x4a; expected[9]  = 0xe2; expected[10] = 0x04; expected[11] = 0xb9;
    expected[12] = 0xef; expected[13] = 0x93; expected[14] = 0x3f; expected[15] = 0xcd;
    expected[16] = 0x1a; expected[17] = 0x8b; expected[18] = 0x22; expected[19] = 0x88;
    expected[20] = 0xb3; expected[21] = 0xdf; expected[22] = 0xa4; expected[23] = 0x96;
    expected[24] = 0x72; expected[25] = 0xab; expected[26] = 0x76; expected[27] = 0x5b;
    expected[28] = 0x54; expected[29] = 0xee; expected[30] = 0x27; expected[31] = 0xc7;
    expected[32] = 0x8a; expected[33] = 0x97; expected[34] = 0x0e; expected[35] = 0x0e;
    expected[36] = 0x95; expected[37] = 0x5c; expected[38] = 0x14; expected[39] = 0xf3;
    expected[40] = 0xa8; expected[41] = 0x8e; expected[42] = 0x74; expected[43] = 0x1b;
    expected[44] = 0x97; expected[45] = 0xc2; expected[46] = 0x86; expected[47] = 0xf7;
    expected[48] = 0x5f; expected[49] = 0x8f; expected[50] = 0xc2; expected[51] = 0x99;
    expected[52] = 0xe8; expected[53] = 0x14; expected[54] = 0x83; expected[55] = 0x62;
    expected[56] = 0xfa; expected[57] = 0x19; expected[58] = 0x8a; expected[59] = 0x39;
    expected[60] = 0x53; expected[61] = 0x1b; expected[62] = 0xed; expected[63] = 0x6d;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (keystream[i] != expected[i]) { ok = 0; }
    }
    if (ok) uart_puts("\nVerificacion TV#5: CORRECTO\n");
    else    uart_puts("\nVerificacion TV#5: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR A.2 #1 (RFC 8439 Appendix A.2)
 * Encrypt con clave=0, nonce=0, counter=0 y plaintext de 64 bytes en cero.
 * Como el plaintext es todo cero, el ciphertext debe ser exactamente igual
 * al keystream del bloque correspondiente.
 * ========================================================================== */
static void test_a2_tv1_encrypt(void) {
    uint32_t state[16];
    uint8_t plaintext[64];
    uint8_t ciphertext[64];
    uint8_t expected[64];

    /* Estado inicial: constantes + key=0 + counter=0 + nonce=0 */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    state[4]  = 0x00000000;  state[5]  = 0x00000000;
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x00000000;
    state[12] = 0x00000000;
    state[13] = 0x00000000;  state[14] = 0x00000000;  state[15] = 0x00000000;

    /* Plaintext = 64 bytes en cero */
    for (int i = 0; i < 64; i++) {
        plaintext[i] = 0x00;
    }

    uart_puts("\n=== TEST A.2 VECTOR #1 (RFC 8439 Appendix A.2) ===\n");
    uart_puts("Clave  : 32 bytes 0x00\n");
    uart_puts("Nonce  : 12 bytes 0x00\n");
    uart_puts("Counter: 0\n");
    uart_puts("Plaintext: 64 bytes 0x00\n");

    chacha20_encrypt(state, plaintext, ciphertext, 64);

    uart_puts("\nCiphertext generado:\n");
    for (int i = 0; i < 64; i++) {
        uart_puthex8(ciphertext[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }

    uart_puts("\nEsperado:\n");
    uart_puts("76 b8 e0 ad a0 f1 3d 90 40 5d 6a e5 53 86 bd 28\n");
    uart_puts("bd d2 19 b8 a0 8d ed 1a a8 36 ef cc 8b 77 0d c7\n");
    uart_puts("da 41 59 7c 51 57 48 8d 77 24 e0 3f b8 d8 4a 37\n");
    uart_puts("6a 43 b8 f4 15 18 a1 1c c3 87 b6 69 b2 ee 65 86\n");

    expected[0]  = 0x76; expected[1]  = 0xb8; expected[2]  = 0xe0; expected[3]  = 0xad;
    expected[4]  = 0xa0; expected[5]  = 0xf1; expected[6]  = 0x3d; expected[7]  = 0x90;
    expected[8]  = 0x40; expected[9]  = 0x5d; expected[10] = 0x6a; expected[11] = 0xe5;
    expected[12] = 0x53; expected[13] = 0x86; expected[14] = 0xbd; expected[15] = 0x28;
    expected[16] = 0xbd; expected[17] = 0xd2; expected[18] = 0x19; expected[19] = 0xb8;
    expected[20] = 0xa0; expected[21] = 0x8d; expected[22] = 0xed; expected[23] = 0x1a;
    expected[24] = 0xa8; expected[25] = 0x36; expected[26] = 0xef; expected[27] = 0xcc;
    expected[28] = 0x8b; expected[29] = 0x77; expected[30] = 0x0d; expected[31] = 0xc7;
    expected[32] = 0xda; expected[33] = 0x41; expected[34] = 0x59; expected[35] = 0x7c;
    expected[36] = 0x51; expected[37] = 0x57; expected[38] = 0x48; expected[39] = 0x8d;
    expected[40] = 0x77; expected[41] = 0x24; expected[42] = 0xe0; expected[43] = 0x3f;
    expected[44] = 0xb8; expected[45] = 0xd8; expected[46] = 0x4a; expected[47] = 0x37;
    expected[48] = 0x6a; expected[49] = 0x43; expected[50] = 0xb8; expected[51] = 0xf4;
    expected[52] = 0x15; expected[53] = 0x18; expected[54] = 0xa1; expected[55] = 0x1c;
    expected[56] = 0xc3; expected[57] = 0x87; expected[58] = 0xb6; expected[59] = 0x69;
    expected[60] = 0xb2; expected[61] = 0xee; expected[62] = 0x65; expected[63] = 0x86;

    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (ciphertext[i] != expected[i]) { ok = 0; }
    }

    if (ok) uart_puts("\nVerificacion A.2 TV#1: CORRECTO\n");
    else    uart_puts("\nVerificacion A.2 TV#1: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR A.2 #2 (RFC 8439 Appendix A.2)
 * Clave con ultimo byte = 0x01, nonce con ultimo byte = 0x02, counter=1.
 * Cifra 375 bytes de texto ASCII.
 * ========================================================================== */
static void test_a2_tv2_encrypt(void) {
    uint32_t state[16];
    uint8_t plaintext[375];
    uint8_t ciphertext[375];
    uint8_t expected[375];

    /* Estado inicial */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    state[4]  = 0x00000000;  state[5]  = 0x00000000;
    state[6]  = 0x00000000;  state[7]  = 0x00000000;
    state[8]  = 0x00000000;  state[9]  = 0x00000000;
    state[10] = 0x00000000;  state[11] = 0x01000000; /* ultimo byte de key = 0x01 */
    state[12] = 0x00000001;  /* initial counter = 1 */
    state[13] = 0x00000000;  state[14] = 0x00000000;  state[15] = 0x02000000; /* ultimo byte nonce = 0x02 */

    /* Plaintext */
    plaintext[0] = 0x41; plaintext[1] = 0x6e; plaintext[2] = 0x79; plaintext[3] = 0x20;
    plaintext[4] = 0x73; plaintext[5] = 0x75; plaintext[6] = 0x62; plaintext[7] = 0x6d;
    plaintext[8] = 0x69; plaintext[9] = 0x73; plaintext[10] = 0x73; plaintext[11] = 0x69;
    plaintext[12] = 0x6f; plaintext[13] = 0x6e; plaintext[14] = 0x20; plaintext[15] = 0x74;
    plaintext[16] = 0x6f; plaintext[17] = 0x20; plaintext[18] = 0x74; plaintext[19] = 0x68;
    plaintext[20] = 0x65; plaintext[21] = 0x20; plaintext[22] = 0x49; plaintext[23] = 0x45;
    plaintext[24] = 0x54; plaintext[25] = 0x46; plaintext[26] = 0x20; plaintext[27] = 0x69;
    plaintext[28] = 0x6e; plaintext[29] = 0x74; plaintext[30] = 0x65; plaintext[31] = 0x6e;
    plaintext[32] = 0x64; plaintext[33] = 0x65; plaintext[34] = 0x64; plaintext[35] = 0x20;
    plaintext[36] = 0x62; plaintext[37] = 0x79; plaintext[38] = 0x20; plaintext[39] = 0x74;
    plaintext[40] = 0x68; plaintext[41] = 0x65; plaintext[42] = 0x20; plaintext[43] = 0x43;
    plaintext[44] = 0x6f; plaintext[45] = 0x6e; plaintext[46] = 0x74; plaintext[47] = 0x72;
    plaintext[48] = 0x69; plaintext[49] = 0x62; plaintext[50] = 0x75; plaintext[51] = 0x74;
    plaintext[52] = 0x6f; plaintext[53] = 0x72; plaintext[54] = 0x20; plaintext[55] = 0x66;
    plaintext[56] = 0x6f; plaintext[57] = 0x72; plaintext[58] = 0x20; plaintext[59] = 0x70;
    plaintext[60] = 0x75; plaintext[61] = 0x62; plaintext[62] = 0x6c; plaintext[63] = 0x69;
    plaintext[64] = 0x63; plaintext[65] = 0x61; plaintext[66] = 0x74; plaintext[67] = 0x69;
    plaintext[68] = 0x6f; plaintext[69] = 0x6e; plaintext[70] = 0x20; plaintext[71] = 0x61;
    plaintext[72] = 0x73; plaintext[73] = 0x20; plaintext[74] = 0x61; plaintext[75] = 0x6c;
    plaintext[76] = 0x6c; plaintext[77] = 0x20; plaintext[78] = 0x6f; plaintext[79] = 0x72;
    plaintext[80] = 0x20; plaintext[81] = 0x70; plaintext[82] = 0x61; plaintext[83] = 0x72;
    plaintext[84] = 0x74; plaintext[85] = 0x20; plaintext[86] = 0x6f; plaintext[87] = 0x66;
    plaintext[88] = 0x20; plaintext[89] = 0x61; plaintext[90] = 0x6e; plaintext[91] = 0x20;
    plaintext[92] = 0x49; plaintext[93] = 0x45; plaintext[94] = 0x54; plaintext[95] = 0x46;
    plaintext[96] = 0x20; plaintext[97] = 0x49; plaintext[98] = 0x6e; plaintext[99] = 0x74;
    plaintext[100] = 0x65; plaintext[101] = 0x72; plaintext[102] = 0x6e; plaintext[103] = 0x65;
    plaintext[104] = 0x74; plaintext[105] = 0x2d; plaintext[106] = 0x44; plaintext[107] = 0x72;
    plaintext[108] = 0x61; plaintext[109] = 0x66; plaintext[110] = 0x74; plaintext[111] = 0x20;
    plaintext[112] = 0x6f; plaintext[113] = 0x72; plaintext[114] = 0x20; plaintext[115] = 0x52;
    plaintext[116] = 0x46; plaintext[117] = 0x43; plaintext[118] = 0x20; plaintext[119] = 0x61;
    plaintext[120] = 0x6e; plaintext[121] = 0x64; plaintext[122] = 0x20; plaintext[123] = 0x61;
    plaintext[124] = 0x6e; plaintext[125] = 0x79; plaintext[126] = 0x20; plaintext[127] = 0x73;
    plaintext[128] = 0x74; plaintext[129] = 0x61; plaintext[130] = 0x74; plaintext[131] = 0x65;
    plaintext[132] = 0x6d; plaintext[133] = 0x65; plaintext[134] = 0x6e; plaintext[135] = 0x74;
    plaintext[136] = 0x20; plaintext[137] = 0x6d; plaintext[138] = 0x61; plaintext[139] = 0x64;
    plaintext[140] = 0x65; plaintext[141] = 0x20; plaintext[142] = 0x77; plaintext[143] = 0x69;
    plaintext[144] = 0x74; plaintext[145] = 0x68; plaintext[146] = 0x69; plaintext[147] = 0x6e;
    plaintext[148] = 0x20; plaintext[149] = 0x74; plaintext[150] = 0x68; plaintext[151] = 0x65;
    plaintext[152] = 0x20; plaintext[153] = 0x63; plaintext[154] = 0x6f; plaintext[155] = 0x6e;
    plaintext[156] = 0x74; plaintext[157] = 0x65; plaintext[158] = 0x78; plaintext[159] = 0x74;
    plaintext[160] = 0x20; plaintext[161] = 0x6f; plaintext[162] = 0x66; plaintext[163] = 0x20;
    plaintext[164] = 0x61; plaintext[165] = 0x6e; plaintext[166] = 0x20; plaintext[167] = 0x49;
    plaintext[168] = 0x45; plaintext[169] = 0x54; plaintext[170] = 0x46; plaintext[171] = 0x20;
    plaintext[172] = 0x61; plaintext[173] = 0x63; plaintext[174] = 0x74; plaintext[175] = 0x69;
    plaintext[176] = 0x76; plaintext[177] = 0x69; plaintext[178] = 0x74; plaintext[179] = 0x79;
    plaintext[180] = 0x20; plaintext[181] = 0x69; plaintext[182] = 0x73; plaintext[183] = 0x20;
    plaintext[184] = 0x63; plaintext[185] = 0x6f; plaintext[186] = 0x6e; plaintext[187] = 0x73;
    plaintext[188] = 0x69; plaintext[189] = 0x64; plaintext[190] = 0x65; plaintext[191] = 0x72;
    plaintext[192] = 0x65; plaintext[193] = 0x64; plaintext[194] = 0x20; plaintext[195] = 0x61;
    plaintext[196] = 0x6e; plaintext[197] = 0x20; plaintext[198] = 0x22; plaintext[199] = 0x49;
    plaintext[200] = 0x45; plaintext[201] = 0x54; plaintext[202] = 0x46; plaintext[203] = 0x20;
    plaintext[204] = 0x43; plaintext[205] = 0x6f; plaintext[206] = 0x6e; plaintext[207] = 0x74;
    plaintext[208] = 0x72; plaintext[209] = 0x69; plaintext[210] = 0x62; plaintext[211] = 0x75;
    plaintext[212] = 0x74; plaintext[213] = 0x69; plaintext[214] = 0x6f; plaintext[215] = 0x6e;
    plaintext[216] = 0x22; plaintext[217] = 0x2e; plaintext[218] = 0x20; plaintext[219] = 0x53;
    plaintext[220] = 0x75; plaintext[221] = 0x63; plaintext[222] = 0x68; plaintext[223] = 0x20;
    plaintext[224] = 0x73; plaintext[225] = 0x74; plaintext[226] = 0x61; plaintext[227] = 0x74;
    plaintext[228] = 0x65; plaintext[229] = 0x6d; plaintext[230] = 0x65; plaintext[231] = 0x6e;
    plaintext[232] = 0x74; plaintext[233] = 0x73; plaintext[234] = 0x20; plaintext[235] = 0x69;
    plaintext[236] = 0x6e; plaintext[237] = 0x63; plaintext[238] = 0x6c; plaintext[239] = 0x75;
    plaintext[240] = 0x64; plaintext[241] = 0x65; plaintext[242] = 0x20; plaintext[243] = 0x6f;
    plaintext[244] = 0x72; plaintext[245] = 0x61; plaintext[246] = 0x6c; plaintext[247] = 0x20;
    plaintext[248] = 0x73; plaintext[249] = 0x74; plaintext[250] = 0x61; plaintext[251] = 0x74;
    plaintext[252] = 0x65; plaintext[253] = 0x6d; plaintext[254] = 0x65; plaintext[255] = 0x6e;
    plaintext[256] = 0x74; plaintext[257] = 0x73; plaintext[258] = 0x20; plaintext[259] = 0x69;
    plaintext[260] = 0x6e; plaintext[261] = 0x20; plaintext[262] = 0x49; plaintext[263] = 0x45;
    plaintext[264] = 0x54; plaintext[265] = 0x46; plaintext[266] = 0x20; plaintext[267] = 0x73;
    plaintext[268] = 0x65; plaintext[269] = 0x73; plaintext[270] = 0x73; plaintext[271] = 0x69;
    plaintext[272] = 0x6f; plaintext[273] = 0x6e; plaintext[274] = 0x73; plaintext[275] = 0x2c;
    plaintext[276] = 0x20; plaintext[277] = 0x61; plaintext[278] = 0x73; plaintext[279] = 0x20;
    plaintext[280] = 0x77; plaintext[281] = 0x65; plaintext[282] = 0x6c; plaintext[283] = 0x6c;
    plaintext[284] = 0x20; plaintext[285] = 0x61; plaintext[286] = 0x73; plaintext[287] = 0x20;
    plaintext[288] = 0x77; plaintext[289] = 0x72; plaintext[290] = 0x69; plaintext[291] = 0x74;
    plaintext[292] = 0x74; plaintext[293] = 0x65; plaintext[294] = 0x6e; plaintext[295] = 0x20;
    plaintext[296] = 0x61; plaintext[297] = 0x6e; plaintext[298] = 0x64; plaintext[299] = 0x20;
    plaintext[300] = 0x65; plaintext[301] = 0x6c; plaintext[302] = 0x65; plaintext[303] = 0x63;
    plaintext[304] = 0x74; plaintext[305] = 0x72; plaintext[306] = 0x6f; plaintext[307] = 0x6e;
    plaintext[308] = 0x69; plaintext[309] = 0x63; plaintext[310] = 0x20; plaintext[311] = 0x63;
    plaintext[312] = 0x6f; plaintext[313] = 0x6d; plaintext[314] = 0x6d; plaintext[315] = 0x75;
    plaintext[316] = 0x6e; plaintext[317] = 0x69; plaintext[318] = 0x63; plaintext[319] = 0x61;
    plaintext[320] = 0x74; plaintext[321] = 0x69; plaintext[322] = 0x6f; plaintext[323] = 0x6e;
    plaintext[324] = 0x73; plaintext[325] = 0x20; plaintext[326] = 0x6d; plaintext[327] = 0x61;
    plaintext[328] = 0x64; plaintext[329] = 0x65; plaintext[330] = 0x20; plaintext[331] = 0x61;
    plaintext[332] = 0x74; plaintext[333] = 0x20; plaintext[334] = 0x61; plaintext[335] = 0x6e;
    plaintext[336] = 0x79; plaintext[337] = 0x20; plaintext[338] = 0x74; plaintext[339] = 0x69;
    plaintext[340] = 0x6d; plaintext[341] = 0x65; plaintext[342] = 0x20; plaintext[343] = 0x6f;
    plaintext[344] = 0x72; plaintext[345] = 0x20; plaintext[346] = 0x70; plaintext[347] = 0x6c;
    plaintext[348] = 0x61; plaintext[349] = 0x63; plaintext[350] = 0x65; plaintext[351] = 0x2c;
    plaintext[352] = 0x20; plaintext[353] = 0x77; plaintext[354] = 0x68; plaintext[355] = 0x69;
    plaintext[356] = 0x63; plaintext[357] = 0x68; plaintext[358] = 0x20; plaintext[359] = 0x61;
    plaintext[360] = 0x72; plaintext[361] = 0x65; plaintext[362] = 0x20; plaintext[363] = 0x61;
    plaintext[364] = 0x64; plaintext[365] = 0x64; plaintext[366] = 0x72; plaintext[367] = 0x65;
    plaintext[368] = 0x73; plaintext[369] = 0x73; plaintext[370] = 0x65; plaintext[371] = 0x64;
    plaintext[372] = 0x20; plaintext[373] = 0x74; plaintext[374] = 0x6f;

    uart_puts("\n=== TEST A.2 VECTOR #2 (RFC 8439 Appendix A.2) ===\n");
    uart_puts("Clave  : ultimo byte = 0x01\n");
    uart_puts("Nonce  : ultimo byte = 0x02\n");
    uart_puts("Counter: 1\n");

    chacha20_encrypt(state, plaintext, ciphertext, 375);

    uart_puts("\nCiphertext generado:\n");
    for (int i = 0; i < 375; i++) {
        uart_puthex8(ciphertext[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }
    uart_puts("\n");

    uart_puts("Esperado:\n");
    uart_puts("a3 fb f0 7d f3 fa 2f de 4f 37 6c a2 3e 82 73 70\n");
    uart_puts("41 60 5d 9f 4f 4f 57 bd 8c ff 2c 1d 4b 79 55 ec\n");
    uart_puts("2a 97 94 8b d3 72 29 15 c8 f3 d3 37 f7 d3 70 05\n");
    uart_puts("0e 9e 96 d6 47 b7 c3 9f 56 e0 31 ca 5e b6 25 0d\n");
    uart_puts("40 42 e0 27 85 ec ec fa 4b 4b b5 e8 ea d0 44 0e\n");
    uart_puts("20 b6 e8 db 09 d8 81 a7 c6 13 2f 42 0e 52 79 50\n");
    uart_puts("42 bd fa 77 73 d8 a9 05 14 47 b3 29 1c e1 41 1c\n");
    uart_puts("68 04 65 55 2a a6 c4 05 b7 76 4d 5e 87 be a8 5a\n");
    uart_puts("d0 0f 84 49 ed 8f 72 d0 d6 62 ab 05 26 91 ca 66\n");
    uart_puts("42 4b c8 6d 2d f8 0e a4 1f 43 ab f9 37 d3 25 9d\n");
    uart_puts("c4 b2 d0 df b4 8a 6c 91 39 dd d7 f7 69 66 e9 28\n");
    uart_puts("e6 35 55 3b a7 6c 5c 87 9d 7b 35 d4 9e b2 e6 2b\n");
    uart_puts("08 71 cd ac 63 89 39 e2 5e 8a 1e 0e f9 d5 28 0f\n");
    uart_puts("a8 ca 32 8b 35 1c 3c 76 59 89 cb cf 3d aa 8b 6c\n");
    uart_puts("cc 3a af 9f 39 79 c9 2b 37 20 fc 88 dc 95 ed 84\n");
    uart_puts("a1 be 05 9c 64 99 b9 fd a2 36 e7 e8 18 b0 4b 0b\n");
    uart_puts("c3 9c 1e 87 6b 19 3b fe 55 69 75 3f 88 12 8c c0\n");
    uart_puts("8a aa 9b 63 d1 a1 6f 80 ef 25 54 d7 18 9c 41 1f\n");
    uart_puts("58 69 ca 52 c5 b8 3f a3 6f f2 16 b9 c1 d3 00 62\n");
    uart_puts("be bc fd 2d c5 bc e0 91 19 34 fd a7 9a 86 f6 e6\n");
    uart_puts("98 ce d7 59 c3 ff 9b 64 77 33 8f 3d a4 f9 cd 85\n");
    uart_puts("14 ea 99 82 cc af b3 41 b2 38 4d d9 02 f3 d1 ab\n");
    uart_puts("7a c6 1d d2 9c 6f 21 ba 5b 86 2f 37 30 e3 7c fd\n");
    uart_puts("c4 fd 80 6c 22 f2 21\n");

    expected[0] = 0xa3; expected[1] = 0xfb; expected[2] = 0xf0; expected[3] = 0x7d;
    expected[4] = 0xf3; expected[5] = 0xfa; expected[6] = 0x2f; expected[7] = 0xde;
    expected[8] = 0x4f; expected[9] = 0x37; expected[10] = 0x6c; expected[11] = 0xa2;
    expected[12] = 0x3e; expected[13] = 0x82; expected[14] = 0x73; expected[15] = 0x70;
    expected[16] = 0x41; expected[17] = 0x60; expected[18] = 0x5d; expected[19] = 0x9f;
    expected[20] = 0x4f; expected[21] = 0x4f; expected[22] = 0x57; expected[23] = 0xbd;
    expected[24] = 0x8c; expected[25] = 0xff; expected[26] = 0x2c; expected[27] = 0x1d;
    expected[28] = 0x4b; expected[29] = 0x79; expected[30] = 0x55; expected[31] = 0xec;
    expected[32] = 0x2a; expected[33] = 0x97; expected[34] = 0x94; expected[35] = 0x8b;
    expected[36] = 0xd3; expected[37] = 0x72; expected[38] = 0x29; expected[39] = 0x15;
    expected[40] = 0xc8; expected[41] = 0xf3; expected[42] = 0xd3; expected[43] = 0x37;
    expected[44] = 0xf7; expected[45] = 0xd3; expected[46] = 0x70; expected[47] = 0x05;
    expected[48] = 0x0e; expected[49] = 0x9e; expected[50] = 0x96; expected[51] = 0xd6;
    expected[52] = 0x47; expected[53] = 0xb7; expected[54] = 0xc3; expected[55] = 0x9f;
    expected[56] = 0x56; expected[57] = 0xe0; expected[58] = 0x31; expected[59] = 0xca;
    expected[60] = 0x5e; expected[61] = 0xb6; expected[62] = 0x25; expected[63] = 0x0d;
    expected[64] = 0x40; expected[65] = 0x42; expected[66] = 0xe0; expected[67] = 0x27;
    expected[68] = 0x85; expected[69] = 0xec; expected[70] = 0xec; expected[71] = 0xfa;
    expected[72] = 0x4b; expected[73] = 0x4b; expected[74] = 0xb5; expected[75] = 0xe8;
    expected[76] = 0xea; expected[77] = 0xd0; expected[78] = 0x44; expected[79] = 0x0e;
    expected[80] = 0x20; expected[81] = 0xb6; expected[82] = 0xe8; expected[83] = 0xdb;
    expected[84] = 0x09; expected[85] = 0xd8; expected[86] = 0x81; expected[87] = 0xa7;
    expected[88] = 0xc6; expected[89] = 0x13; expected[90] = 0x2f; expected[91] = 0x42;
    expected[92] = 0x0e; expected[93] = 0x52; expected[94] = 0x79; expected[95] = 0x50;
    expected[96] = 0x42; expected[97] = 0xbd; expected[98] = 0xfa; expected[99] = 0x77;
    expected[100] = 0x73; expected[101] = 0xd8; expected[102] = 0xa9; expected[103] = 0x05;
    expected[104] = 0x14; expected[105] = 0x47; expected[106] = 0xb3; expected[107] = 0x29;
    expected[108] = 0x1c; expected[109] = 0xe1; expected[110] = 0x41; expected[111] = 0x1c;
    expected[112] = 0x68; expected[113] = 0x04; expected[114] = 0x65; expected[115] = 0x55;
    expected[116] = 0x2a; expected[117] = 0xa6; expected[118] = 0xc4; expected[119] = 0x05;
    expected[120] = 0xb7; expected[121] = 0x76; expected[122] = 0x4d; expected[123] = 0x5e;
    expected[124] = 0x87; expected[125] = 0xbe; expected[126] = 0xa8; expected[127] = 0x5a;
    expected[128] = 0xd0; expected[129] = 0x0f; expected[130] = 0x84; expected[131] = 0x49;
    expected[132] = 0xed; expected[133] = 0x8f; expected[134] = 0x72; expected[135] = 0xd0;
    expected[136] = 0xd6; expected[137] = 0x62; expected[138] = 0xab; expected[139] = 0x05;
    expected[140] = 0x26; expected[141] = 0x91; expected[142] = 0xca; expected[143] = 0x66;
    expected[144] = 0x42; expected[145] = 0x4b; expected[146] = 0xc8; expected[147] = 0x6d;
    expected[148] = 0x2d; expected[149] = 0xf8; expected[150] = 0x0e; expected[151] = 0xa4;
    expected[152] = 0x1f; expected[153] = 0x43; expected[154] = 0xab; expected[155] = 0xf9;
    expected[156] = 0x37; expected[157] = 0xd3; expected[158] = 0x25; expected[159] = 0x9d;
    expected[160] = 0xc4; expected[161] = 0xb2; expected[162] = 0xd0; expected[163] = 0xdf;
    expected[164] = 0xb4; expected[165] = 0x8a; expected[166] = 0x6c; expected[167] = 0x91;
    expected[168] = 0x39; expected[169] = 0xdd; expected[170] = 0xd7; expected[171] = 0xf7;
    expected[172] = 0x69; expected[173] = 0x66; expected[174] = 0xe9; expected[175] = 0x28;
    expected[176] = 0xe6; expected[177] = 0x35; expected[178] = 0x55; expected[179] = 0x3b;
    expected[180] = 0xa7; expected[181] = 0x6c; expected[182] = 0x5c; expected[183] = 0x87;
    expected[184] = 0x9d; expected[185] = 0x7b; expected[186] = 0x35; expected[187] = 0xd4;
    expected[188] = 0x9e; expected[189] = 0xb2; expected[190] = 0xe6; expected[191] = 0x2b;
    expected[192] = 0x08; expected[193] = 0x71; expected[194] = 0xcd; expected[195] = 0xac;
    expected[196] = 0x63; expected[197] = 0x89; expected[198] = 0x39; expected[199] = 0xe2;
    expected[200] = 0x5e; expected[201] = 0x8a; expected[202] = 0x1e; expected[203] = 0x0e;
    expected[204] = 0xf9; expected[205] = 0xd5; expected[206] = 0x28; expected[207] = 0x0f;
    expected[208] = 0xa8; expected[209] = 0xca; expected[210] = 0x32; expected[211] = 0x8b;
    expected[212] = 0x35; expected[213] = 0x1c; expected[214] = 0x3c; expected[215] = 0x76;
    expected[216] = 0x59; expected[217] = 0x89; expected[218] = 0xcb; expected[219] = 0xcf;
    expected[220] = 0x3d; expected[221] = 0xaa; expected[222] = 0x8b; expected[223] = 0x6c;
    expected[224] = 0xcc; expected[225] = 0x3a; expected[226] = 0xaf; expected[227] = 0x9f;
    expected[228] = 0x39; expected[229] = 0x79; expected[230] = 0xc9; expected[231] = 0x2b;
    expected[232] = 0x37; expected[233] = 0x20; expected[234] = 0xfc; expected[235] = 0x88;
    expected[236] = 0xdc; expected[237] = 0x95; expected[238] = 0xed; expected[239] = 0x84;
    expected[240] = 0xa1; expected[241] = 0xbe; expected[242] = 0x05; expected[243] = 0x9c;
    expected[244] = 0x64; expected[245] = 0x99; expected[246] = 0xb9; expected[247] = 0xfd;
    expected[248] = 0xa2; expected[249] = 0x36; expected[250] = 0xe7; expected[251] = 0xe8;
    expected[252] = 0x18; expected[253] = 0xb0; expected[254] = 0x4b; expected[255] = 0x0b;
    expected[256] = 0xc3; expected[257] = 0x9c; expected[258] = 0x1e; expected[259] = 0x87;
    expected[260] = 0x6b; expected[261] = 0x19; expected[262] = 0x3b; expected[263] = 0xfe;
    expected[264] = 0x55; expected[265] = 0x69; expected[266] = 0x75; expected[267] = 0x3f;
    expected[268] = 0x88; expected[269] = 0x12; expected[270] = 0x8c; expected[271] = 0xc0;
    expected[272] = 0x8a; expected[273] = 0xaa; expected[274] = 0x9b; expected[275] = 0x63;
    expected[276] = 0xd1; expected[277] = 0xa1; expected[278] = 0x6f; expected[279] = 0x80;
    expected[280] = 0xef; expected[281] = 0x25; expected[282] = 0x54; expected[283] = 0xd7;
    expected[284] = 0x18; expected[285] = 0x9c; expected[286] = 0x41; expected[287] = 0x1f;
    expected[288] = 0x58; expected[289] = 0x69; expected[290] = 0xca; expected[291] = 0x52;
    expected[292] = 0xc5; expected[293] = 0xb8; expected[294] = 0x3f; expected[295] = 0xa3;
    expected[296] = 0x6f; expected[297] = 0xf2; expected[298] = 0x16; expected[299] = 0xb9;
    expected[300] = 0xc1; expected[301] = 0xd3; expected[302] = 0x00; expected[303] = 0x62;
    expected[304] = 0xbe; expected[305] = 0xbc; expected[306] = 0xfd; expected[307] = 0x2d;
    expected[308] = 0xc5; expected[309] = 0xbc; expected[310] = 0xe0; expected[311] = 0x91;
    expected[312] = 0x19; expected[313] = 0x34; expected[314] = 0xfd; expected[315] = 0xa7;
    expected[316] = 0x9a; expected[317] = 0x86; expected[318] = 0xf6; expected[319] = 0xe6;
    expected[320] = 0x98; expected[321] = 0xce; expected[322] = 0xd7; expected[323] = 0x59;
    expected[324] = 0xc3; expected[325] = 0xff; expected[326] = 0x9b; expected[327] = 0x64;
    expected[328] = 0x77; expected[329] = 0x33; expected[330] = 0x8f; expected[331] = 0x3d;
    expected[332] = 0xa4; expected[333] = 0xf9; expected[334] = 0xcd; expected[335] = 0x85;
    expected[336] = 0x14; expected[337] = 0xea; expected[338] = 0x99; expected[339] = 0x82;
    expected[340] = 0xcc; expected[341] = 0xaf; expected[342] = 0xb3; expected[343] = 0x41;
    expected[344] = 0xb2; expected[345] = 0x38; expected[346] = 0x4d; expected[347] = 0xd9;
    expected[348] = 0x02; expected[349] = 0xf3; expected[350] = 0xd1; expected[351] = 0xab;
    expected[352] = 0x7a; expected[353] = 0xc6; expected[354] = 0x1d; expected[355] = 0xd2;
    expected[356] = 0x9c; expected[357] = 0x6f; expected[358] = 0x21; expected[359] = 0xba;
    expected[360] = 0x5b; expected[361] = 0x86; expected[362] = 0x2f; expected[363] = 0x37;
    expected[364] = 0x30; expected[365] = 0xe3; expected[366] = 0x7c; expected[367] = 0xfd;
    expected[368] = 0xc4; expected[369] = 0xfd; expected[370] = 0x80; expected[371] = 0x6c;
    expected[372] = 0x22; expected[373] = 0xf2; expected[374] = 0x21;

    int ok = 1;
    for (int i = 0; i < 375; i++) {
        if (ciphertext[i] != expected[i]) { ok = 0; }
    }

    if (ok) uart_puts("Verificacion A.2 TV#2: CORRECTO\n");
    else    uart_puts("Verificacion A.2 TV#2: ERROR\n");
}

/* ==========================================================================
 * TEST VECTOR A.2 #3 (RFC 8439 Appendix A.2)
 * Key especifica, nonce con ultimo byte = 0x02, counter=42.
 * Cifra 127 bytes.
 * ========================================================================== */
static void test_a2_tv3_encrypt(void) {
    uint32_t state[16];
    uint8_t plaintext[127];
    uint8_t ciphertext[127];
    uint8_t expected[127];

    /* Estado inicial */
    state[0]  = 0x61707865;  state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;  state[3]  = 0x6b206574;
    state[4]  = 0xa540921c;
    state[5]  = 0x8ad355eb;
    state[6]  = 0x868833f3;
    state[7]  = 0xf0b5f604;
    state[8]  = 0xc1173947;
    state[9]  = 0x09802b40;
    state[10] = 0xbc5cca9d;
    state[11] = 0xc0757020;
    state[12] = 42;
    state[13] = 0x00000000;
    state[14] = 0x00000000;
    state[15] = 0x02000000;

    /* Plaintext */
    plaintext[0] = 0x27; plaintext[1] = 0x54; plaintext[2] = 0x77; plaintext[3] = 0x61;
    plaintext[4] = 0x73; plaintext[5] = 0x20; plaintext[6] = 0x62; plaintext[7] = 0x72;
    plaintext[8] = 0x69; plaintext[9] = 0x6c; plaintext[10] = 0x6c; plaintext[11] = 0x69;
    plaintext[12] = 0x67; plaintext[13] = 0x2c; plaintext[14] = 0x20; plaintext[15] = 0x61;
    plaintext[16] = 0x6e; plaintext[17] = 0x64; plaintext[18] = 0x20; plaintext[19] = 0x74;
    plaintext[20] = 0x68; plaintext[21] = 0x65; plaintext[22] = 0x20; plaintext[23] = 0x73;
    plaintext[24] = 0x6c; plaintext[25] = 0x69; plaintext[26] = 0x74; plaintext[27] = 0x68;
    plaintext[28] = 0x79; plaintext[29] = 0x20; plaintext[30] = 0x74; plaintext[31] = 0x6f;
    plaintext[32] = 0x76; plaintext[33] = 0x65; plaintext[34] = 0x73; plaintext[35] = 0x0a;
    plaintext[36] = 0x44; plaintext[37] = 0x69; plaintext[38] = 0x64; plaintext[39] = 0x20;
    plaintext[40] = 0x67; plaintext[41] = 0x79; plaintext[42] = 0x72; plaintext[43] = 0x65;
    plaintext[44] = 0x20; plaintext[45] = 0x61; plaintext[46] = 0x6e; plaintext[47] = 0x64;
    plaintext[48] = 0x20; plaintext[49] = 0x67; plaintext[50] = 0x69; plaintext[51] = 0x6d;
    plaintext[52] = 0x62; plaintext[53] = 0x6c; plaintext[54] = 0x65; plaintext[55] = 0x20;
    plaintext[56] = 0x69; plaintext[57] = 0x6e; plaintext[58] = 0x20; plaintext[59] = 0x74;
    plaintext[60] = 0x68; plaintext[61] = 0x65; plaintext[62] = 0x20; plaintext[63] = 0x77;
    plaintext[64] = 0x61; plaintext[65] = 0x62; plaintext[66] = 0x65; plaintext[67] = 0x3a;
    plaintext[68] = 0x0a; plaintext[69] = 0x41; plaintext[70] = 0x6c; plaintext[71] = 0x6c;
    plaintext[72] = 0x20; plaintext[73] = 0x6d; plaintext[74] = 0x69; plaintext[75] = 0x6d;
    plaintext[76] = 0x73; plaintext[77] = 0x79; plaintext[78] = 0x20; plaintext[79] = 0x77;
    plaintext[80] = 0x65; plaintext[81] = 0x72; plaintext[82] = 0x65; plaintext[83] = 0x20;
    plaintext[84] = 0x74; plaintext[85] = 0x68; plaintext[86] = 0x65; plaintext[87] = 0x20;
    plaintext[88] = 0x62; plaintext[89] = 0x6f; plaintext[90] = 0x72; plaintext[91] = 0x6f;
    plaintext[92] = 0x67; plaintext[93] = 0x6f; plaintext[94] = 0x76; plaintext[95] = 0x65;
    plaintext[96] = 0x73; plaintext[97] = 0x2c; plaintext[98] = 0x0a; plaintext[99] = 0x41;
    plaintext[100] = 0x6e; plaintext[101] = 0x64; plaintext[102] = 0x20; plaintext[103] = 0x74;
    plaintext[104] = 0x68; plaintext[105] = 0x65; plaintext[106] = 0x20; plaintext[107] = 0x6d;
    plaintext[108] = 0x6f; plaintext[109] = 0x6d; plaintext[110] = 0x65; plaintext[111] = 0x20;
    plaintext[112] = 0x72; plaintext[113] = 0x61; plaintext[114] = 0x74; plaintext[115] = 0x68;
    plaintext[116] = 0x73; plaintext[117] = 0x20; plaintext[118] = 0x6f; plaintext[119] = 0x75;
    plaintext[120] = 0x74; plaintext[121] = 0x67; plaintext[122] = 0x72; plaintext[123] = 0x61;
    plaintext[124] = 0x62; plaintext[125] = 0x65; plaintext[126] = 0x2e;

    uart_puts("\n=== TEST A.2 VECTOR #3 (RFC 8439 Appendix A.2) ===\n");
    uart_puts("Clave  : 256 bits especifica RFC\n");
    uart_puts("Nonce  : ultimo byte = 0x02\n");
    uart_puts("Counter: 42\n");

    chacha20_encrypt(state, plaintext, ciphertext, 127);

    uart_puts("\nCiphertext generado:\n");
    for (int i = 0; i < 127; i++) {
        uart_puthex8(ciphertext[i]);
        uart_putc(' ');
        if ((i + 1) % 16 == 0) uart_puts("\n");
    }
    uart_puts("\n");

    uart_puts("Esperado:\n");
    uart_puts("62 e6 34 7f 95 ed 87 a4 5f fa e7 42 6f 27 a1 df\n");
    uart_puts("5f b6 91 10 04 4c 0d 73 11 8e ff a9 5b 01 e5 cf\n");
    uart_puts("16 6d 3d f2 d7 21 ca f9 b2 1e 5f b1 4c 61 68 71\n");
    uart_puts("fd 84 c5 4f 9d 65 b2 83 19 6c 7f e4 f6 05 53 eb\n");
    uart_puts("f3 9c 64 02 c4 22 34 e3 2a 35 6b 3e 76 43 12 a6\n");
    uart_puts("1a 55 32 05 57 16 ea d6 96 25 68 f8 7d 3f 3f 77\n");
    uart_puts("04 c6 a8 d1 bc d1 bf 4d 50 d6 15 4b 6d a7 31 b1\n");
    uart_puts("87 b5 8d fd 72 8a fa 36 75 7a 79 7a c1 88 d1\n");

    expected[0] = 0x62; expected[1] = 0xe6; expected[2] = 0x34; expected[3] = 0x7f;
    expected[4] = 0x95; expected[5] = 0xed; expected[6] = 0x87; expected[7] = 0xa4;
    expected[8] = 0x5f; expected[9] = 0xfa; expected[10] = 0xe7; expected[11] = 0x42;
    expected[12] = 0x6f; expected[13] = 0x27; expected[14] = 0xa1; expected[15] = 0xdf;
    expected[16] = 0x5f; expected[17] = 0xb6; expected[18] = 0x91; expected[19] = 0x10;
    expected[20] = 0x04; expected[21] = 0x4c; expected[22] = 0x0d; expected[23] = 0x73;
    expected[24] = 0x11; expected[25] = 0x8e; expected[26] = 0xff; expected[27] = 0xa9;
    expected[28] = 0x5b; expected[29] = 0x01; expected[30] = 0xe5; expected[31] = 0xcf;
    expected[32] = 0x16; expected[33] = 0x6d; expected[34] = 0x3d; expected[35] = 0xf2;
    expected[36] = 0xd7; expected[37] = 0x21; expected[38] = 0xca; expected[39] = 0xf9;
    expected[40] = 0xb2; expected[41] = 0x1e; expected[42] = 0x5f; expected[43] = 0xb1;
    expected[44] = 0x4c; expected[45] = 0x61; expected[46] = 0x68; expected[47] = 0x71;
    expected[48] = 0xfd; expected[49] = 0x84; expected[50] = 0xc5; expected[51] = 0x4f;
    expected[52] = 0x9d; expected[53] = 0x65; expected[54] = 0xb2; expected[55] = 0x83;
    expected[56] = 0x19; expected[57] = 0x6c; expected[58] = 0x7f; expected[59] = 0xe4;
    expected[60] = 0xf6; expected[61] = 0x05; expected[62] = 0x53; expected[63] = 0xeb;
    expected[64] = 0xf3; expected[65] = 0x9c; expected[66] = 0x64; expected[67] = 0x02;
    expected[68] = 0xc4; expected[69] = 0x22; expected[70] = 0x34; expected[71] = 0xe3;
    expected[72] = 0x2a; expected[73] = 0x35; expected[74] = 0x6b; expected[75] = 0x3e;
    expected[76] = 0x76; expected[77] = 0x43; expected[78] = 0x12; expected[79] = 0xa6;
    expected[80] = 0x1a; expected[81] = 0x55; expected[82] = 0x32; expected[83] = 0x05;
    expected[84] = 0x57; expected[85] = 0x16; expected[86] = 0xea; expected[87] = 0xd6;
    expected[88] = 0x96; expected[89] = 0x25; expected[90] = 0x68; expected[91] = 0xf8;
    expected[92] = 0x7d; expected[93] = 0x3f; expected[94] = 0x3f; expected[95] = 0x77;
    expected[96] = 0x04; expected[97] = 0xc6; expected[98] = 0xa8; expected[99] = 0xd1;
    expected[100] = 0xbc; expected[101] = 0xd1; expected[102] = 0xbf; expected[103] = 0x4d;
    expected[104] = 0x50; expected[105] = 0xd6; expected[106] = 0x15; expected[107] = 0x4b;
    expected[108] = 0x6d; expected[109] = 0xa7; expected[110] = 0x31; expected[111] = 0xb1;
    expected[112] = 0x87; expected[113] = 0xb5; expected[114] = 0x8d; expected[115] = 0xfd;
    expected[116] = 0x72; expected[117] = 0x8a; expected[118] = 0xfa; expected[119] = 0x36;
    expected[120] = 0x75; expected[121] = 0x7a; expected[122] = 0x79; expected[123] = 0x7a;
    expected[124] = 0xc1; expected[125] = 0x88; expected[126] = 0xd1;

    int ok = 1;
    for (int i = 0; i < 127; i++) {
        if (ciphertext[i] != expected[i]) { ok = 0; }
    }

    if (ok) uart_puts("Verificacion A.2 TV#3: CORRECTO\n");
    else    uart_puts("Verificacion A.2 TV#3: ERROR\n");
}

/* ==========================================================================
 * PROGRAMA PRINCIPAL
 * Ejecuta todos los tests en orden y se queda en loop infinito al final.
 * El loop infinito es necesario en baremetal: sin sistema operativo no hay
 * a donde "retornar" cuando main() termina.
 * ========================================================================== */
int main(void) {
    test_quarter_round();    /* test basico: una sola operacion quarter round   */
    test_chacha20_block();   /* test del bloque completo con la clave del RFC   */
    test_chacha20_encrypt(); /* test de cifrado y descifrado de 114 bytes       */
    test_tv1_block();        /* TV#1: todo ceros, counter=0                     */
    test_tv2_block();        /* TV#2: todo ceros, counter=1                     */
    test_tv3_block();        /* TV#3: ultimo byte de clave=0x01, counter=1      */
    test_tv4_block();        /* TV#4: byte[1] de clave=0xff, counter=2          */
    test_tv5_block();        /* TV#5: todo ceros pero ultimo byte nonce=0x02    */

    test_a2_tv1_encrypt();   /* A.2 TV#1: encrypt con plaintext cero            */
    test_a2_tv2_encrypt();   /* A.2 TV#2: encrypt de 375 bytes                  */
    test_a2_tv3_encrypt();   /* A.2 TV#3: encrypt de 127 bytes                  */

    while (1) {}
    return 0;
}