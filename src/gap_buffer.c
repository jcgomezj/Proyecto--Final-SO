#include "gap_buffer.h"
#include <stdlib.h>
#include <string.h>

/* Asegura que el gap tenga espacio disponible */
static void ensure_gap_space(GapBuffer *gb, size_t needed)
{
    size_t gap_size = gb->gap_end - gb->gap_start;
    if (gap_size >= needed) return;

    /* Expandir buffer */
    size_t new_size = gb->buf_size * 2;
    if (new_size < gb->buf_size + needed) new_size = gb->buf_size + needed;
    
    char *new_buf = malloc(new_size);
    if (!new_buf) return;

    /* Copiar contenido antes del gap */
    memcpy(new_buf, gb->buffer, gb->gap_start);
    
    /* Copiar contenido después del gap */
    size_t after_len = gb->buf_size - gb->gap_end;
    memcpy(new_buf + new_size - after_len, gb->buffer + gb->gap_end, after_len);

    free(gb->buffer);
    gb->buffer = new_buf;
    gb->gap_end = new_size - after_len;
    gb->buf_size = new_size;
}

GapBuffer* gap_buffer_create(size_t initial_size)
{
    GapBuffer *gb = malloc(sizeof(GapBuffer));
    if (!gb) return NULL;

    gb->buffer = malloc(initial_size);
    if (!gb->buffer) {
        free(gb);
        return NULL;
    }

    gb->gap_start = 0;
    gb->gap_end = initial_size;
    gb->buf_size = initial_size;
    return gb;
}

void gap_buffer_load(GapBuffer *gb, char *data, size_t len)
{
    if (!gb || !data) return;

    ensure_gap_space(gb, len);
    
    /* Copiar datos al inicio del buffer */
    if (len > 0) {
        memcpy(gb->buffer, data, len);
    }
    gb->gap_start = len;
    /* gap_end permanece igual para máximo espacio disponible */
}

void gap_buffer_extract(GapBuffer *gb, char *out, size_t out_size)
{
    if (!gb || !out || out_size == 0) return;

    size_t before_len = gb->gap_start;
    size_t after_len = gb->buf_size - gb->gap_end;
    size_t total_len = before_len + after_len;
    
    if (total_len >= out_size) total_len = out_size - 1;

    /* Copiar antes del gap */
    if (before_len > 0 && before_len <= total_len) {
        memcpy(out, gb->buffer, before_len);
    }

    /* Copiar después del gap */
    size_t copy_after = (total_len > before_len) ? (total_len - before_len) : 0;
    if (copy_after > 0) {
        memcpy(out + before_len, gb->buffer + gb->gap_end, copy_after);
    }

    out[total_len] = '\0';
}

size_t gap_buffer_length(const GapBuffer *gb)
{
    if (!gb) return 0;
    return gb->gap_start + (gb->buf_size - gb->gap_end);
}

void gap_buffer_insert(GapBuffer *gb, char c)
{
    if (!gb) return;

    ensure_gap_space(gb, 1);
    
    gb->buffer[gb->gap_start] = c;
    gb->gap_start++;
}

void gap_buffer_delete(GapBuffer *gb)
{
    if (!gb || gb->gap_start == 0) return;
    gb->gap_start--;
}

void gap_buffer_move_left(GapBuffer *gb)
{
    if (!gb || gb->gap_start == 0) return;

    gb->gap_start--;
    /* Mover carácter desde gap_end a gap_start */
    gb->buffer[gb->gap_end - 1] = gb->buffer[gb->gap_start];
    gb->gap_end--;
}

void gap_buffer_move_right(GapBuffer *gb)
{
    if (!gb || gb->gap_end >= gb->buf_size) return;

    /* Mover carácter desde gap_end al gap_start */
    gb->buffer[gb->gap_start] = gb->buffer[gb->gap_end];
    gb->gap_start++;
    gb->gap_end++;
}

void gap_buffer_free(GapBuffer *gb)
{
    if (!gb) return;
    free(gb->buffer);
    free(gb);
}
