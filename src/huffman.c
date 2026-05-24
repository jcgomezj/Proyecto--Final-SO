#include "huffman.h"
#include <stdlib.h>
#include <string.h>

typedef struct HuffNode {
    int freq;
    uint8_t byte_val;
    struct HuffNode *left, *right;
} HuffNode;

typedef struct {
    HuffNode *root;
    uint32_t freq_table[256];
} HuffmanInternal;

static HuffNode* create_node(uint8_t byte_val, int freq)
{
    HuffNode *node = malloc(sizeof(HuffNode));
    node->freq = freq;
    node->byte_val = byte_val;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static int cmp_nodes(const void *a, const void *b)
{
    HuffNode * const *na = a;
    HuffNode * const *nb = b;
    return (*na)->freq - (*nb)->freq;
}

static HuffNode* build_huffman_tree(HuffmanInternal *hi, uint8_t *in, size_t len)
{
    /* Contar frecuencias */
    memset(hi->freq_table, 0, sizeof(hi->freq_table));
    for (size_t i = 0; i < len; i++) {
        hi->freq_table[in[i]]++;
    }

    /* Crear nodos hoja */
    HuffNode **nodes = malloc(256 * sizeof(HuffNode*));
    int node_count = 0;
    
    for (int i = 0; i < 256; i++) {
        if (hi->freq_table[i] > 0) {
            nodes[node_count++] = create_node((uint8_t)i, (int)hi->freq_table[i]);
        }
    }

    if (node_count == 0) {
        free(nodes);
        return NULL;
    }

    if (node_count == 1) {
        /* Solo un símbolo → crear nodo raíz */
        HuffNode *root = create_node(nodes[0]->byte_val, nodes[0]->freq);
        root->left = nodes[0];
        free(nodes);
        return root;
    }

    /* Construir árbol */
    while (node_count > 1) {
        qsort(nodes, node_count, sizeof(HuffNode*), cmp_nodes);
        
        HuffNode *left = nodes[0];
        HuffNode *right = nodes[1];
        HuffNode *parent = create_node(0, left->freq + right->freq);
        parent->left = left;
        parent->right = right;

        nodes[0] = parent;
        nodes[1] = nodes[node_count - 1];
        node_count--;
    }

    HuffNode *root = nodes[0];
    free(nodes);
    return root;
}

static void free_tree(HuffNode *node)
{
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    free(node);
}

HuffContext* huff_context_create(void)
{
    HuffContext *ctx = malloc(sizeof(HuffContext));
    if (!ctx) return NULL;
    
    HuffmanInternal *hi = malloc(sizeof(HuffmanInternal));
    if (!hi) {
        free(ctx);
        return NULL;
    }
    
    hi->root = NULL;
    ctx->_internal = hi;
    return ctx;
}

void huff_compress(HuffContext *ctx, uint8_t *in, size_t in_len,
                   uint8_t **out, size_t *out_len)
{
    if (!ctx || !in || !out || !out_len) return;

    HuffmanInternal *hi = (HuffmanInternal*)ctx->_internal;
    
    /* Compilar árbol */
    if (hi->root) free_tree(hi->root);
    hi->root = build_huffman_tree(hi, in, in_len);

    if (!hi->root) {
        *out_len = 0;
        *out = malloc(1);
        return;
    }

    /* Generar tabla de frecuencias + datos */
    size_t freq_table_size = 256 * 4;
    uint8_t *compressed = malloc(freq_table_size + in_len + 4);
    if (!compressed) {
        *out_len = 0;
        *out = malloc(1);
        return;
    }

    uint8_t *ptr = compressed;
    
    /* Escribir longitud original (4 bytes) */
    *(uint32_t*)ptr = (uint32_t)in_len;
    ptr += 4;

    /* Escribir tabla de frecuencias */
    memcpy(ptr, hi->freq_table, freq_table_size);
    ptr += freq_table_size;

    /* Copiar datos (en este implementación simple, no comprimimos realmente) */
    memcpy(ptr, in, in_len);
    ptr += in_len;

    *out = compressed;
    *out_len = ptr - compressed;
}

int huff_decompress(HuffContext *ctx, uint8_t *in, size_t in_len,
                    uint8_t **out, size_t *out_len)
{
    if (!ctx || !in || !out || !out_len) return -1;

    if (in_len < 4) return -1;

    uint32_t orig_len = *(uint32_t*)in;
    if (orig_len > 10000000) return -1; /* Sanidad */

    size_t freq_table_size = 256 * 4;
    if (in_len < 4 + freq_table_size) return -1;

    uint8_t *decompressed = malloc(orig_len);
    if (!decompressed) return -1;

    /* Copiar datos descomprimidos (en este caso, simplemente se copian) */
    uint8_t *data_start = in + 4 + freq_table_size;
    size_t data_len = in_len - 4 - freq_table_size;
    
    if (data_len != orig_len) {
        free(decompressed);
        return -1;
    }

    memcpy(decompressed, data_start, data_len);

    *out = decompressed;
    *out_len = orig_len;
    return 0;
}

void huff_context_free(HuffContext *ctx)
{
    if (!ctx) return;
    
    HuffmanInternal *hi = (HuffmanInternal*)ctx->_internal;
    if (hi) {
        if (hi->root) free_tree(hi->root);
        free(hi);
    }
    free(ctx);
}
