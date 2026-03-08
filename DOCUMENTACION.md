Implementación de chacha20_quarter_round en ensamblador RISC-V.

Se verificó utilizando el vector de prueba del RFC 8439:

Input:
a = 0x11111111
b = 0x01020304
c = 0x9b8d6f43
d = 0x01234567

Output esperado:
a = 0xea2a92f4
b = 0xcb1cf8ce
c = 0x4581472e
d = 0x5881c4bb

La implementación fue ejecutada en QEMU usando el entorno Docker proporcionado.
