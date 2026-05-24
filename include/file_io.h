#ifndef FILE_IO_H
#define FILE_IO_H

#include <stddef.h>

/* Leer archivo completo en memoria */
int file_read_raw(const char *filepath, char **out, size_t *out_size);

/* Escribir buffer a archivo */
void file_write_raw(const char *path, char *data, size_t len);

#endif
