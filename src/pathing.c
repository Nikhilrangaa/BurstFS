#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#include "config.h"
#include "pathing.h"

extern ckptfs_config_t g_cfg;

int is_checkpoint_path(const char *path) {
    (void)path;
    return 1; /* MVP: entire mount is treated as checkpoint namespace */
}

void make_lower_path(const char *path, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s%s", g_cfg.lower_root, path);
}

void make_spool_path(const char *path, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s%s", g_cfg.spool_root, path);
}

int visible_path(const char *path, char *out, size_t out_sz) {
    char spool[PATH_MAX];
    char lower[PATH_MAX];
    struct stat st;

    make_spool_path(path, spool, sizeof(spool));
    if (lstat(spool, &st) == 0) {
        snprintf(out, out_sz, "%s", spool);
        return 0;
    }

    make_lower_path(path, lower, sizeof(lower));
    if (lstat(lower, &st) == 0) {
        snprintf(out, out_sz, "%s", lower);
        return 0;
    }

    return -1;
}
