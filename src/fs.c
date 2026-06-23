#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdint.h>
#include "fs.h"
#include "pathing.h"
#include "journal.h"
#include "replicator.h"
#include "util.h"

#define COMMIT_MARKER ".aifs_committed"

static void checkpoint_root_for_path(const char *path, char *out, size_t out_sz) {
    const char *slash2;
    if (!path || path[0] != '/') {
        snprintf(out, out_sz, "/");
        return;
    }
    slash2 = strchr(path + 1, '/');
    if (!slash2) {
        snprintf(out, out_sz, "%s", path);
        return;
    }
    snprintf(out, out_sz, "%.*s", (int)(slash2 - path), path);
}

static void remove_commit_marker_for_path(const char *path) {
    char root[PATH_MAX];
    char lower_root[PATH_MAX];
    char marker[PATH_MAX];
    checkpoint_root_for_path(path, root, sizeof(root));
    if (strcmp(root, "/") == 0) return;
    make_lower_path(root, lower_root, sizeof(lower_root));
    snprintf(marker, sizeof(marker), "%s/%s", lower_root, COMMIT_MARKER);
    unlink(marker); /* best effort: making checkpoint dirty */
}

static int is_commit_marker_name(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, COMMIT_MARKER) == 0;
}

static int ckptfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    char real[PATH_MAX];
    (void)fi;
    memset(stbuf, 0, sizeof(*stbuf));
    if (strcmp(path, "/") == 0) {
        char root[PATH_MAX];
        make_spool_path("/", root, sizeof(root));
        if (lstat(root, stbuf) == 0) return 0;
        make_lower_path("/", root, sizeof(root));
        if (lstat(root, stbuf) == 0) return 0;
        return -ENOENT;
    }
    if (visible_path(path, real, sizeof(real)) != 0)
        return -ENOENT;
    if (lstat(real, stbuf) != 0)
        return -errno;
    return 0;
}

static int ckptfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags) {
    DIR *dp;
    struct dirent *de;
    char real[PATH_MAX];
    (void)offset;
    (void)fi;
    (void)flags;
    if (strcmp(path, "/") == 0) {
        char spool_root[PATH_MAX];
        make_spool_path("/", spool_root, sizeof(spool_root));
        snprintf(real, sizeof(real), "%s", spool_root);
    } else if (visible_path(path, real, sizeof(real)) != 0) {
        return -ENOENT;
    }
    dp = opendir(real);
    if (!dp)
        return -errno;
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        if (strcmp(de->d_name, COMMIT_MARKER) == 0)
            continue; /* do not expose internal marker */
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        filler(buf, de->d_name, &st, 0, 0);
    }
    closedir(dp);
    return 0;
}

static int ckptfs_mkdir(const char *path, mode_t mode) {
    char spool[PATH_MAX];
    make_spool_path(path, spool, sizeof(spool));
    if (mkdir_p(spool) != 0)
        return -errno;
    chmod(spool, mode);
    return 0;
}

static int ckptfs_open(const char *path, struct fuse_file_info *fi) {
    char real[PATH_MAX];
    int fd;
    if (is_commit_marker_name(path))
        return -ENOENT;
    if (visible_path(path, real, sizeof(real)) != 0)
        return -ENOENT;
    fd = open(real, fi->flags);
    if (fd < 0)
        return -errno;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        remove_commit_marker_for_path(path);
        journal_mark_pending(path);
    }
    fi->fh = (uint64_t)fd;
    return 0;
}

static int ckptfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char spool[PATH_MAX];
    int fd;
    if (is_commit_marker_name(path))
        return -EPERM;
    make_spool_path(path, spool, sizeof(spool));
    if (ensure_parent_dir(spool) != 0)
        return -EIO;
    fd = open(spool, fi->flags | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -errno;
    fi->fh = (uint64_t)fd;
    remove_commit_marker_for_path(path);
    journal_mark_pending(path);
    return 0;
}

static int ckptfs_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
    int fd;
    ssize_t res;
    (void)path;
    fd = (int)fi->fh;
    res = pread(fd, buf, size, offset);
    if (res < 0)
        return -errno;
    return (int)res;
}

static int ckptfs_write(const char *path, const char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    int fd;
    ssize_t res;
    if (is_commit_marker_name(path))
        return -EPERM;
    fd = (int)fi->fh;
    res = pwrite(fd, buf, size, offset);
    if (res < 0)
        return -errno;
    return (int)res;
}

/*
 * Flush local spool contents to stable local media, mark journal durable,
 * and enqueue replication to backend.
 *
 * IMPORTANT:
 * - This does NOT wait for backend replication to finish.
 * - Backend durability is represented separately by the internal
 *   .aifs_committed marker created by the replicator after all files under a
 *   checkpoint root become REMOTE_DURABLE.
 */
static int ckptfs_flush_local_and_enqueue(const char *path, struct fuse_file_info *fi)
{
    struct stat st;
    int fd;
    if (!fi)
        return -EINVAL;
    if (is_commit_marker_name(path))
        return -EPERM;
    fd = (int)(uintptr_t)fi->fh;
    if (fd < 0)
        return -EBADF;
    if (fsync(fd) != 0) {
        perror("fsync(local spool) failed");
        return -errno;
    }
    if (fstat(fd, &st) == 0) {
        journal_mark_local_durable(path, (size_t)st.st_size);
    } else {
        perror("fstat failed");
        return -errno;
    }
    if (replicator_enqueue(path) != 0) {
        fprintf(stderr, "replicator_enqueue failed for %s\n", path);
        return -EIO;
    }
    return 0;
}

static int ckptfs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    (void)datasync;
    fprintf(stderr, "FSYNC called for %s\n", path);
    return ckptfs_flush_local_and_enqueue(path, fi);
}

static int ckptfs_release(const char *path, struct fuse_file_info *fi) {
    int rc = 0;
    int fd;
    if (!fi)
        return -EINVAL;
    fd = (int)(uintptr_t)fi->fh;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        fprintf(stderr, "RELEASE triggering flush for %s\n", path);
        rc = ckptfs_flush_local_and_enqueue(path, fi);
    }
    if (fd >= 0) {
        if (close(fd) != 0 && rc == 0)
            rc = -errno;
    }
    fi->fh = 0;
    return rc;
}

struct fuse_operations ckptfs_ops = {
    .getattr = ckptfs_getattr,
    .readdir = ckptfs_readdir,
    .mkdir   = ckptfs_mkdir,
    .open    = ckptfs_open,
    .create  = ckptfs_create,
    .read    = ckptfs_read,
    .write   = ckptfs_write,
    .fsync   = ckptfs_fsync,
    .release = ckptfs_release,
};
