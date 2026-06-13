#ifndef JOURNAL_H
#define JOURNAL_H

#include <stddef.h>

typedef enum {
    CKPT_PENDING = 0,
    CKPT_LOCAL_DURABLE,
    CKPT_REPLICATING,
    CKPT_REMOTE_DURABLE,
    CKPT_FAILED
} ckpt_state_t;

int journal_init(void);
void journal_mark_pending(const char *path);
void journal_mark_local_durable(const char *path, size_t size);
void journal_mark_replicating(const char *path);
void journal_mark_remote_durable(const char *path);
void journal_mark_failed(const char *path);
ckpt_state_t journal_get_state(const char *path);

#endif
