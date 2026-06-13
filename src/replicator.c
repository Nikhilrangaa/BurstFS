#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>

#include "replicator.h"
#include "journal.h"
#include "pathing.h"
#include "util.h"

#define MAX_QUEUE 1024
#define MAX_PATH_LEN 4096

typedef struct {
    char path[MAX_PATH_LEN];
} repl_job_t;

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

        if (ensure_parent_dir(dst) != 0 || copy_file(src, dst) != 0) {
            journal_mark_failed(job.path);
            continue;
        }

        journal_mark_remote_durable(job.path);
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
