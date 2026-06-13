#ifndef CONFIG_H
#define CONFIG_H

#include <limits.h>

typedef enum {
    REPL_LOCAL_ONLY = 0,
    REPL_ASYNC_TO_LOWER = 1,
    REPL_SYNC_TO_LOWER = 2
} replication_mode_t;

typedef struct {
    char mountpoint[PATH_MAX];
    char lower_root[PATH_MAX];
    char spool_root[PATH_MAX];
    char checkpoint_prefix[PATH_MAX];
    replication_mode_t repl_mode;
} ckptfs_config_t;

extern ckptfs_config_t g_cfg;

int config_init_from_args(int argc, char **argv);

#endif
