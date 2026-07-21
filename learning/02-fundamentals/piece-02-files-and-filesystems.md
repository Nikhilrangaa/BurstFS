# Piece 2 — Files and Filesystems: what a file actually is

**The question this piece answers: when you "save a file," what does the disk actually store?**

## A drive knows nothing about files

A drive (NVMe, HDD) is just a gigantic array of numbered storage slots called **blocks** (typically 4 KB each). A 1 TB drive ≈ 250 million blocks, numbered 0 to 250 million. The drive's only API: "read block #N" / "write block #N." No files, no folders, no names. That's it.

Files are an *illusion* built on top — and the software that builds the illusion is the **filesystem** (ext4 on Linux, APFS on macOS, NTFS on Windows). A filesystem is a data structure spread across those blocks.

## The three ingredients of the illusion (ext4-style)

### 1. File data
The actual bytes of your file, scattered across whatever blocks were free. A 10 KB file occupies 3 blocks, not necessarily adjacent.

### 2. The inode — the file's ID card
For every file, the filesystem keeps one small record called an **inode**, holding:
- which blocks contain the file's data (the block map)
- the file's size, owner, permissions, timestamps

Crucially: **the inode does NOT contain the file's name.** A file, to the filesystem, is inode #8,391,204 with 3 data blocks. Names live elsewhere.

### 3. The directory — a name→inode phone book
A **directory is itself a file** whose data is just a table:

```
"model.pt"      → inode 8391204
"optimizer.pt"  → inode 8391207
"notes.txt"     → inode 8391911
```

A **path** like `/step-1000/model.pt` is walked one hop at a time: root directory's table → find "step-1000" → that inode is a directory → read its table → find "model.pt" → inode 8391204 → read its blocks.

## Consequences that matter enormously later

1. **A file's name, its metadata, and its data are three separate things stored in three separate places.** Updating one does not automatically persist the others. (This is why, later, you'll see AIFS's code carefully syncing a file AND its parent directory — two different objects on disk.)
2. **`rename()` is tiny and atomic.** Renaming a 100 GB file moves zero data — it edits one table entry in the directory. Filesystems guarantee rename is all-or-nothing: after a crash, the old name or the new name exists, never a half-name, never a name pointing at garbage. This single guarantee is the foundation of crash-safe software everywhere — and the heart of AIFS's replicator (`copy to file.tmp` → `rename to file` = the file appears *complete or not at all*).
3. **Deleting = removing a table entry** (the "unlink" of a name from an inode). The inode/data are reclaimed when no names point to them.

## Mounts: gluing filesystems into one tree

Linux presents a single directory tree starting at `/`, but different subtrees can be served by different filesystems on different drives. Attaching one at a directory is called **mounting**; the directory is the **mount point**.

- `/` → filesystem on drive 1
- `/mnt/nvme` → a different filesystem on the NVMe drive (`mount /dev/nvme1n1p1 /mnt/nvme`)
- `/mnt/aifs` → can be served by *a program pretending to be a filesystem* (FUSE — Piece 8)

Crossing a mount point silently switches which filesystem answers your requests. Caveat you now understand: rename atomicity holds *within* one filesystem — you can't atomically rename across two different filesystems (that's a copy).

## Connect it to AIFS

`./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache` — all three arguments are just directories, but on different filesystems:
- `/mnt/nvme/aifs-cache` (spool) — real directory on the NVMe drive's ext4 filesystem
- `/tmp/aifs-backend` (lower) — real directory standing in for network storage
- `/mnt/aifs` — mount point where the ckptfs program itself answers filesystem requests

AIFS's "virtual" files are a mapping trick: the path `/mnt/aifs/step-1000/model.pt` is translated by string concatenation into a *real* path in spool or lower (`src/pathing.c` — it's ~40 lines and after this piece you can read it). The `.aifs_committed` marker is just an ordinary tiny file used as a durable, atomic signal flag. And the replicator's copy-then-rename ritual (within the lower filesystem, so atomicity holds) is consequence #2 above, deployed deliberately.

## Check questions

1. A drive has no concept of files. What does it actually offer, and what builds the file illusion?
2. What three separate things make up "a file," and where does the *name* live?
3. Why is `rename()` atomic and instant even for a 100 GB file — and what does "atomic" buy you after a crash?
4. You write data into `lower/model.pt.tmp`, finish, then rename it to `lower/model.pt`. The machine crashes at some unknown moment during all this. What are the possible states of `lower/` afterwards — and which *impossible* state is the whole point of the ritual?
5. What is a mount point, and which of AIFS's three directories is one?
