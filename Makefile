CC=gcc
CFLAGS=-Wall -Wextra -O2 -Iinclude $(shell pkg-config fuse3 --cflags)
LDFLAGS=$(shell pkg-config fuse3 --libs) -lpthread

SRC=src/main.c src/fs.c src/pathing.c src/journal.c src/replicator.c src/util.c
OBJ=$(SRC:.c=.o)

all: ckptfs

ckptfs: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(OBJ) ckptfs
