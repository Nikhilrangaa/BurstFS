#define FUSE_USE_VERSION 31
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <fuse3/fuse.h>
#include "config.h"
#include "journal.h"
#include "replicator.h"
#include "fs.h"

ckptfs_config_t g_cfg;

static int ensure_dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int build_orphan_spool_path(const char *spool_root, pid_t pid, char *out, size_t out_sz) {
    int n;
    n = snprintf(out, out_sz, "%s.orphan.%ld", spool_root, (long)pid);
    if (n < 0 || (size_t)n >= out_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/*
 * Conservative crash-recovery rule:
 * - Never trust leftover NVMe/spool data after a process/VM crash.
 * - Quarantine the spool root on startup so only lower_root (persistent store)
 *   determines the restart-visible checkpoint set.
 */
static int quarantine_spool_root(void) {
    struct stat st;
    char quarantine[PATH_MAX];

    if (lstat(g_cfg.spool_root, &st) != 0) {
        if (errno == ENOENT) {
            return ensure_dir_exists(g_cfg.spool_root);
        }
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (build_orphan_spool_path(g_cfg.spool_root, getpid(), quarantine, sizeof(quarantine)) != 0) {
        return -1;
    }
    if (rename(g_cfg.spool_root, quarantine) != 0) {
        return -1;
    }
    return ensure_dir_exists(g_cfg.spool_root);
}

int config_init_from_args(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <mountpoint> <lower_root> <spool_root>\n", argv[0]);
        return -1;
    }
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.mountpoint, sizeof(g_cfg.mountpoint), "%s", argv[1]);
    snprintf(g_cfg.lower_root, sizeof(g_cfg.lower_root), "%s", argv[2]);
    snprintf(g_cfg.spool_root, sizeof(g_cfg.spool_root), "%s", argv[3]);
    snprintf(g_cfg.checkpoint_prefix, sizeof(g_cfg.checkpoint_prefix), "%s", "/");
    g_cfg.repl_mode = REPL_ASYNC_TO_LOWER;
    return 0;
}

int main(int argc, char **argv) {
    if (config_init_from_args(argc, argv) != 0) {
        return 1;
    }
    if (ensure_dir_exists(g_cfg.lower_root) != 0) {
        perror("ensure lower_root");
        return 1;
    }
    if (quarantine_spool_root() != 0) {
        perror("quarantine_spool_root");
        return 1;
    }
    if (journal_init() != 0) {
        fprintf(stderr, "journal_init failed\n");
        return 1;
    }
    if (replicator_start() != 0) {
        fprintf(stderr, "replicator_start failed\n");
        return 1;
    }
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    fuse_opt_add_arg(&args, argv[0]);
    fuse_opt_add_arg(&args, g_cfg.mountpoint);
    fuse_opt_add_arg(&args, "-f");
    int ret = fuse_main(args.argc, args.argv, &ckptfs_ops, NULL);
    replicator_stop();
    fuse_opt_free_args(&args);
    return ret;
}

