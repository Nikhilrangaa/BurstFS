#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include "journal.h"

#define MAX_JOURNAL_ENTRIES 4096
#define MAX_PATH_LEN 4096

typedef struct {
    char path[MAX_PATH_LEN];
    ckpt_state_t state;
    size_t size;
    int used;
} journal_entry_t;

static journal_entry_t g_entries[MAX_JOURNAL_ENTRIES];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static journal_entry_t *find_or_create(const char *path) {
    int i;
    journal_entry_t *free_slot = NULL;

    for (i = 0; i < MAX_JOURNAL_ENTRIES; i++) {
        if (g_entries[i].used && strcmp(g_entries[i].path, path) == 0) {
            return &g_entries[i];
        }
        if (!g_entries[i].used && free_slot == NULL) {
            free_slot = &g_entries[i];
        }
    }

    if (free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        snprintf(free_slot->path, sizeof(free_slot->path), "%s", path);
        free_slot->used = 1;
        return free_slot;
    }

    return NULL;
}

static int checkpoint_root_for_path(const char *path, char *out, size_t out_sz) {
    const char *slash2;
    size_t len;

    if (!path || path[0] != '/') {
        if (out_sz > 0) {
            out[0] = '/';
            if (out_sz > 1) out[1] = '\0';
        }
        return 0;
    }

    slash2 = strchr(path + 1, '/');
    if (!slash2) {
        len = strlen(path);
        if (len + 1 > out_sz) {
            errno = ENAMETOOLONG;
            if (out_sz) out[0] = '\0';
            return -1;
        }
        memcpy(out, path, len + 1);
        return 0;
    }

    len = (size_t)(slash2 - path);
    if (len + 1 > out_sz) {
        errno = ENAMETOOLONG;
        if (out_sz) out[0] = '\0';
        return -1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

int journal_init(void) {
    memset(g_entries, 0, sizeof(g_entries));
    return 0;
}

void journal_mark_pending(const char *path) {
    pthread_mutex_lock(&g_lock);
    journal_entry_t *e = find_or_create(path);
    if (e) e->state = CKPT_PENDING;
    pthread_mutex_unlock(&g_lock);
}

void journal_mark_local_durable(const char *path, size_t size) {
    pthread_mutex_lock(&g_lock);
    journal_entry_t *e = find_or_create(path);
    if (e) {
        e->state = CKPT_LOCAL_DURABLE;
        e->size = size;
    }
    pthread_mutex_unlock(&g_lock);
}

void journal_mark_replicating(const char *path) {
    pthread_mutex_lock(&g_lock);
    journal_entry_t *e = find_or_create(path);
    if (e) e->state = CKPT_REPLICATING;
    pthread_mutex_unlock(&g_lock);
}

void journal_mark_remote_durable(const char *path) {
    pthread_mutex_lock(&g_lock);
    journal_entry_t *e = find_or_create(path);
    if (e) e->state = CKPT_REMOTE_DURABLE;
    pthread_mutex_unlock(&g_lock);
}

void journal_mark_failed(const char *path) {
    pthread_mutex_lock(&g_lock);
    journal_entry_t *e = find_or_create(path);
    if (e) e->state = CKPT_FAILED;
    pthread_mutex_unlock(&g_lock);
}

ckpt_state_t journal_get_state(const char *path) {
    int i;
    ckpt_state_t state = CKPT_FAILED;

    pthread_mutex_lock(&g_lock);
    for (i = 0; i < MAX_JOURNAL_ENTRIES; i++) {
        if (g_entries[i].used && strcmp(g_entries[i].path, path) == 0) {
            state = g_entries[i].state;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);

    return state;
}

int journal_all_remote_durable_under_root(const char *root_path) {
    int i;
    int found_any = 0;

    pthread_mutex_lock(&g_lock);

    for (i = 0; i < MAX_JOURNAL_ENTRIES; i++) {
        char this_root[MAX_PATH_LEN];

        if (!g_entries[i].used) {
            continue;
        }
        if (checkpoint_root_for_path(g_entries[i].path, this_root, sizeof(this_root)) != 0) {
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
        if (strcmp(this_root, root_path) != 0) {
            continue;
        }

        found_any = 1;
        if (g_entries[i].state != CKPT_REMOTE_DURABLE) {
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_lock);
    return found_any ? 1 : 0;
}
