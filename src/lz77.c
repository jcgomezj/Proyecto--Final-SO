#include "lz77.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define WINDOW_SIZE     32768   /* Tamaño de la ventana deslizante */
#define MAX_MATCH_LEN   258     /* Longitud máxima de match */
#define MIN_MATCH_LEN   3       /* Longitud mínima para match */

/* Token LZ77: (tipo, offset/literal, length) */
typedef struct {
    uint8_t type;       /* 0=literal, 1=match */
    uint16_t offset;    /* Offset en ventana (si match) */
    uint16_t length;    /* Longitud (literal o match) */
    uint8_t literal;    /* Valor literal (si type==0) */
} LZ77Token;

static int find_best_match(const uint8_t *data, size_t pos, size_t len,
                           uint16_t *out_offset, uint16_t *out_length)
{
    size_t window_start = (pos > WINDOW_SIZE) ? (pos - WINDOW_SIZE) : 0;
    int best_len = 0;
    size_t best_pos = 0;

    /* Buscar mejor match */
    for (size_t i = window_start; i < pos; i++) {
        int match_len = 0;
        while (match_len < MAX_MATCH_LEN && 
               pos + match_len < len && 
               data[i + match_len] == data[pos + match_len]) {
            match_len++;
        }

        if (match_len >= MIN_MATCH_LEN && match_len > best_len) {
            best_len = match_len;
            best_pos = i;
        }
    }

    if (best_len >= MIN_MATCH_LEN) {
        *out_offset = (uint16_t)(pos - best_pos);
        *out_length = (uint16_t)best_len;
        return 1;
    }

    return 0;
}

int lz77_compress(uint8_t *in, size_t in_len,
                  uint8_t **out, size_t *out_len, LZ77Stats *stats)
{
    if (!in || !out || !out_len || !stats) return -1;

    LZ77Token *tokens = malloc((in_len + 1) * sizeof(LZ77Token));
    if (!tokens) return -1;

    memset(stats, 0, sizeof(LZ77Stats));
    size_t token_count = 0;
    size_t pos = 0;

    /* Comprimir */
    while (pos < in_len) {
        uint16_t offset = 0, length = 0;

        if (find_best_match(in, pos, in_len, &offset, &length)) {
            /* Match encontrado */
            tokens[token_count].type = 1;
            tokens[token_count].offset = offset;
            tokens[token_count].length = length;
            tokens[token_count].literal = 0;
            pos += length;
            stats->match_tokens++;
        } else {
            /* Literal */
            tokens[token_count].type = 0;
            tokens[token_count].literal = in[pos];
            tokens[token_count].offset = 0;
            tokens[token_count].length = 1;
            pos++;
            stats->literal_tokens++;
        }

        token_count++;
        stats->tokens_generated++;
    }

    /* Serializar tokens */
    uint8_t *compressed = malloc(token_count * 5 + 4);
    if (!compressed) {
        free(tokens);
        return -1;
    }

    uint8_t *ptr = compressed;

    /* Escribir longitud original */
    *(uint32_t*)ptr = (uint32_t)in_len;
    ptr += 4;

    /* Escribir tokens */
    for (size_t i = 0; i < token_count; i++) {
        *ptr++ = tokens[i].type;
        
        if (tokens[i].type == 0) {
            /* Literal */
            *ptr++ = tokens[i].literal;
            *ptr++ = 0;
            *ptr++ = 0;
            *ptr++ = 0;
        } else {
            /* Match */
            *(uint16_t*)ptr = tokens[i].offset;
            ptr += 2;
            *(uint16_t*)ptr = tokens[i].length;
            ptr += 2;
        }
    }

    free(tokens);

    *out = compressed;
    *out_len = ptr - compressed;
    return 0;
}

int lz77_decompress(uint8_t *in, size_t in_len,
                    uint8_t **out, size_t *out_len)
{
    if (!in || !out || !out_len) return -1;

    if (in_len < 4) return -1;

    uint32_t orig_len = *(uint32_t*)in;
    if (orig_len > 10000000) return -1; /* Sanidad */

    uint8_t *decompressed = malloc(orig_len);
    if (!decompressed) return -1;

    uint8_t *ptr = in + 4;
    uint8_t *end = in + in_len;
    size_t out_pos = 0;

    /* Descomprimir tokens */
    while (ptr < end && out_pos < orig_len) {
        uint8_t type = *ptr++;
        
        if (type == 0) {
            /* Literal */
            if (ptr + 4 > end) break;
            uint8_t literal = *ptr++;
            ptr += 3; /* Saltar padding */
            
            if (out_pos < orig_len) {
                decompressed[out_pos++] = literal;
            }
        } else {
            /* Match */
            if (ptr + 4 > end) break;
            uint16_t offset = *(uint16_t*)ptr;
            ptr += 2;
            uint16_t length = *(uint16_t*)ptr;
            ptr += 2;

            /* Copiar desde la ventana */
            if (offset > 0 && offset <= out_pos) {
                for (int i = 0; i < length && out_pos < orig_len; i++) {
                    size_t src_pos = out_pos - offset;
                    decompressed[out_pos++] = decompressed[src_pos];
                }
            }
        }
    }

    if (out_pos != orig_len) {
        free(decompressed);
        return -1;
    }

    *out = decompressed;
    *out_len = orig_len;
    return 0;
}
