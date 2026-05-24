#ifndef LZ77_H
#define LZ77_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t tokens_generated;    /* Total de tokens */
    size_t literal_tokens;      /* Tokens literales */
    size_t match_tokens;        /* Tokens de match */
} LZ77Stats;

/* Comprimir con LZ77 */
int lz77_compress(uint8_t *in, size_t in_len,
                  uint8_t **out, size_t *out_len, LZ77Stats *stats);

/* Descomprimir con LZ77 */
int lz77_decompress(uint8_t *in, size_t in_len,
                    uint8_t **out, size_t *out_len);

#endif
