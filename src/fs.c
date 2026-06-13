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

#include "fs.h"
#include "pathing.h"
#include "journal.h"
#include "replicator.h"
#include "util.h"

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

    if (visible_path(path, real, sizeof(real)) != 0)
        return -ENOENT;

    fd = open(real, fi->flags);
    if (fd < 0)
        return -errno;

    fi->fh = (uint64_t)fd;
    return 0;
}

static int ckptfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char spool[PATH_MAX];
    int fd;

    make_spool_path(path, spool, sizeof(spool));
    if (ensure_parent_dir(spool) != 0)
        return -EIO;

    fd = open(spool, fi->flags | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -errno;

    fi->fh = (uint64_t)fd;
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
    (void)path;

    fd = (int)fi->fh;
    res = pwrite(fd, buf, size, offset);
    if (res < 0)
        return -errno;

    return (int)res;
}

static int ckptfs_fsync(const char *path, int datasync, struct fuse_file_info *fi) {
    struct stat st;
    int fd = (int)fi->fh;
    (void)datasync;

    if (fsync(fd) != 0)
        return -errno;

    if (fstat(fd, &st) == 0) {
        journal_mark_local_durable(path, (size_t)st.st_size);
    }

    if (replicator_enqueue(path) != 0)
        return -EIO;

    return 0;
}

static int ckptfs_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    close((int)fi->fh);
    return 0;
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
