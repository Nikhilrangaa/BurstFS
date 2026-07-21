# Fundamentals Track — from zero to AIFS

No prior knowledge assumed. One piece at a time; don't advance until the check questions at the end of each piece feel easy. Each piece ends by connecting back to AIFS so nothing is abstract.

| # | Piece | Question it answers |
|---|---|---|
| 1 | [Hardware: CPU, RAM, storage](piece-01-hardware.md) | Where does data physically live, and what dies when power is cut? |
| 2 | Files and filesystems | What *is* a file? What does the disk actually store? |
| 3 | The OS, the kernel, and syscalls | Who is allowed to touch the hardware, and how do programs ask? |
| 4 | The page cache and `fsync` | Why does `write()` lie about your data being saved? |
| 5 | Processes, threads, and locks | How do two things run "at once" without corrupting shared data? |
| 6 | Network storage (NFS) | Why is shared storage durable but slow? |
| 7 | GPUs, training, and checkpoints | Why does an AI job stop and save, and why does that cost money? |
| 8 | FUSE | How can a normal C program pretend to be a filesystem? |
| 9 | Synthesis | Re-read `00-orientation.md` and `01-systems-deep-dive.md` — they should now feel obvious |

Status: Piece 1 written. Later pieces are added as we cover them in training sessions.
