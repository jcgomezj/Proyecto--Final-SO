# Informe Técnico — Proyecto 3: Sistemas Operativos
## IO-Editor: Pipeline de Compresión + Cifrado RC4

---

## 1. Arquitectura del pipeline

El editor implementa dos flujos simétricos: uno de guardado y uno de apertura.

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

### ¿Por qué comprimir primero y cifrar después?

El texto plano tiene distribución no uniforme de bytes: `'e'` aparece mucho más que `'z'`. Huffman y LZ77 explotan exactamente esa redundancia estadística para reducir el tamaño. RC4, en cambio, produce una salida pseudoaleatoria con distribución casi uniforme (~8 bits/byte de entropía). Si cifráramos primero, los compresores no encontrarían ningún patrón y el resultado sería igual o mayor al original. El orden correcto es siempre **comprimir → cifrar**.

---

## 2. Implementación RC4

Se eligió RC4 sobre AES por tres razones concretas:

- Es cifrado de flujo, por lo que el tamaño cifrado es exactamente igual al tamaño comprimido (AES-CBC añade hasta 16 bytes de padding)
- Su implementación completa cabe en menos de 60 líneas de C sin librerías externas
- Cumple el mandato del proyecto de implementación propia

### KSA (Key Scheduling Algorithm)

```c
void rc4_init(RC4Context *ctx, const uint8_t *key, size_t key_len) {
    for (int i = 0; i < 256; i++) S[i] = i;
    j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % key_len]) & 0xFF;
        swap(S[i], S[j]);
    }
}
```

### PRGA + XOR

```c
void rc4_crypt(RC4Context *ctx, const uint8_t *in, uint8_t *out, size_t len) {
    for (size_t k = 0; k < len; k++) {
        i = (i + 1) & 0xFF;
        j = (j + S[i]) & 0xFF;
        swap(S[i], S[j]);
        out[k] = in[k] ^ S[(S[i] + S[j]) & 0xFF];
    }
}
```

El XOR es involutivo: aplicarlo dos veces con la misma clave devuelve el dato original. Por eso `rc4_crypt()` sirve tanto para cifrar como para descifrar sin lógica adicional.

---

## 3. Gestión segura de la llave en RAM

Este es el aspecto más relevante desde el punto de vista del sistema operativo. El problema no es solo elegir un buen algoritmo de cifrado, sino asegurarse de que la llave no quede expuesta en memoria más tiempo del estrictamente necesario.

### Medidas implementadas

| Medida | Implementación | Propósito |
|---|---|---|
| Sin hardcoding | `read_passphrase()` por consola | La llave no está en el binario |
| Sin eco visual | `termios` sin `ECHO` | La llave no aparece en pantalla |
| Sin argv | Consola, no `argv[]` | `ps aux` no expone la llave |
| `mlock()` | Antes de leer la passphrase | El SO no puede enviar la página a swap |
| `explicit_bzero()` llave | Inmediatamente tras `rc4_init()` | La llave plana desaparece en RAM |
| `rc4_context_destroy()` | Tras `rc4_crypt()` | El S-box derivado también desaparece |
| `munlock()` | Al salir del programa | Liberar el pin de memoria |

### Ciclo de vida de la llave

```
1. char passphrase[128]            ← mlock() bloquea la página
2. read_passphrase()               ← terminal sin eco
3. rc4_init(ctx, passphrase, len)  ← KSA genera S-box
4. explicit_bzero(passphrase, 128) ← LLAVE DESTRUIDA
5. rc4_crypt(ctx, ...)             ← cifra usando solo el S-box
6. rc4_context_destroy(ctx)        ← S-BOX DESTRUIDO
7. munlock(passphrase, 128)        ← liberar pin
```

### Por qué explicit_bzero y no memset

Con optimizaciones activadas, el compilador puede detectar que un `memset` escribe en memoria que "ya no se usa" y eliminarlo. Esto se llama Dead Store Elimination. `explicit_bzero` está declarado de forma que esa optimización no aplica — el borrado ocurre siempre, sin importar el nivel de optimización.

---

## 4. Resultados del benchmark

### Archivo bench_text.txt (237,000 bytes)

| Métrica | Clásico | Solo compresión | Compresión + RC4 | Impacto |
|---|---|---|---|---|
| Tamaño (Huffman) | 237,000 B | 138,561 B | 138,561 B | -41.5% |
| Tamaño (LZ77) | 237,000 B | 29,888 B | 29,888 B | -87.4% |
| CPU Huffman | 0.002 ms | 2.18 ms | 2.23 ms | — |
| CPU RC4 (aislado) | — | — | 0.52 ms | overhead medido |
| CPU Total (H+RC4) | 0.002 ms | 2.18 ms | 2.75 ms | +25.4% CPU |
| CPU LZ77 | 0.002 ms | 52.95 ms | 52.96 ms | — |
| CPU RC4 sobre LZ77 | — | — | 0.10 ms | overhead ~0.2% |

### Archivo xlarge.txt (199,232 bytes)

| Pipeline | Tamaño final | Ratio | CPU compresión | CPU RC4 | CPU total |
|---|---|---|---|---|---|
| Clásico | 199,232 B | 100% | 0.001 ms | — | 0.001 ms |
| Huffman solo | 103,155 B | 51.8% | 3.30 ms | — | 3.30 ms |
| LZ77 solo | 66,108 B | 33.2% | 152.76 ms | — | 152.76 ms |
| Huffman + RC4 | 103,155 B | 51.8% | 3.25 ms | 0.35 ms | 3.60 ms |
| LZ77 + RC4 | 66,108 B | 33.2% | 154.32 ms | 0.21 ms | 154.53 ms |

El overhead de RC4 sobre LZ77 es menor al 0.2% del tiempo total. El cuello de botella es la compresión, no el cifrado. Agregar seguridad criptográfica tiene un costo computacional marginal cuando se usa un cifrado de flujo.

---

## 5. Balance espacio / tiempo / seguridad

| Dimensión | Resultado | Detalle |
|---|---|---|
| Espacio (I/O) | Ganancia | LZ77+RC4: 87.4% menos bytes al disco |
| Tiempo (CPU) | Costo aceptable | RC4 añade menos de 0.5 ms sobre la compresión |
| Seguridad | Datos completamente cifrados en reposo | — |

Añadir RC4 al pipeline casi no afecta el tiempo total (overhead menor al 1% sobre LZ77, ~19% sobre Huffman) y a cambio el archivo ocupa entre 41-87% menos en disco completamente cifrado.

---

## 6. Cumplimiento de restricciones técnicas

- No se usa OpenSSL ni ninguna librería criptográfica externa
- La transformación ocurre íntegramente en buffers de RAM antes de `write()`
- La llave se borra con `explicit_bzero()` inmediatamente tras `rc4_init()`
- El S-box se destruye con `rc4_context_destroy()` tras cifrar
- `mlock()` previene que la llave llegue a swap

---

## 7. Preguntas de sustentación

**¿Qué pasa si ciframos primero y luego comprimimos?**

RC4 genera datos pseudoaleatorios con entropía ~8 bits/byte, distribución uniforme. Huffman y LZ77 buscan patrones repetitivos y frecuencias no uniformes. Al no haber patrones en datos cifrados, la compresión no reduce nada y puede producir un archivo más grande por el overhead de los headers. El orden correcto es comprimir → cifrar.

**¿La llave queda en swap si el SO la mueve antes de borrarla?**

Sí, es un riesgo real. La mitigación es `mlock(passphrase, PASSPHRASE_MAX)`, que bloquea la página física de RAM y prohíbe al kernel enviarla a swap mientras el proceso corre. Complementariamente, `explicit_bzero()` garantiza que el borrado no sea eliminado por Dead Store Elimination.

**¿Por qué un buffer de 4096 bytes y no otro tamaño?**

4096 bytes es el tamaño estándar de una página de memoria virtual en x86_64 con Linux, y también el tamaño de bloque de ext4. Alinear los buffers a este tamaño evita lecturas/escrituras parciales de página: con un buffer de 5000 bytes cada operación I/O cruzaría un límite de página y obligaría al hardware a una segunda operación. Con 4096, cada operación mapea exactamente a páginas completas del kernel.

---

## 8. Formato de archivos en disco

```
.huff.rc4:  [ "HUFR" (4 bytes) ][ RC4( Huffman(texto) ) ]
.lz77.rc4:  [ "LZRC" (4 bytes) ][ RC4( LZ77(texto)   ) ]
```

El magic header no está cifrado para permitir detección automática del formato al abrir el archivo. Si los primeros 4 bytes no coinciden con ninguna firma conocida, el pipeline aborta.
