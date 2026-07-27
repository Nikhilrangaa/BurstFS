# Piece 3 — The OS Kernel and Syscalls: who touches the hardware, and how programs ask

**The question this piece answers: your C program never actually touches the drive — so who does, and how does asking work?**

## Two worlds inside one computer

Every running computer is split into two privilege worlds, enforced by the CPU itself:

- **Kernel space** — where the **kernel** (the core of the OS: Linux) runs with unlimited power. Only the kernel may talk to hardware: drives, network cards, RAM management, screens.
- **User space** — where every normal program runs: your browser, `python`, `dd`, and `ckptfs`. User-space programs *cannot* touch hardware. At all.

Why the wall exists: dozens of programs run at once. If any program could write raw blocks to the drive, one bug in any of them could destroy the filesystem data structure for everyone. So the CPU enforces: user-space code that tries to touch hardware directly is stopped dead. All hardware access is funneled through one trusted gatekeeper — the kernel.

## Syscalls: the service window

If programs can't touch hardware, how does anything ever get saved? They **ask the kernel**. The asking mechanism is the **system call (syscall)** — a special CPU instruction that freezes your program, switches the CPU into kernel mode, runs the kernel's handler for your request, then switches back and resumes your program with the result.

Everything a program does to the outside world is a syscall. The ones that matter for this project:

| Syscall | Meaning |
|---|---|
| `open(path, flags)` | "Find/create this file; give me a handle." |
| `read(fd, buf, n)` / `write(fd, buf, n)` | "Move n bytes through handle fd." |
| `close(fd)` | "Done with this handle." |
| `fsync(fd)` | "Force this file's data to durable media; don't return until done." |
| `rename(old, new)` | "Atomically swap this directory entry." |
| `mkdir`, `unlink`, `stat` | Make dir, delete name, read metadata. |

`printf`, Python's `f.write()`, `torch.save()` — all end up at these same few syscalls. The syscall layer is the narrow waist of the whole OS.

## File descriptors: the claim ticket

`open()` returns a small integer — a **file descriptor (fd)**. It indexes a per-process table *inside the kernel* where the kernel remembers: which inode, current offset, open mode. Subsequent `read(fd)`/`write(fd)`/`fsync(fd)` calls just present the ticket — no path lookup again. Path resolution (the phone-book walk from Piece 2) happens **once, at open time**; that's a core efficiency pattern you'll see AIFS copy exactly.

## Crossing the wall costs time

A syscall is a **context switch**: save program state, switch privilege, run kernel code, switch back. Each crossing costs on the order of a microsecond — nothing for one call, dominant if you make millions. Rule of thumb behind lots of systems design (and FUSE's slowness later): **fewer, bigger requests beat many small ones.**

## The VFS: the kernel's switchboard

Inside the kernel, one more layer: the **VFS (Virtual File System)**. When `write(fd, ...)` arrives, the VFS looks at which mount point the file lives under and routes the call to the right filesystem implementation:

- path under `/` → ext4 driver (root drive)
- path under `/mnt/nvme` → ext4 driver (NVMe drive)
- path under `/mnt/aifs` → **the FUSE driver, which forwards the request to a user-space program** (Piece 8)

The VFS is why every filesystem "feels" identical to applications — same syscalls, same behavior contract — regardless of what's underneath. It's also the exact hook that makes AIFS possible: mount ckptfs at `/mnt/aifs`, and the VFS delivers every syscall under that path to it.

## Seeing it live

`strace` prints every syscall a program makes:

```
$ strace -e trace=openat,read,write,fsync,rename,close dd if=/dev/zero of=/tmp/x bs=1M count=2
openat(AT_FDCWD, "/tmp/x", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(3, "\0\0..."..., 1048576) = 1048576
write(3, "\0\0..."..., 1048576) = 1048576
close(3) = 0
```

We'll use strace on the Linux VM to *watch* a checkpoint write flow through AIFS.

## Connect it to AIFS

- ckptfs is an ordinary **user-space** program. It has no special powers — every file operation it performs on spool/lower is itself a normal syscall to the kernel.
- The training app's syscalls on `/mnt/aifs/...` get routed by the VFS to ckptfs (via FUSE), which then issues its *own* syscalls on the real spool/lower paths. One app-level write = syscalls on both sides of the wall. (This double-crossing is the FUSE performance tax measured in the benchmarks.)
- The fd/claim-ticket pattern appears verbatim in `src/fs.c`: at open/create time ckptfs opens the *real* spool file and stashes that fd; every later read/write/fsync just reuses it.
- `fsync` is the only syscall carrying a durability promise — the whole AIFS design (Piece 4) is built around intercepting and reinterpreting it.

## Check questions

1. Why can't your program write to the drive directly, and who can?
2. What physically happens when a program makes a syscall? Why isn't it free?
3. What is a file descriptor, what does the kernel remember behind it, and what expensive work does it let you skip repeating?
4. What does the VFS do when a `write()` arrives, and why is the VFS the thing that makes AIFS possible?
5. `torch.save(model, "/mnt/aifs/step-1000/model.pt")` runs. Name the syscalls you'd expect to see, in order, and state which *one* of them carries a durability promise.
6. (Re-test from Piece 2) What is a mount point, and which of AIFS's three directories is one?
