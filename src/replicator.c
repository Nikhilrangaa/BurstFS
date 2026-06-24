#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include "replicator.h"
#include "journal.h"
#include "pathing.h"
#include "util.h"

#define MAX_QUEUE 1024
#define MAX_PATH_LEN 4096
#define COMMIT_MARKER ".aifs_committed"

typedef struct {
    char path[MAX_PATH_LEN];
} repl_job_t;

extern int journal_all_remote_durable_under_root(const char *root_path);

static repl_job_t g_queue[MAX_QUEUE];
static int g_head = 0, g_tail = 0;
static int g_running = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;

static int queue_empty(void) {
    return g_head == g_tail;
}

static int queue_full(void) {
    return ((g_tail + 1) % MAX_QUEUE) == g_head;
}

static int build_tmp_file_path(const char *dst, char *tmpdst, size_t tmpdst_sz) {
    int n = snprintf(tmpdst, tmpdst_sz, "%s.tmp", dst);
    if (n < 0 || (size_t)n >= tmpdst_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int build_commit_marker_path(const char *lower_root, char *marker, size_t marker_sz) {
    int n = snprintf(marker, marker_sz, "%s/%s", lower_root, COMMIT_MARKER);
    if (n < 0 || (size_t)n >= marker_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
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

static int fsync_parent_dir(const char *path) {
    char tmp[PATH_MAX];
    char *slash;
    int fd;

    snprintf(tmp, sizeof(tmp), "%s", path);
    slash = strrchr(tmp, '/');
    if (!slash) return 0;

    if (slash == tmp) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    fd = open(tmp, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    if (fsync(fd) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int copy_file_atomic(const char *src, const char *dst) {
    char tmpdst[PATH_MAX];
    if (build_tmp_file_path(dst, tmpdst, sizeof(tmpdst)) != 0) return -1;
    if (ensure_parent_dir(tmpdst) != 0) return -1;
    if (copy_file(src, tmpdst) != 0) {
        unlink(tmpdst);
        return -1;
    }
    if (rename(tmpdst, dst) != 0) {
        unlink(tmpdst);
        return -1;
    }
    if (fsync_parent_dir(dst) != 0) {
        return -1;
    }
    return 0;
}

static int write_commit_marker_for_root(const char *root_path) {
    char lower_root[PATH_MAX];
    char marker[PATH_MAX];
    int fd;

    make_lower_path(root_path, lower_root, sizeof(lower_root));
    if (mkdir_p(lower_root) != 0) return -1;
    if (build_commit_marker_path(lower_root, marker, sizeof(marker)) != 0) return -1;

    fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    if (write(fd, "committed\n", 10) < 0) {
        close(fd);
        return -1;
    }
    if (fsync(fd) != 0) {
        close(fd);
        return -1;
    }
    close(fd);

    if (fsync_parent_dir(marker) != 0) return -1;
    return 0;
}

static void maybe_commit_checkpoint_root(const char *file_path) {
    char root[MAX_PATH_LEN];

    if (checkpoint_root_for_path(file_path, root, sizeof(root)) != 0) {
        return;
    }
    if (strcmp(root, "/") == 0) {
        return;
    }

    if (journal_all_remote_durable_under_root(root)) {
        if (write_commit_marker_for_root(root) != 0) {
            fprintf(stderr, "failed to write commit marker for root %s: %s\n",
                    root, strerror(errno));
        }
    }
}

static void *repl_worker(void *arg) {
    (void)arg;
    while (1) {
        repl_job_t job;
        char src[PATH_MAX];
        char dst[PATH_MAX];

        pthread_mutex_lock(&g_lock);
        while (queue_empty() && g_running) {
            pthread_cond_wait(&g_cv, &g_lock);
        }
        if (!g_running && queue_empty()) {
            pthread_mutex_unlock(&g_lock);
            break;
        }
        job = g_queue[g_head];
        g_head = (g_head + 1) % MAX_QUEUE;
        pthread_mutex_unlock(&g_lock);

        journal_mark_replicating(job.path);
        make_spool_path(job.path, src, sizeof(src));
        make_lower_path(job.path, dst, sizeof(dst));

        if (copy_file_atomic(src, dst) != 0) {
            journal_mark_failed(job.path);
            continue;
        }

        journal_mark_remote_durable(job.path);
        maybe_commit_checkpoint_root(job.path);
    }
    return NULL;
}

int replicator_start(void) {
    g_running = 1;
    return pthread_create(&g_thread, NULL, repl_worker, NULL);
}

void replicator_stop(void) {
    pthread_mutex_lock(&g_lock);
    g_running = 0;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_thread, NULL);
}

int replicator_enqueue(const char *virtual_path) {
    pthread_mutex_lock(&g_lock);
    if (queue_full()) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    snprintf(g_queue[g_tail].path, sizeof(g_queue[g_tail].path), "%s", virtual_path);
    g_tail = (g_tail + 1) % MAX_QUEUE;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

