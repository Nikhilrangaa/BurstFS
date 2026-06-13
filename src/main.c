#define FUSE_USE_VERSION 31

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse3/fuse.h>

#include "config.h"
#include "journal.h"
#include "replicator.h"
#include "fs.h"

ckptfs_config_t g_cfg;

int config_init_from_args(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <mountpoint> <lower_root> <spool_root>\n", argv[0]);
        return -1;
    }

    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.mountpoint, sizeof(g_cfg.mountpoint), "%s", argv[1]);
    snprintf(g_cfg.lower_root, sizeof(g_cfg.lower_root), "%s", argv[2]);
    snprintf(g_cfg.spool_root, sizeof(g_cfg.spool_root), "%s", argv[3]);
    snprintf(g_cfg.checkpoint_prefix, sizeof(g_cfg.checkpoint_prefix), "/");
    g_cfg.repl_mode = REPL_ASYNC_TO_LOWER;

    return 0;
}

int main(int argc, char **argv) {
    if (config_init_from_args(argc, argv) != 0) {
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
