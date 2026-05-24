#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int file_read_raw(const char *filepath, char **out, size_t *out_size)
{
    if (!filepath || !out || !out_size) return -1;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

    size_t size = (size_t)st.st_size;
    char *buf = malloc(size + 1);
    if (!buf) {
        close(fd);
        return -1;
    }

    ssize_t n = read(fd, buf, size);
    close(fd);

    if (n != (ssize_t)size) {
        free(buf);
        return -1;
    }

    buf[size] = '\0';
    *out = buf;
    *out_size = size;
    return 0;
}

void file_write_raw(const char *path, char *data, size_t len)
{
    if (!path || !data) return;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    write(fd, data, len);
    close(fd);
}
