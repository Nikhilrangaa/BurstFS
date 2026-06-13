#ifndef REPLICATOR_H
#define REPLICATOR_H

int replicator_start(void);
void replicator_stop(void);
int replicator_enqueue(const char *virtual_path);

#endif
