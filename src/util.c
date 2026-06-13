#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <limits.h>

#include "util.h"

int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int ensure_parent_dir(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    return mkdir_p(dirname(tmp));
}

int copy_file(const char *src, const char *dst) {
    int in_fd, out_fd;
    ssize_t n;
    char buf[1024 * 1024];

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) return -1;

    out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        char *p = buf;
        ssize_t remaining = n;
        while (remaining > 0) {
            ssize_t written = write(out_fd, p, remaining);
            if (written < 0) {
                close(in_fd);
                close(out_fd);
                return -1;
            }
            remaining -= written;
            p += written;
        }
    }

    fsync(out_fd);
    close(in_fd);
    close(out_fd);
    return (n < 0) ? -1 : 0;
}
