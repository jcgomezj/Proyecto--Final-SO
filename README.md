# IO-Editor — Pipeline de Compresión + Cifrado RC4

Editor de texto en terminal que implementa un pipeline de guardado seguro: comprime con Huffman o LZ77, cifra con RC4 propio, y escribe al disco. La lectura invierte el proceso de forma simétrica.

Proyecto 3 — Sistemas Operativos  
Universidad —  Autores: Estefa, Juanjo, JuanCa

---

## Cómo compilar

Requiere GCC y `make`. En Ubuntu/Debian:

```bash
sudo apt install build-essential
make
```

El binario resultante es `./editor`.

Para limpiar y recompilar desde cero:

```bash
make clean && make
```

---

## Cómo usar

**Abrir un archivo nuevo:**
```bash
stty -ixon && ./editor mi_archivo.txt
```

**Abrir un archivo ya cifrado:**
```bash
./editor mi_archivo.txt.huff.rc4
# o
./editor mi_archivo.txt.lz77.rc4
```

> `stty -ixon` es necesario en algunos terminales para que Ctrl+S llegue al programa en lugar de ser interceptado por el control de flujo del shell.

**Controles dentro del editor:**

| Tecla | Acción |
|---|---|
| Ctrl+S | Guardar (comprime + cifra + escribe al disco) |
| Ctrl+X | Salir |
| Cualquier tecla | Escribir texto |

Al guardar, el programa pide una passphrase por consola (sin eco). Se generan dos archivos: uno con Huffman+RC4 (`.huff.rc4`) y otro con LZ77+RC4 (`.lz77.rc4`), más un reporte de métricas (`.report`).

Al abrir un archivo cifrado, pide la misma passphrase. Si no coincide, el pipeline aborta.

---

## Estructura del proyecto

```
.
├── src/
│   ├── main.c          # Lógica principal, pipeline guardar/abrir, encrypt_buffer()
│   ├── rc4.c           # Implementación RC4: KSA, PRGA, rc4_context_destroy()
│   ├── huffman.c       # Compresión/descompresión Huffman
│   ├── lz77.c          # Compresión/descompresión LZ77
│   ├── gap_buffer.c    # Estructura de datos del editor
│   └── file_io.c       # Lectura/escritura raw al disco
├── include/            # Headers de cada módulo
├── tests/              # Archivos de prueba para benchmarks
├── INFORME_P3.md       # Informe técnico completo
├── profiling_results_p3.txt  # Resultados de profiling
└── Makefile
```

---

## Arquitectura del pipeline

```
GUARDAR:
  texto_plano → [Huffman / LZ77] → compressed_buf
             → [RC4 en RAM]      → encrypted_buf
             → write() al disco  → archivo.huff.rc4 / archivo.lz77.rc4

ABRIR:
  disco → file_read_raw() → [RC4 decrypt]        → compressed_buf
       → [Huffman / LZ77 decompress]             → texto_plano
       → gap_buffer_load()
```

El orden comprimir→cifrar es obligatorio: RC4 produce salida pseudoaleatoria con entropía ~8 bits/byte, lo que hace imposible comprimir después del cifrado.

---

## Seguridad en RAM

La passphrase nunca queda expuesta más tiempo del necesario:

1. `mlock()` bloquea la página de RAM que contiene la passphrase — el kernel no puede enviarla a swap
2. `rc4_init()` absorbe la passphrase en el S-box
3. `explicit_bzero()` destruye la passphrase inmediatamente después
4. `rc4_crypt()` cifra usando solo el S-box
5. `rc4_context_destroy()` destruye el S-box con otro `explicit_bzero()`
6. `munlock()` libera el pin de memoria al salir

`explicit_bzero()` se usa en lugar de `memset()` porque el compilador puede eliminar un `memset` sobre memoria que ya no se usa (Dead Store Elimination). `explicit_bzero` garantiza que el borrado ocurra siempre.

---

## Formato de archivos generados

```
.huff.rc4 → [ "HUFR" (4 bytes magic) ][ RC4(Huffman(texto)) ]
.lz77.rc4 → [ "LZRC" (4 bytes magic) ][ RC4(LZ77(texto))   ]
```

El magic header no está cifrado para permitir detección automática del formato al abrir.