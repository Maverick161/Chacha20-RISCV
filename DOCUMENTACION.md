# Documentación técnica — ChaCha20 en RISC-V

## Arquitectura del software

El algoritmo ChaCha20 se diseñó mediante una separación clara entre una capa de control escrita en C y una capa de ejecución escrita en ensamblador RISC-V. Esta separación permite hacer cambios en una capa sin afectar la otra. Por ejemplo, si se necesita modificar cómo se imprime la salida, no hay que tocar el ensamblador, y si se cambia algo interno del algoritmo, el código de pruebas en C no se ve afectado.

---

## Estructura general del proyecto

El proyecto se organiza de forma que los componentes principales del sistema queden claramente separados:

- `src/chacha20.h`: define la interfaz pública del módulo ChaCha20.
- `src/main.c`: contiene la lógica de pruebas, construcción del estado inicial, impresión por UART y validación contra vectores del RFC.
- `src/chacha20_asm.S`: implementa en ensamblador RISC-V las rutinas principales del algoritmo.
- `src/startup.s`: archivo de arranque para la ejecución bare-metal.
- `src/linker.ld`: script de enlazado para ubicar correctamente el programa en memoria.
- `docs/`: contiene capturas y evidencias visuales de ejecución.
- `tests/`: incluye pruebas auxiliares o experimentales.
- `Dockerfile`: define el entorno reproducible de compilación y depuración.
- `run.sh`: script de apoyo para ejecución del sistema.

Esta organización hizo más fácil ubicar cada parte del proyecto: el código del algoritmo, los archivos de soporte para compilar y ejecutar, y las evidencias de pruebas y depuración.
---

## Separación entre capa C y capa ensamblador

La estructura del proyecto se divide en dos capas principales:

### Capa en C

La capa en C cumple con las funciones de orquestación y validación. Sus principales responsabilidades son:

- Construir el estado inicial de ChaCha20 mediante la función `build_state()`.
- Definir y ejecutar los casos de prueba.
- Imprimir resultados por UART para poder observar la ejecución en QEMU.
- Comparar salidas con vectores de prueba oficiales del RFC 8439.
- Invocar las funciones implementadas en ensamblador.

En esta capa no se implementa ningún detalle del algoritmo. Esto mantiene el código de pruebas independiente de los detalles internos del algoritmo. Y permite validar las pruebas que sean necesarias.

### Capa en ensamblador

En esta capa se encuentra toda la implementación lógica del algoritmo. Las funciones implementadas son:

- `chacha20_quarter_round`
- `chacha20_block`
- `chacha20_encrypt`

En estas funciones se encuentran las operaciones fundamentales del cifrado: suma modular de 32 bits, XOR, rotaciones lógicas a la izquierda, procesamiento por rondas y generación del keystream. Al estar implementadas en ensamblador, se pudo controlar de manera explícita lo siguiente:

- Uso de registros temporales.
- Acceso al stack.
- Paso de argumentos según convención ABI.
- Llamadas internas entre funciones.

De esta forma el código implementado en C queda limpio y es sencillo de leer, mientras que todo el detalle del algoritmo queda dentro del ensamblador.

---

## Interfaces definidas entre C y ensamblador

La comunicación entre ambas capas se realiza mediante las funciones declaradas en el archivo `chacha20.h`:

```c
void chacha20_quarter_round(uint32_t *state, int a, int b, int c, int d);
void chacha20_block(uint32_t *state, uint8_t *output);
void chacha20_encrypt(uint32_t *state, uint8_t *plaintext, uint8_t *ciphertext, uint32_t length);
```

Las interfaces definen un punto de interacción entre C y ensamblador sin exponer detalles internos de implementación. Cada función tiene una responsabilidad definida:

### `chacha20_quarter_round`

Recibe un arreglo `state` y cuatro índices enteros `a`, `b`, `c` y `d`. Su función es aplicar la transformación elemental *quarter round* sobre cuatro palabras del estado. Esta interfaz basada en índices facilita la reutilización de la rutina tanto para las rondas por columna como para las rondas diagonales, evitando tener que duplicar la logica del algoritmo. Para la implementación se utilizaron como referencia las operaciones definidas en el documento de especificaciones.

### `chacha20_block`

Recibe el estado inicial de 16 palabras y un buffer de salida de 64 bytes. Internamente esta función realiza lo siguiente:

1. Copia el estado original a un `working_state`.
2. Ejecuta 20 rondas del algoritmo.
3. Suma el estado original al estado transformado.
4. Escribe el bloque final en el buffer de salida.

Esta función representa la unidad base de generación de keystream de ChaCha20.

### `chacha20_encrypt`

Recibe el estado, un bloque de entrada `plaintext`, un bloque de salida `ciphertext` y la longitud total en bytes. Su responsabilidad es:

1. Invocar repetidamente `chacha20_block`.
2. Generar bloques de 64 bytes de keystream.
3. Aplicar XOR entre el keystream y el texto de entrada.
4. Incrementar el contador del estado para cada bloque procesado.

De esta manera, `chacha20_encrypt` usa `chacha20_block` internamente para general el keystream de cada bloque.

---

## Justificación de las decisiones de diseño

### Separar el control del algoritmo

C se hace cargo de la construcción del estado, las pruebas y la salida por UART, mientras que la implementación del algoritmo se desarrolló en ensamblador. Al realizar esta separcion se obtuvieron las siguientes ventajas:

- El código en C solo se usa para validar pruebas y resultados.
- En el ensamblador solo se implementa el agoritmo
- La depuración se vuelve más sencilla.
### API mínima y estable

El encabezado `chacha20.h` define una interfaz pequeña y concreta. Esto evita dependencias directas entre el código de pruebas y la implementación interna.

### Implementar `quarter_round` con índices y no con punteros individuales

Al principio implementé una versión con punteros individuales para entender cómo funcionaba el quarter round. Cuando fui a implementar chacha20_block me di cuenta que era mucho más práctico usar índices sobre el arreglo, porque así podía reutilizar la misma función para las rondas de columna y las diagonales sin duplicar código

### Usar un `working_state` en el stack

En `chacha20_block` se reserva espacio en el stack para copiar el estado original a un arreglo temporal. Esto sigue fielmente la especificación de ChaCha20, donde las rondas se aplican sobre una copia del estado y al final se suma el estado original. Esta estrategia también evita modificar directamente el estado base durante las rondas.

### Procesar el cifrado en bloques de 64 bytes

La función `chacha20_encrypt` genera keystream por bloques completos de 64 bytes, lo cual coincide con la naturaleza del algoritmo ChaCha20. Para longitudes no múltiples de 64, la rutina procesa únicamente los bytes restantes del bloque final.Esto permite cifrar mensajes de cualquier longitud.

# Mapeo entre el estado de 16 palabras y los registros RISC-V

El algoritmo ChaCha20 opera sobre un estado de 16 palabras de 32 bits (512 bits en total).
Dado que RISC-V de 32 bits dispone de un conjunto limitado de registros, no es posible mantener
las 16 palabras simultáneamente en registros durante toda la ejecución. La estrategia adoptada
fue diferente para cada función según su responsabilidad.

---

## `chacha20_quarter_round`

Esta función recibe cuatro índices sobre el arreglo `state[]` y opera únicamente sobre esas
cuatro palabras a la vez.

| Registro | Rol en la función | Justificación |
|----------|-------------------|---------------|
| `a0` | Puntero base a `state[]` | Primer argumento por convención RISC-V |
| `a1` | Índice `a` (convertido a byte offset con `slli a1,a1,2`) | Segundo argumento |
| `a2` | Índice `b` (idem) | Tercer argumento |
| `a3` | Índice `c` (idem) | Cuarto argumento |
| `a4` | Índice `d` (idem) | Quinto argumento |
| `t0` | Valor `state[a]` cargado desde memoria | Temporal caller-saved, no requiere preservación |
| `t1` | Valor `state[b]` cargado desde memoria | Idem |
| `t2` | Valor `state[c]` cargado desde memoria | Idem |
| `t3` | Valor `state[d]` cargado desde memoria | Idem |
| `t5` | Auxiliar para rotaciones (`slli`) | Temporal libre, descartado tras cada `or` |
| `a5`, `a6`, `a7` | Direcciones de escritura (`store`) | Reutilizan los offsets ya calculados en `a1..a3` |
| `t4` | Dirección de escritura para `state[d]` | `a4` ya modificado, se usa `t4` para el `sw` final |

Esta función no tiene prólogo ni epílogo porque solo usa registros `t` (temporales) y `a`
(argumentos), todos caller-saved. El llamador (`chacha20_block`) ya los guarda si los necesita.

### Flujo de datos en `quarter_round`

```asm
# Conversión de índices a byte offsets
slli a1, a1, 2        # a1 = índice_a * 4

# Carga desde memoria
add  t0, a0, a1
lw   t0, 0(t0)        # t0 = state[a]

# Operación (ejemplo línea 1)
add  t0, t0, t1       # a += b
xor  t3, t3, t0       # d ^= a
slli t5, t3, 16       # parte alta para rotl(d,16)
srli t3, t3, 16       # parte baja para rotl(d,16)
or   t3, t5, t3       # d = rotl(d, 16)

# Escritura de vuelta a memoria
add  a5, a0, a1
sw   t0, 0(a5)        # state[a] = t0
```

---

## `chacha20_block`

Esta es la función más compleja. El estado de 16 palabras no cabe en registros durante las
20 rondas, por lo que se almacena en el stack como `working_state`. Solo los punteros y el
contador de rondas viven en registros `s` (callee-saved).

### Layout del stack frame (84 bytes)

```
┌─────────────────────────────────────────────────────────┐
│ sp+ 0  → ra              (return address)               │
│ sp+ 4  → s0              (contador loop: 10)            │
│ sp+ 8  → s1              (puntero state original)       │
│ sp+12  → s2              (puntero output)               │
│ sp+16  → s3              (puntero working_state → sp+20)│
├─────────────────────────────────────────────────────────┤
│ sp+20  → working_state[0]                               │
│ sp+24  → working_state[1]                               │
│   ...                                                   │
│ sp+80  → working_state[15]   (64 bytes total)           │
└─────────────────────────────────────────────────────────┘
```

### Tabla de registros

| Registro | Valor almacenado | Justificación |
|----------|-----------------|---------------|
| `s0` | Contador del loop (10→0) | Callee-saved: sobrevive a los 80 `call chacha20_quarter_round` |
| `s1` | Puntero `state` original | Callee-saved: necesario para la suma final `working_state += state` |
| `s2` | Puntero `output` (64 bytes) | Callee-saved: necesario para la serialización final |
| `s3` | Puntero a `working_state` en el stack | Callee-saved: se pasa como `a0` en cada llamada a `quarter_round` |
| `t0`, `t1` | Temporales de copia y suma | Se usan y descartan en cada par `lw/sw` |

### Por qué el estado vive en el stack y no en registros

Las 16 palabras del estado viven en el stack (`sp+20` a `sp+83`) porque cada llamada a
`chacha20_quarter_round` modifica el estado in-place a través del puntero `s3`. Mantenerlas en
registros `s` hubiera requerido 16 registros callee-saved adicionales, lo que es imposible en
RISC-V (solo existen `s0`–`s11`, y varios ya están en uso).

```asm
# Prólogo: reservar stack y guardar callee-saved
addi sp, sp, -84
sw   ra,  0(sp)
sw   s0,  4(sp)
sw   s1,  8(sp)
sw   s2, 12(sp)
sw   s3, 16(sp)

mv   s1, a0            # s1 = puntero state original
mv   s2, a1            # s2 = puntero output
li   s0, 10            # s0 = contador (10 rondas dobles)
addi s3, sp, 20        # s3 = &working_state[0]

# Loop: s3 se pasa como puntero base en cada QR
mv   a0, s3
li   a1, 0
li   a2, 4
li   a3, 8
li   a4, 12
call chacha20_quarter_round    # QR(0,4,8,12) — columna 0
```

---

## `chacha20_encrypt`

Esta función coordina múltiples llamadas a `chacha20_block` e itera sobre el mensaje completo.
Introduce un buffer de keystream también en el stack.

### Layout del stack frame (96 bytes)

```
┌─────────────────────────────────────────────────────────┐
│ sp+ 0  → ra                                             │
│ sp+ 4  → s0   (puntero a state)                         │
│ sp+ 8  → s1   (cursor sobre plaintext)                  │
│ sp+12  → s2   (cursor sobre ciphertext)                 │
│ sp+16  → s3   (bytes restantes por cifrar)              │
│ sp+20  → s4   (cursor sobre el buffer de keystream)     │
│ sp+24  → s5   (constante 64 para el if)                 │
│ sp+28  → s6   (bytes a procesar en esta iteración)      │
├─────────────────────────────────────────────────────────┤
│ sp+32  → keystream[0]                                   │
│   ...                                                   │
│ sp+92  → keystream[63]   (64 bytes)                     │
└─────────────────────────────────────────────────────────┘
```

### Tabla de registros

| Registro | Valor almacenado | Justificación |
|----------|-----------------|---------------|
| `s0` | Puntero base a `state[]` | Callee-saved: se pasa a `chacha20_block` en cada iteración y se necesita para incrementar `state[12]` |
| `s1` | Cursor sobre `plaintext` | Callee-saved: avanza byte a byte a lo largo de todo el mensaje |
| `s2` | Cursor sobre `ciphertext` | Idem |
| `s3` | Longitud restante | Callee-saved: debe sobrevivir a cada `call chacha20_block` |
| `s4` | Cursor sobre el keystream | Callee-saved: avanza durante el XOR y se resetea al inicio del buffer en cada bloque nuevo |
| `s5` | Constante `64` | Evita recargar la constante en cada iteración del loop |
| `s6` | Bytes a procesar en este bloque | Necesario para calcular `s3 -= s6` correctamente después del XOR |

### Incremento del contador

El contador `state[12]` se incrementa directamente en memoria. La palabra 12 está en el
offset `12 × 4 = 48` bytes desde el inicio del arreglo:

```asm
lw   t1, 48(s0)    # leer state[12] (counter)
addi t1, t1, 1     # counter += 1
sw   t1, 48(s0)    # guardar counter actualizado
```

### Reseteo del puntero de keystream

Al iniciar cada nuevo bloque, `s4` debe volver al inicio del buffer de keystream en `sp+32`:

```asm
addi s4, sp, 32    # s4 = &keystream[0]  ← valor correcto
```

---

## Decisión general de diseño

El criterio consistente en las tres funciones fue:

> **Registros `s` para todo lo que debe sobrevivir a una llamada a función, y registros `t`
> solo para cómputos intermedios dentro del mismo bloque de código.**

El estado de 16 palabras nunca se intenta mapear completamente a registros — siempre vive en
memoria (stack), con `s3` o `s0` como ancla de puntero. Esto respeta las convenciones de
llamada RISC-V y hace el código verificable con GDB inspeccionando directamente la región de
memoria `sp+20..sp+83` en `chacha20_block`.

### Resumen de convenciones respetadas

| Tipo de registro | Registros usados | Preservados en prólogo |
|-----------------|-----------------|------------------------|
| Argumentos de entrada | `a0`–`a4` | No (caller-saved) |
| Temporales de cómputo | `t0`–`t5` | No (caller-saved) |
| Variables de larga vida | `s0`–`s6` | Sí (callee-saved, guardados en stack) |
| Estado de 16 palabras | Stack (`sp+20`) | N/A — vive en memoria |

# Evidencias de Ejecución con GDB/QEMU

Todas las sesiones de depuración fueron realizadas dentro del entorno Docker `rvqemu`,
conectando `gdb-multiarch` al puerto 1234 expuesto por QEMU con las flags `-s -S`.
El binario fue compilado con `-g3 -gdwarf-4` para obtener símbolos completos.

Comando de conexión utilizado en todas las sesiones:

```bash
gdb-multiarch chacha20.elf
(gdb) set architecture riscv:rv32
(gdb) target remote localhost:1234
```

---
## Evidencia 1 — Estado del bloque antes y después de las 20 rondas

Para capturar el estado interno del algoritmo se colocaron dos breakpoints:
- `break round_loop` — pausa al inicio de cada ronda, capturando el estado inicial antes de
  que comience cualquier transformación.
- `break round_end` — pausa al finalizar las 20 rondas, antes de la suma final con el estado
  original.

El test utilizado fue `test_chacha20_block` (RFC 8439 2.3.2), con la clave del RFC,
`counter=1` y `nonce=0x09000000 0x4a000000 0x00000000`.

![Estado inicial y despues de las 20 rondas](docs/Evidencia1.png)

**Estado inicial** (`working_state` antes de la ronda 1, inspeccionado con `x/16xw $s3`):

```
0x61707865  0x3320646e  0x79622d32  0x6b206574   ← constantes "expand 32-byte k"
0x03020100  0x07060504  0x0b0a0908  0x0f0e0d0c   ← clave bytes  0-15
0x13121110  0x17161514  0x1b1a1918  0x1f1e1d1c   ← clave bytes 16-31
0x00000001  0x09000000  0x4a000000  0x00000000   ← counter=1, nonce del RFC 2.3.2
```

Este estado coincide exactamente con la Tabla 1 del RFC 8439 2.3.2

**Estado después de la ronda 1** (segunda parada en `round_loop`):

```
0xcd52e917  0x85ab03b4  0xb3457395  0xf96de7dd
0xc4b7cd22  0x5c2e187a  0xc95eb461  0x316c801a
0x7bf7d740  0x7eddd644  0xf1a1bdf5  0x761246ca
0x6b0d58a3  0x6798471a  0xd737f167  0xf173888d
```

Como se puede observar todos los 16 valores cambiaron drásticamente tras una sola
ronda doble (columnas + diagonales), demostrando el buen funcionamiento del algoritmo.

**Estado después de las 20 rondas** (parada en `round_end`, antes de la suma final):

```
0x837778ab  0xe238d763  0xa67ae21e  0x5950bb2f
0xc4f2d0c7  0xfc62bb2f  0x8fa018fc  0x3f5ec7b7
0x335271c2  0xf29489f3  0xeabda8fc  0x82e46ebd
0xd19c12b4  0xb04e16de  0x9e83d0cb  0x4e3c50a2
```

Este estado coincide con el estado intermedio del RFC 8439 2.3.2 
---
## Evidencia 2 — Verificación del vector de prueba TV#1 contra el RFC

Para verificar la corrección del keystream final se utilizó el Test Vector #1 del
Apéndice A.1 del RFC 8439 (clave=0x00×32, nonce=0x00×12, counter=0). Se colocó un
breakpoint en la dirección `0x800050bc`, que corresponde a la primera instrucción de
serialización de `round_end` — el punto donde `working_state` ya contiene el resultado
final de `working_state += state`.

```gdb
break test_tv1_block
continue
break *0x800050bc
continue
x/16xw $s3
```
![Verificación TV#1 contra RFC 8439 Apéndice A.1](docs/Evidencia2.png)
**Keystream generado** (inspeccionado con `x/16xw $s3` en `0x80016518`):

```
0xade0b876  0x903df1a0  0xe56a5d40  0x28bd8653
0xb819d2bd  0x1aed8da0  0xccef36a8  0xc70d778b
0x7c5941da  0x8d485751  0x3fe02477  0x374ad8b8
0xf4b8436a  0x1ca11815  0x69b687c3  0x8665eeb2
```
**Valor esperado** según RFC 8439 Apéndice A.1 (en little-endian de 32 bits):

```
76 b8 e0 ad → 0xade0b876  
a0 f1 3d 90 → 0x903df1a0  
40 5d 6a e5 → 0xe56a5d40  
53 86 bd 28 → 0x28bd8653  
bd d2 19 b8 → 0xb819d2bd  
a0 8d ed 1a → 0x1aed8da0  
a8 36 ef cc → 0xccef36a8  
8b 77 0d c7 → 0xc70d778b  
da 41 59 7c → 0x7c5941da  
51 57 48 8d → 0x8d485751  
77 24 e0 3f → 0x3fe02477  
b8 d8 4a 37 → 0x374ad8b8  
6a 43 b8 f4 → 0xf4b8436a  
15 18 a1 1c → 0x1ca11815  
c3 87 b6 69 → 0x69b687c3  
b2 ee 65 86 → 0x8665eeb2  
```

Los 64 bytes del keystream coinciden exactamente byte a byte con el RFC 8439 

---
## Evidencia 3 — Registros s0–s11 preservados (convenciones de llamada)

Para verificar que `chacha20_block` respeta las convenciones de llamada RISC-V se
inspeccionaron los registros `s0`–`s11` en dos momentos dentro de `test_tv1_block`:

- **Antes del call** (`0x80000ff4`): instrucción `jal ra, chacha20_block`
- **Después del ret** (`0x80000ff8`): primera instrucción tras el retorno

```gdb
break *0x80000ff4
continue
info registers s0 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11
break *0x80000ff8
continue
info registers s0 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11
```
![Registros s0-s11 antes del call a chacha20_block](docs/Evidencia3oficial.png)
![Registros s0-s11 después del retorno de chacha20_block](docs/Evidencia3oficialdos.png)

**Comparación de registros antes y después:**

| Registro | Antes del call | Después del ret | ¿Preservado? |
|----------|---------------|-----------------|--------------|
| `s0` | `0x80016638` | `0x80016638` | Sí |
| `s1` | `0x800165d8` | `0x800165d8` | Sí |
| `s2` | `0x80016598` | `0x80016598` | Sí |
| `s3` | `0x80016518` | `0x80016518` | Sí |
| `s4` | `0x0` | `0x0` | Sí |
| `s5` | `0x0` | `0x0` | Sí |
| `s6` | `0x9f74a669` | `0x9f74a669` | Sí |
| `s7`–`s11` | `0x0` | `0x0` | Sí |

Todos los registros callee-saved (`s0`–`s11`) tienen valores idénticos antes y después
de la llamada a `chacha20_block`, confirmando que el prólogo y epílogo de la función
guardan y restauran correctamente el estado del llamador según las convenciones RISC-V.

---
## Evidencia 4 — Incremento del contador entre bloques

Para verificar el correcto manejo del contador se utilizó `test_chacha20_encrypt` (114 bytes
= 2 bloques de 64 y 50 bytes respectivamente, con `counter` inicial = 1). Se colocó un
breakpoint en la instrucción `sw t1, 48(s0)` (`0x800051e8`), que es donde el counter
incrementado se escribe de vuelta al estado.

```gdb
break test_chacha20_encrypt
continue
break *0x800051e8
continue
x/xw $s0+48    ← counter después del bloque 1
continue
x/xw $s0+48    ← counter después del bloque 2
```
![Incremento del contador entre bloques en chacha20_encrypt](docs/Evidencia4.png)
**Resultados observados:**

| Momento | Dirección | Valor del counter |
|---------|-----------|-------------------|
| Después del bloque 1 | `0x800164ac` | `0x00000002` (1→2)|
| Después del bloque 2 | `0x800164ac` | `0x00000003` (2→3)|

El counter se incrementa correctamente en 1 por cada bloque de 64 bytes procesado,
tal como especifica el RFC 8439 §2.3. Esto confirma que el counter se maneja bien entre bloques.

> **Nota:** Durante esta sesión también se identificó un bug en el reseteo del puntero
> `s4` al inicio de cada iteración (`addi s4, sp, 28` en lugar de `addi s4, sp, 32`).
> Este bug se documenta en detalle en la sección Bitácora de Bug.

# Bitácora de Bug

## Bug: Reseteo incorrecto del puntero de keystream en `chacha20_encrypt`

### Descripción

Durante el desarrollo de `chacha20_encrypt` se introdujo un error en el offset usado
para resetear el puntero `s4` al inicio del buffer de keystream al comienzo de cada
nueva iteración del loop principal.

El stack frame de `chacha20_encrypt` tiene la siguiente estructura:

```
sp+ 0  → ra
sp+ 4  → s0   (puntero a state)
sp+ 8  → s1   (cursor plaintext)
sp+12  → s2   (cursor ciphertext)
sp+16  → s3   (bytes restantes)
sp+20  → s4   (cursor keystream)
sp+24  → s5   (constante 64)
sp+28  → s6   (bytes a procesar en la iteración actual)   ← aquí está s6
sp+32  → keystream[0..63]  (64 bytes)                     ← aquí empieza el keystream
```

El keystream buffer comienza en `sp+32`, pero la instrucción de reseteo usaba `sp+28`:

```asm
Codigo incorrecto: apunta a donde está guardado s6, no al keystream
addi s4, sp, 28    # resetear s4 al inicio del keystream
```
### Detección

El bug fue detectado durante el análisis del código fuente al revisar el stack frame
de `chacha20_encrypt`. Al comparar el offset de inicialización de `s4` en el prólogo
con el offset de reseteo al final del `xor_loop`, se encontró una inconsistencia:

```asm
addi s4, sp, 32   # inicialización en el prólogo   ← correcto (sp+32)
...
addi s4, sp, 28   # reseteo en xor_loop            ← incorrecto (sp+28)
```

La inicialización en el prólogo apuntaba correctamente a `sp+32`, pero el reseteo
al final de cada iteración apuntaba a `sp+28`, que es la posición donde el epílogo
guarda `s6`, no donde comienza el keystream.

Se utilizó `grep` para localizar ambas ocurrencias en el archivo fuente y confirmar
la discrepancia:

![Código con el bug: ambas líneas con sp+28](docs/BUg.png)
Como se observa en la captura, con el bug presente ambas líneas apuntan a `sp+28`:
- **Línea 417**: `addi s4, sp, 28  #empieza el keystream` ← inicialización incorrecta
- **Línea 456**: `addi s4, sp, 28  # resetear s4 al inicio del keystream` ← reseteo incorrecto

Adicionalmente, se inspeccionó el binario con GDB usando `disassemble chacha20_encrypt`
para verificar cómo el compilador tradujo el código:

![Disassemble de chacha20_encrypt en GDB](docs/Disassemble.png)
El disassemble muestra que en `0x8000518c <+52>` la instrucción es `addi s4,sp,32`,
confirmando que el prólogo inicializa `s4` correctamente. El bug estaba únicamente
en el reseteo dentro del `xor_loop` (no visible en este fragmento del disassemble
por estar más abajo en la función).

### Por qué los tests pasaban a pesar del bug

El bug solo se activaba a partir del segundo bloque de cada llamada a
`chacha20_encrypt`, cuando `s4` necesitaba resetearse. En el primer bloque,
`s4` se inicializaba correctamente desde el prólogo con `sp+32`.

A partir del segundo bloque, `s4` apuntaba a `sp+28` — la posición de `s6` en el
stack — en lugar de `sp+32` donde comienza el keystream. Sin embargo, dado que
`chacha20_block` escribe el keystream comenzando en `sp+32` y el epílogo de
`chacha20_block` restaura los registros `s` antes de retornar, la región `sp+28`
podía contener valores que casualmente no alteraban el resultado final en los
vectores de prueba específicos del RFC.
### Corrección

La corrección consistió en cambiar el offset de `sp+28` a `sp+32` en la instrucción
de reseteo dentro del `xor_loop`.
Se aplicó la corrección con `sed` y se verificó con `grep`:
![Código corregido: ambas líneas con sp+32](docs/Nobug.png)

### Lección aprendida
Lo que aprendí con este bug es que en ensamblador es muy fácil equivocarse con los offsets del stack si no se tiene una claridad a la hora de crear los registros. En este caso había dos líneas que referenciaban el mismo buffer pero con offsets distintos, y eso fue suficiente para introducir un error sutil

# Análisis de Resultados

## Resumen de resultados obtenidos

La implementación de ChaCha20 en ensamblador RISC-V fue validada contra un total de
**11 vectores de prueba oficiales** del RFC 8439, abarcando las tres funciones
principales del algoritmo. Todos los tests produjeron resultados correctos tras la
corrección del bug documentado en la sección anterior.

| Test | Función | Fuente RFC | Resultado |
|------|---------|------------|-----------|
| Quarter Round | `chacha20_quarter_round` | 2.1.1 | Correcto |
| Block 2.3.2 | `chacha20_block` | 2.3.2 | Correcto |
| Encrypt 2.4.2 | `chacha20_encrypt` | 2.4.2 | Correcto |
| TV#1 (key=0, ctr=0) | `chacha20_block` | Apéndice A.1 | Correcto |
| TV#2 (key=0, ctr=1) | `chacha20_block` | Apéndice A.1 | Correcto |
| TV#3 (key byte31=0x01) | `chacha20_block` | Apéndice A.1 | Correcto |
| TV#4 (key byte1=0xff) | `chacha20_block` | Apéndice A.1 | Correcto |
| TV#5 (nonce byte11=0x02) | `chacha20_block` | Apéndice A.1 | Correcto |
| A.2 TV#1 (64 bytes) | `chacha20_encrypt` | Apéndice A.2 | Correcto |
| A.2 TV#2 (375 bytes) | `chacha20_encrypt` | Apéndice A.2 | Correcto |
| A.2 TV#3 (127 bytes, ctr=42) | `chacha20_encrypt` | Apéndice A.2 | Correcto |

---

## Conclusión

La implementación cumple con todos los requisitos funcionales establecidos en
el RFC 8439 y en el enunciado del proyecto. El algoritmo ChaCha20 fue
implementado exitosamente en ensamblador RISC-V respetando las
convenciones de llamada, con un manejo correcto del stack frame en las tres
funciones y verificado exhaustivamente con los vectores de prueba oficiales.

La parte más difícil del proyecto fue entender el algoritmo como tal y luego traducirlo correctamente a ensamblador. El bug del stack frame fue un buen ejemplo de eso — un error de 4 bytes en un offset que era difícil de ver solo leyendo el código. Aprendí que antes de escribir ensamblador conviene tener muy claro en papel cómo está organizado el stack, porque un offset equivocado puede ser muy difícil de encontrar después.

## Referencias

- RFC 8439: https://www.rfc-editor.org/rfc/rfc8439
- RISC-V Calling Conventions: https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf