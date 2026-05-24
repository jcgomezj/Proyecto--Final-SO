#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    /* Contexto interno para compresión Huffman */
    void *_internal;
} HuffContext;

/* Crear contexto Huffman */
HuffContext* huff_context_create(void);

/* Comprimir datos con Huffman */
void huff_compress(HuffContext *ctx, uint8_t *in, size_t in_len,
                   uint8_t **out, size_t *out_len);

/* Descomprimir datos con Huffman */
int huff_decompress(HuffContext *ctx, uint8_t *in, size_t in_len,
                    uint8_t **out, size_t *out_len);

/* Liberar contexto Huffman */
void huff_context_free(HuffContext *ctx);

#endif
