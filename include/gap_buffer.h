#ifndef GAP_BUFFER_H
#define GAP_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/* Tamaño de página para alineación de buffers */
#define PAGE_SIZE_BUF    4096
#define INITIAL_GAP_SIZE 8192

typedef struct {
    char   *buffer;      /* Buffer circular con gap */
    size_t  gap_start;   /* Posición del cursor (inicio del gap) */
    size_t  gap_end;     /* Fin del gap */
    size_t  buf_size;    /* Tamaño total del buffer */
} GapBuffer;

/* Crear nuevo gap buffer */
GapBuffer* gap_buffer_create(size_t initial_size);

/* Cargar texto en el buffer */
void gap_buffer_load(GapBuffer *gb, char *data, size_t len);

/* Extraer texto del buffer */
void gap_buffer_extract(GapBuffer *gb, char *out, size_t out_size);

/* Obtener longitud del texto */
size_t gap_buffer_length(const GapBuffer *gb);

/* Insertar carácter en la posición del cursor */
void gap_buffer_insert(GapBuffer *gb, char c);

/* Borrar carácter anterior al cursor (backspace) */
void gap_buffer_delete(GapBuffer *gb);

/* Mover cursor a la izquierda */
void gap_buffer_move_left(GapBuffer *gb);

/* Mover cursor a la derecha */
void gap_buffer_move_right(GapBuffer *gb);

/* Liberar memoria del gap buffer */
void gap_buffer_free(GapBuffer *gb);

#endif
