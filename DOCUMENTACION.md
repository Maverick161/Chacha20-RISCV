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
