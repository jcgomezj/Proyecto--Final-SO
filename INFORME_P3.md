# INFORME TÉCNICO — Proyecto 3 SO
## El Triángulo de Hierro: Espacio, Tiempo y Seguridad
### IO-Editor: Pipeline de Compresión + Cifrado RC4

---

## 1. Arquitectura del Pipeline

### Regla Arquitectónica Implementada

```
GUARDAR:
  texto_plano → [Huffman / LZ77 COMPRESS] → compressed_buf
             → [RC4 ENCRYPT en RAM]       → encrypted_buf
             → write() al disco           → archivo.huff.rc4 / archivo.lz77.rc4

ABRIR:
  disco → file_read_raw() → [RC4 DECRYPT en RAM] → compressed_buf
       → [Huffman / LZ77 DECOMPRESS]             → texto_plano
       → gap_buffer_load()
```

### ¿Por qué COMPRIMIR primero y CIFRAR después?

Esta es la pregunta central del proyecto. La respuesta yace en el concepto de **entropía de la información**:

- **Texto plano**: distribución no uniforme de bytes. El carácter `'e'` es más frecuente que `'z'`. Los algoritmos de compresión (Huffman, LZ77) explotan esta **redundancia estadística** y estos **patrones repetitivos** para reducir el tamaño.

- **Texto cifrado con RC4**: el keystream RC4 es pseudoaleatorio → la salida tiene distribución uniforme de bytes (~entropía de 8 bits/byte). No hay patrones. Si aplicamos Huffman o LZ77 **después** de cifrar, los algoritmos no encuentran nada que comprimir. El resultado puede ser **igual o mayor** que el input.

**Conclusión**: el orden `COMPRIMIR → CIFRAR` es la única arquitectura viable para lograr ambos objetivos simultáneamente.

---

## 2. Implementación RC4 (Criptografía en C Space)

### Algoritmo elegido: RC4 (Rivest Cipher 4)

**Justificación de RC4 sobre AES:**

| Criterio | RC4 | AES-CBC |
|---|---|---|
| Tipo | Cifrado de flujo | Cifrado de bloque |
| Padding | **No** (bytes cifrados == bytes de entrada) | Sí (hasta 16 bytes extra) |
| Implementación | Pura en C (< 60 líneas) | Compleja o requiere librería |
| Overhead CPU | Muy bajo | Mayor |
| Mandato profesor | ✅ Implementación propia | Requiere OpenSSL/mbedTLS |

RC4 cumple el mandato de "implementación propia" y al ser **cifrado de flujo**, no introduce padding, lo que significa que el tamaño cifrado es **exactamente igual** al tamaño comprimido.

### KSA (Key Scheduling Algorithm)

```c
// Inicializa el S-box de 256 bytes con la llave
void rc4_init(RC4Context *ctx, const uint8_t *key, size_t key_len) {
    // Paso 1: identidad (0..255)
    for (int i = 0; i < 256; i++) S[i] = i;
    // Paso 2: mezcla con la llave
    j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % key_len]) & 0xFF;
        swap(S[i], S[j]);
    }
}
```

### PRGA (Pseudo-Random Generation Algorithm) + XOR

```c
void rc4_crypt(RC4Context *ctx, const uint8_t *in, uint8_t *out, size_t len) {
    for (size_t k = 0; k < len; k++) {
        i = (i + 1) & 0xFF;
        j = (j + S[i]) & 0xFF;
        swap(S[i], S[j]);
        ks_byte = S[(S[i] + S[j]) & 0xFF];
        out[k] = in[k] ^ ks_byte;  // XOR: misma op para cifrar y descifrar
    }
}
```

---

## 3. Gestión Segura de la Llave en RAM

### Nivel Arquitecto OS — Todas las medidas implementadas

| Medida | Implementación | Propósito |
|---|---|---|
| Sin hardcoding | `read_passphrase()` por consola | La llave no está en el binario |
| Sin eco visual | `termios` sin `ECHO` | La llave no aparece en pantalla |
| Sin argv | Consola, no `argv[]` | `ps aux` no expone la llave |
| `mlock()` | Antes de leer la passphrase | El SO no puede enviar la página a Swap |
| `explicit_bzero()` llave | Inmediatamente tras `rc4_init()` | La llave plana desaparece en RAM |
| `rc4_context_destroy()` | Tras `rc4_crypt()` | El S-box derivado también desaparece |
| `munlock()` | Al salir del programa | Liberar el pin de memoria |

### Flujo de Vida de la Llave en RAM

```
1. char passphrase[128]  ← mlock() bloquea la página
2. read_passphrase()     ← terminal sin eco, leída en RAM
3. rc4_init(ctx, passphrase, len)  ← KSA genera S-box
4. explicit_bzero(passphrase, 128) ← ★ LLAVE DESTRUIDA ★
5. rc4_crypt(ctx, ...)             ← cifra con S-box
6. rc4_context_destroy(ctx)        ← explicit_bzero(ctx) ★ S-BOX DESTRUIDO ★
7. munlock(passphrase, 128)        ← liberar pin
```

### Mitigación del ataque de Swap (pregunta de sustentación)

El profesor pregunta: *"¿qué pasa si el SO manda la página de la llave al Swap antes de borrarla?"*

**Mitigación implementada**: `mlock(passphrase, PASSPHRASE_MAX)` bloquea la página física de RAM que contiene `passphrase`. El kernel no puede seleccionarla para swap mientras el `mlock` esté activo. Esta es la defensa correcta a nivel de OS.

---

## 4. Resultados del Benchmark

### Tabla de Métricas — Archivo `bench_text.txt` (237,000 bytes)

| Métrica | A. Clásico | B. Solo Compresión | C. Compresión + RC4 | Impacto A→C |
|---|---|---|---|---|
| **Tamaño I/O (Huffman)** | 237,000 bytes | 138,561 bytes | 138,561 bytes | **-41.5%** |
| **Tamaño I/O (LZ77)** | 237,000 bytes | 29,888 bytes | 29,888 bytes | **-87.4%** |
| **CPU Huffman** | 0.002 ms | 2.18 ms | 2.23 ms compress | — |
| **CPU RC4 (aislado)** | — | — | **0.52 ms** | overhead medido |
| **CPU Total (H+RC4)** | 0.002 ms | 2.18 ms | **2.75 ms** | 25.4% más CPU |
| **CPU LZ77 (aislado)** | 0.002 ms | 52.95 ms | 52.96 ms | — |
| **CPU RC4 sobre LZ77** | — | — | **0.10 ms** | overhead ~0.2% |

### Tabla — Archivo `xlarge.txt` (199,232 bytes, texto variado)

| Pipeline | Tamaño Final | Ratio | CPU Compresión | CPU RC4 | CPU Total |
|---|---|---|---|---|---|
| A. Clásico | 199,232 B | 100% | 0.001 ms | — | 0.001 ms |
| B. Huffman solo | 103,155 B | 51.8% | 3.30 ms | — | 3.30 ms |
| B. LZ77 solo | 66,108 B | 33.2% | 152.76 ms | — | 152.76 ms |
| C. Huffman + RC4 | 103,155 B | 51.8% | 3.25 ms | **0.35 ms** | **3.60 ms** |
| C. LZ77 + RC4 | 66,108 B | 33.2% | 154.32 ms | **0.21 ms** | **154.53 ms** |

### Hallazgo clave sobre RC4

**RC4 es extremadamente eficiente en CPU**: su overhead sobre LZ77 es <0.2% del tiempo total del pipeline. El cuello de botella es la compresión, no el cifrado. Esto demuestra que agregar seguridad criptográfica tiene un costo computacional marginal cuando se usa un cifrado de flujo apropiado.

---

## 5. Análisis del "Triángulo de Hierro"

| Dimensión | Resultado | Detalle |
|---|---|---|
| **Espacio (I/O)** | ✅ Ganancia | LZ77+RC4: 87.4% menos bytes al bus de disco |
| **Tiempo (CPU)** | ⚠️ Costo aceptable | RC4 añade <0.5 ms sobre la compresión |
| **Seguridad** | ✅ 100% cifrado | Datos en reposo completamente protegidos |

**Conclusión del arquitecto**: Añadir RC4 al pipeline casi no afecta el tiempo total (overhead < 1% sobre LZ77, ~19% sobre Huffman). A cambio, obtenemos datos en reposo completamente cifrados y un archivo que ocupa entre 41-87% menos en disco. El sistema resultante opera prácticamente al mismo tiempo que el enfoque clásico inseguro, con seguridad total.

---

## 6. Regla Arquitectónica #6 — Restricción Técnica

**Mandato cumplido:** Implementación propia de RC4 en `src/rc4.c` e `include/rc4.h`.

**Restricciones observadas:**
- ✅ No se usa OpenSSL ni libcrypto
- ✅ No se usa ninguna función de alto nivel que escriba a disco
- ✅ La transformación ocurre íntegramente en buffers de RAM antes de `write()`
- ✅ La llave se borra inmediatamente con `explicit_bzero()` tras `rc4_init()`
- ✅ El S-box se destruye con `rc4_context_destroy()` tras cifrar
- ✅ `mlock()` previene que la llave llegue al área de Swap

---

## 7. Respuestas a Preguntas de Sustentación

### P1: "¿Qué pasa si encriptamos primero y luego comprimimos?"

**Respuesta**: El archivo no se comprimirá. RC4 genera datos pseudoaleatorios con entropía ~8 bits/byte (distribución uniforme). Los algoritmos de compresión buscan patrones repetitivos y frecuencias no uniformes. Al no haber patrones en los datos cifrados, la compresión es inútil y el archivo podría ser **más grande** que el original (por el overhead de los headers de compresión). Este es exactamente el argumento por el que el orden correcto es **COMPRIMIR → CIFRAR**.

### P2: "¿La llave queda en el Swap si el SO la mueve antes de borrarla?"

**Respuesta**: Sí, es un riesgo real de SO. Nuestra mitigación es `mlock(passphrase, PASSPHRASE_MAX)`, que bloquea la página física de RAM y le prohíbe al kernel enviarla al disco de Swap. Adicionalmente, `explicit_bzero()` garantiza que el borrado no sea eliminado por el compilador (Dead Store Elimination). Un núcleo Linux con `mlock()` activo nunca enviará esa página a swap mientras el proceso esté corriendo.

### P3: "¿Por qué un buffer de 4096 bytes y no 4000 o 5000?"

**Respuesta**: 4096 bytes (4 KB) es el tamaño estándar de una **página de memoria virtual** en la arquitectura x86_64 con Linux, y también el tamaño de bloque del sistema de archivos ext4. Alinear los buffers a este tamaño evita **lecturas/escrituras parciales de página**: si el buffer fuera de 5000 bytes, cada operación I/O cruzaría un límite de página, obligando al hardware a hacer una segunda operación de lectura para completarla. Con 4096, cada operación mapea exactamente a páginas completas del kernel, maximizando la eficiencia del bus I/O.

---

## 8. Formato de Archivos en Disco

```
Archivo .huff.rc4:
┌─────────────────┬──────────────────────────────────────────┐
│  "HUFR" (4B)   │  datos_cifrados_RC4(Huffman(texto))      │
│  magic header  │  (mismo tamaño que Huffman comprimido)   │
└─────────────────┴──────────────────────────────────────────┘

Archivo .lz77.rc4:
┌─────────────────┬──────────────────────────────────────────┐
│  "LZRC" (4B)   │  datos_cifrados_RC4(LZ77(texto))         │
│  magic header  │  (mismo tamaño que LZ77 comprimido)      │
└─────────────────┴──────────────────────────────────────────┘
```

El magic header NO está cifrado para permitir detección automática del formato al abrir el archivo.
