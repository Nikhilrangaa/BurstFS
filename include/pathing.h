#ifndef PATHING_H
#define PATHING_H

#include <stddef.h>

int is_checkpoint_path(const char *path);
void make_lower_path(const char *path, char *out, size_t out_sz);
void make_spool_path(const char *path, char *out, size_t out_sz);
int visible_path(const char *path, char *out, size_t out_sz);

#endif
