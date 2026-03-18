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

    while (1) {} /* nos quedamos aqui para siempre, el procesador no para */
    return 0;
}