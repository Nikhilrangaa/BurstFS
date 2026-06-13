#ifndef UTIL_H
#define UTIL_H

int mkdir_p(const char *path);
int copy_file(const char *src, const char *dst);
int ensure_parent_dir(const char *path);

#endif
