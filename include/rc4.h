#ifndef RC4_H
#define RC4_H

/*
 * rc4.h — Implementación propia de RC4 (Rivest Cipher 4)
 *
 * RC4 es un cifrado de flujo simétrico: genera un keystream pseudoaleatorio
 * que se XOR con el plaintext. La misma operación sirve para cifrar y descifrar.
 *
 * ¿Por qué RC4 y no AES?
 *   - RC4 es un cifrado de FLUJO → no necesita padding de bloque.
 *   - El overhead de CPU es mínimo comparado con AES-CBC.
 *   - No requiere librerías externas: implementación pura en C.
 *   - Cumple el mandato del profesor: "implementación propia".
 *
 * Nota de seguridad académica:
 *   RC4 tiene vulnerabilidades conocidas (WEP, TLS). En producción se usaría
 *   ChaCha20. Para este ejercicio de OS, ilustra perfectamente el concepto
 *   de cifrado en buffer RAM sin tocar el disco en plaintext.
 *
 * Gestión de la llave en memoria (Regla Arquitectónica #6):
 *   La llave se copia al contexto RC4Context. El llamador DEBE:
 *   1. mlock() la página antes de cargar la llave.
 *   2. explicit_bzero() el buffer de llave tras rc4_init().
 *   3. explicit_bzero() el RC4Context tras usarlo.
 */

#include <stddef.h>
#include <stdint.h>

/* Tamaño fijo del S-box de RC4 */
#define RC4_SBOX_SIZE 256

/* Tamaño máximo de llave RC4 (40-2048 bits; usamos 256 bytes máx) */
#define RC4_KEY_MAX_LEN 64

typedef struct {
    uint8_t S[RC4_SBOX_SIZE]; /* Permutation array (S-box) */
    uint8_t i;                /* Índice i del PRGA */
    uint8_t j;                /* Índice j del PRGA */
} RC4Context;

/*
 * rc4_init: inicializa el S-box (KSA - Key Scheduling Algorithm).
 *
 * key     - puntero a la llave (texto plano en RAM)
 * key_len - longitud de la llave en bytes
 * ctx     - contexto RC4 a inicializar
 *
 * Tras llamar a esta función, el llamador DEBE borrar 'key' con
 * explicit_bzero() o equivalente.
 */
void rc4_init(RC4Context *ctx, const uint8_t *key, size_t key_len);

/*
 * rc4_crypt: cifra o descifra datos en memoria (in-place o separado).
 *
 * Como RC4 es XOR con keystream, encrypt == decrypt.
 * in      - buffer de entrada
 * out     - buffer de salida (puede ser igual a in para in-place)
 * len     - número de bytes a procesar
 * ctx     - contexto inicializado con rc4_init
 */
void rc4_crypt(RC4Context *ctx, const uint8_t *in, uint8_t *out, size_t len);

/*
 * rc4_context_destroy: borra de forma segura el S-box y los índices.
 *
 * Usa explicit_bzero para evitar que el compilador optimice el borrado.
 * Previene "Memory Scraping" si el proceso hace un core dump.
 */
void rc4_context_destroy(RC4Context *ctx);

#endif /* RC4_H */
