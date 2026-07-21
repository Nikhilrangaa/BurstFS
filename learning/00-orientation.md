# Session 0 — Orientation: What This Project Actually Is

> Study notes from deep-dive training. Repo: AIFS / `ckptfs`.

## The one-paragraph summary

Despite the repo name "AI-Accelerator", this is **not an AI-agent platform — it is a storage system**. AIFS (binary: `ckptfs`) is a prototype **checkpoint-acceleration filesystem** written in C using FUSE (Filesystem in Userspace) on Linux. When AI training jobs save checkpoints (model weights + optimizer state, often 10s–100s of GB) directly to durable shared storage (NFS / Azure NetApp Files), the GPUs sit idle waiting. AIFS makes checkpoint writes land on fast **local NVMe** first (training resumes quickly), then copies data to durable storage **in the background**.

Measured on Azure L8as_v3: raw NVMe 3,200 MB/s · AIFS async path 1,400 MB/s · AIFS durable (fsync) path 879 MB/s. Business pitch: this is a **GPU-efficiency tool**, not just storage. Pricing calculator: https://aifs-calculator.vercel.app/

## The mental model: three directories, one illusion

`./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache`

| Role | Example path | What it is |
|---|---|---|
| **Mountpoint** | `/mnt/aifs` | The *virtual* filesystem apps see. Nothing stored here. |
| **Spool** ("upper") | `/mnt/nvme/aifs-cache` | Fast local NVMe. All new writes land here first. |
| **Lower** | `/tmp/aifs-backend` | Durable storage (stand-in for NFS). Source of truth after a crash. |

## Write path (the heart of the system)

1. `create` → file physically created in **spool**; old `.aifs_committed` marker for that checkpoint dir deleted; journal → `CKPT_PENDING`.
2. `write` → plain `pwrite` to spool fd (NVMe speed).
3. `fsync` → fsync the spool file, journal → `CKPT_LOCAL_DURABLE`, **enqueue background replication job**, return. App unblocked. This is the entire performance win.
4. Background replicator thread → copy spool file to `lower/...file.tmp` → atomic `rename()` → fsync parent dir → journal `CKPT_REMOTE_DURABLE`.
5. When **all** journaled files under a checkpoint's top-level dir are `REMOTE_DURABLE` → write `.aifs_committed` into that dir in lower (fsync'd). Marker = "this whole checkpoint is complete and trustworthy."

## Read path

Path resolution is **spool first, fall back to lower** (`visible_path()` in `src/pathing.c`) — same upper/lower idea as overlayfs.

## Crash recovery model

The journal (`src/journal.c`) is **in-memory only** — 5-state machine per file:
`PENDING → LOCAL_DURABLE → REPLICATING → REMOTE_DURABLE` (or `FAILED`).
It evaporates on crash. Recovery is **conservative**: on startup, the whole spool dir is renamed to `<spool>.orphan.<pid>` (quarantine). Only lower — specifically checkpoint dirs bearing `.aifs_committed` — is trusted. A checkpoint mid-replication at crash time never gets its marker → training resumes from the *previous* committed checkpoint. Correctness bought by sacrificing the newest (possibly incomplete) checkpoint.

## File map

| File | Role |
|---|---|
| `src/main.c` | Args → spool quarantine → start journal + replicator → `fuse_main()` |
| `src/fs.c` | The 9 FUSE callbacks; commit-marker hygiene; fsync→enqueue |
| `src/pathing.c` | Virtual path → spool/lower mapping; spool-first resolution |
| `src/journal.c` | In-memory state machine + "whole checkpoint durable?" query |
| `src/replicator.c` | Background thread, ring queue, atomic copy, commit markers |
| `src/util.c` | `mkdir_p`, `copy_file` (1 MiB buffer + fsync) |
| `src/errors.c` | State→string helper — dead code, not in Makefile |

## The three layers of truth (critical for honest defense)

1. **README is stale** — claims no atomic commit / no recovery; recent commits added both.
2. **Code is the MVP** — whole-file granularity, in-memory journal, single replication thread, no `unlink`/`rename`/`truncate`/`rmdir`.
3. **Specs are aspirational** — 64–128 MB chunking, generation counters, eviction, multi-node publish-coherence model: **none implemented**. Never claim they exist in code; say "spec defines target architecture; prototype implements file-granularity MVP." The commit marker *is* a primitive single-node version of the publish model.

## Known defects (seed for test plan)

1. `readdir("/")` lists only the spool → after restart (spool quarantined/empty) `ls /mnt/aifs` shows **nothing**, though committed checkpoints exist in lower. Contradicts stated recovery semantics.
2. Opening an existing lower-only file for write bypasses the spool → synchronous backend writes + later replication fails (`CKPT_FAILED`).
3. No union readdir (spool dir shadows lower contents).
4. Journal: 4096 entries, linear scan, never freed → silent exhaustion.
5. Replication queue full (1023 slots) → `fsync` returns `EIO`.
6. Orphan quarantine dirs accumulate forever → NVMe space leak.
7. `release` + `fsync` double-enqueue (wasteful, benign).
8. `copy_file` ignores `fsync(out_fd)` return value → failed durable write could still be marked `REMOTE_DURABLE`.

## Positioning note

The intern project plan PDF lists AI-agent platforms (CrewAI, AutoGen, LangFlow…) as competitors — wrong category for this product. Real competitive set: raw NFS/cloud file services, WEKA, VAST, DAOS, JuiceFS, Alluxio, Hammerspace, plus framework-level async checkpointing (PyTorch DCP async, Azure Nebula). Confirm with repo owner before doing competitive analysis.

## Practical constraint

Requires **Linux + libfuse3**. Won't build on macOS. Use an Ubuntu VM (README workflow used Azure L8as_v3 with an NVMe data disk).
