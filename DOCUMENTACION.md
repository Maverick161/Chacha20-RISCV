# Documentación técnica — ChaCha20 en RISC-V

## Arquitectura del software

El algoritmo ChaCha20 se diseñó mediante una separación clara entre una capa de control escrita en C y una capa de ejecución escrita en ensamblador RISC-V. Este diseño permite mantener una arquitectura modular, facilitar la depuración y aprovechar el ensamblador en las partes críticas del algoritmo, donde el control preciso de registros, memoria y flujo de ejecución resulta especialmente importante.

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

Esta organización favorece la mantenibilidad del proyecto, ya que separa la lógica funcional, la infraestructura de compilación/ejecución y la evidencia documental.

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

En esta capa no se implementa ningún detalle del algoritmo. Esto reduce la complejidad en el código de prueba y hace que la interfaz externa del módulo sea simple y clara.

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

Esta división permite que el código de bajo nivel permanezca encapsulado, mientras que la capa superior en C conserva legibilidad y facilidad de prueba.

---

## Interfaces definidas entre C y ensamblador

La comunicación entre ambas capas se realiza mediante las funciones declaradas en el archivo `chacha20.h`:

```c
void chacha20_quarter_round(uint32_t *state, int a, int b, int c, int d);
void chacha20_block(uint32_t *state, uint8_t *output);
void chacha20_encrypt(uint32_t *state, uint8_t *plaintext, uint8_t *ciphertext, uint32_t length);
```

Estas interfaces fueron diseñadas para que la capa C no dependa de los detalles internos del ensamblador. Cada función tiene una responsabilidad definida:

### `chacha20_quarter_round`

Recibe un arreglo `state` y cuatro índices enteros `a`, `b`, `c` y `d`. Su función es aplicar la transformación elemental *quarter round* sobre cuatro palabras del estado. Esta interfaz basada en índices facilita la reutilización de la rutina tanto para las rondas por columna como para las rondas diagonales, evitando tener que duplicar código o preparar múltiples punteros desde C. Para la implementación se utilizaron como referencia las operaciones definidas en el documento de especificaciones.

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

De esta manera, `chacha20_encrypt` reutiliza `chacha20_block` como primitiva interna, manteniendo una arquitectura jerárquica y limpia.

---

## Justificación de las decisiones de diseño

### Separar el control del algoritmo

C se hace cargo de la construcción del estado, las pruebas y la salida por UART, mientras que la implementación del algoritmo se desarrolló en ensamblador. De esta manera se lograron las siguientes ventajas:

- El código en C es más claro para preparar pruebas y validar resultados.
- El ensamblador se concentra únicamente en el algoritmo.
- La depuración se vuelve más sencilla, porque el flujo de alto nivel y el núcleo del algoritmo están desacoplados.

### API mínima y estable

El encabezado `chacha20.h` define una interfaz pequeña y concreta. Esto reduce el acoplamiento entre archivos y permite modificar detalles internos del ensamblador sin afectar la lógica de pruebas escrita en C.

### Implementar `quarter_round` con índices y no con punteros individuales

Para la implementación de esta función era más sencillo usar punteros individuales; sin embargo, a la hora de crear la funcionalidad de `chacha20_block`, ese diseño no era el ideal. Se decidió implementar una solución usando índices sobre el arreglo de 16 palabras.

### Usar un `working_state` en el stack

En `chacha20_block` se reserva espacio en el stack para copiar el estado original a un arreglo temporal. Esto sigue fielmente la especificación de ChaCha20, donde las rondas se aplican sobre una copia del estado y al final se suma el estado original. Esta estrategia también evita modificar directamente el estado base durante las rondas.

### Procesar el cifrado en bloques de 64 bytes

La función `chacha20_encrypt` genera keystream por bloques completos de 64 bytes, lo cual coincide con la naturaleza del algoritmo ChaCha20. Para longitudes no múltiples de 64, la rutina procesa únicamente los bytes restantes del bloque final. Esto mejora la generalidad de la implementación.

# Mapeo entre el estado de 16 palabras y los registros RISC-V

El algoritmo ChaCha20 opera sobre un estado de 16 palabras de 32 bits (512 bits en total).
Dado que RISC-V de 32 bits dispone de un conjunto limitado de registros, no es posible mantener
las 16 palabras simultáneamente en registros durante toda la ejecución. La estrategia adoptada
fue diferente para cada función según su responsabilidad.

---

## `chacha20_quarter_round`

Esta función recibe cuatro **índices** sobre el arreglo `state[]` y opera únicamente sobre esas
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

Esta función **no tiene prólogo ni epílogo** porque solo usa registros `t` (temporales) y `a`
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

Esta es la función más compleja. El estado de 16 palabras **no cabe en registros** durante las
20 rondas, por lo que se almacena en el **stack** como `working_state`. Solo los punteros y el
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