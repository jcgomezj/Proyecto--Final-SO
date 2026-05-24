/*
 * rc4.c — Implementación propia del cifrado de flujo RC4
 *
 * Referencia: RFC 4345 / descripción original de Ron Rivest (1987)
 *
 * Pipeline de seguridad implementado:
 *   1. KSA  (Key Scheduling Algorithm) → genera S-box permutada
 *   2. PRGA (Pseudo-Random Generation Algorithm) → genera keystream
 *   3. XOR del keystream con los datos → cifrado/descifrado
 *
 * Orden arquitectónico correcto en el editor:
 *   texto_plano → [COMPRESS] → compressed_buf → [RC4 ENCRYPT] → disco
 *
 * Por qué comprimir ANTES de cifrar:
 *   La encriptación RC4 (y cualquier cifrado moderno) genera una salida
 *   con distribución de bytes pseudoaleatoria (alta entropía ≈ 8 bits/byte).
 *   Los algoritmos de compresión buscan PATRONES y REDUNDANCIA. Al cifrar
 *   primero, destruimos todos los patrones → la compresión posterior es
 *   inútil y puede incluso aumentar el tamaño del archivo.
 */

#include "rc4.h"
#include <string.h>    /* explicit_bzero (glibc >= 2.25) */
#include <strings.h>  /* explicit_bzero alternativo en algunos BSD */

/* ─── KSA: Key Scheduling Algorithm ─────────────────────────── */

void rc4_init(RC4Context *ctx, const uint8_t *key, size_t key_len)
{
    uint8_t *S = ctx->S;
    size_t   i, j;

    /* Paso 1: inicializar S-box con identidad (0..255) */
    for (i = 0; i < RC4_SBOX_SIZE; i++)
        S[i] = (uint8_t)i;

    /* Paso 2: KSA — mezclar el S-box con la llave */
    j = 0;
    for (i = 0; i < RC4_SBOX_SIZE; i++) {
        /*
         * j = (j + S[i] + key[i % key_len]) mod 256
         * El cast a uint8_t hace la aritmetica modular automaticamente.
         */
        j = (j + S[i] + key[i % key_len]) & 0xFF;

        /* Swap S[i] y S[j] */
        uint8_t tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;
    }

    /* Inicializar índices PRGA */
    ctx->i = 0;
    ctx->j = 0;
}

/* ─── PRGA: Pseudo-Random Generation Algorithm + XOR ───────── */

void rc4_crypt(RC4Context *ctx, const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t *S = ctx->S;
    uint8_t  i = ctx->i;
    uint8_t  j = ctx->j;

    for (size_t k = 0; k < len; k++) {
        /*
         * Generar siguiente byte del keystream:
         *   i = (i + 1) mod 256
         *   j = (j + S[i]) mod 256
         *   swap(S[i], S[j])
         *   keystream_byte = S[(S[i] + S[j]) mod 256]
         */
        i = (uint8_t)(i + 1);
        j = (uint8_t)(j + S[i]);

        uint8_t tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;

        uint8_t ks_byte = S[(uint8_t)(S[i] + S[j])];

        /* XOR → misma operación para cifrar y descifrar */
        out[k] = in[k] ^ ks_byte;
    }

    /* Guardar estado PRGA para uso incremental (si fuera necesario) */
    ctx->i = i;
    ctx->j = j;
}

/* ─── Destrucción segura del contexto ────────────────────────── */

void rc4_context_destroy(RC4Context *ctx)
{
    /*
     * explicit_bzero garantiza que el compilador NO optimice el borrado.
     * Un memset() normal puede ser eliminado por el compilador si detecta
     * que el buffer no se usa después (Dead Store Elimination).
     *
     * Esto borra el S-box completo (256 bytes) + índices i, j.
     * Cualquier página de swap que contenga este contexto quedará
     * con ceros si el SO la reclama, aunque idealmente se usa mlock().
     */
    explicit_bzero(ctx, sizeof(RC4Context));
}
