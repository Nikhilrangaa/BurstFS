# Session 1 — Systems Deep-Dive (interview-defense level)

> Prerequisite: `00-orientation.md`. This is the advanced material — revisit after finishing the fundamentals track (`02-fundamentals/`).

## Part 0 — What a normal `write()` does (no AIFS)

1. Syscall traps into the kernel → routed through the **VFS** (the abstraction that lets ext4/NFS/FUSE sit behind one API).
2. Data is copied into the **page cache** (kernel RAM). `write()` returns. **Not durable yet.**
3. Kernel writeback threads flush dirty pages to disk later (~30s).
4. Only `fsync(fd)` forces the flush + device cache flush and blocks until data is on stable media. **`fsync` is the only durability promise in POSIX.**

On NFS the "device" is a network server → flush/fsync costs network round-trips. 100 GB checkpoint at ~900 MB/s ≈ 2 minutes of idle GPUs.

**AIFS in one sentence: it moves the durability boundary from the slow tier (NFS) to the fast tier (NVMe), and uses an atomic commit protocol to keep the slow tier consistent.** It is the page-cache idea rebuilt in userspace: NVMe plays RAM's role, NFS plays the disk's role, plus an explicit commit protocol because NVMe survives some failures and not others.

## Part 1 — FUSE mechanics

1. `fuse_main` (main.c) opens `/dev/fuse` and mounts at the mountpoint; the VFS now routes every syscall under `/mnt/aifs` to the FUSE kernel module.
2. Kernel packages each op into a request queue behind `/dev/fuse`.
3. libfuse workers in our process read requests → dispatch to callbacks in `ckptfs_ops` (fs.c) → write replies back → kernel completes the app's syscall.

Key facts:
- **Cost:** extra user↔kernel transitions + memory copies per op → why FUSE is slower (1,400 vs 3,200 MB/s). Big sequential writes (requests up to ~1 MiB) amortize it; small-file metadata workloads are worst-case.
- **Why FUSE anyway:** dev velocity, no kernel code, a bug kills a process not the machine, easy deploys. Precedents: sshfs, s3fs, JuiceFS client, GlusterFS.
- **Threading:** without `-s`, libfuse is **multithreaded** → callbacks run concurrently → shared state needs locks.
- **`fi->fh` trick:** `open`/`create` stash the real fd in `fi->fh`; `read`/`write`/`fsync`/`release` reuse it — path resolution happens once.
- **vs overlayfs:** overlayfs = union *view* with copy-up; AIFS = **tiering engine with write-back and a durability protocol** (background down-copy is the whole point).

## Part 2 — Life of one checkpoint + full crash matrix

Trace for `/mnt/aifs/step-1000/model.pt`:

- **T1 `create`**: real file at `spool/step-1000/model.pt`; unlink old `lower/step-1000/.aifs_committed` (root now *dirty*); journal `PENDING`.
- **T2 `write`s**: `pwrite` to spool fd at NVMe speed.
- **T3 `fsync`**: fsync spool fd (blocks until on NVMe) → `LOCAL_DURABLE` → enqueue replication → **return**. Trainer paid NVMe latency; NFS cost off the critical path. This IS the product.
- **T4 replicator**: `REPLICATING` → copy to `lower/.../model.pt.tmp` (1 MiB loop + fsync tmp) → `rename(tmp, final)` → fsync parent dir → `REMOTE_DURABLE`.
- **T5 commit check**: if ALL journaled files under `/step-1000` are `REMOTE_DURABLE` → write `.aifs_committed` (write → fsync file → fsync parent dir).

Crash matrix (after restart, spool is quarantined; only lower + markers matter):

| Crash instant | lower/step-1000 state | Committed? | Outcome |
|---|---|---|---|
| During T2 | old/absent | No | Fall back to previous checkpoint; app never got fsync ack — correct |
| **After T3 (fsync acked)** | old | No | **Checkpoint lost despite fsync success** — the deliberate trade-off |
| Mid-copy T4 | orphan `.tmp` | No | Tmp ignored by contract (never cleaned — defect) |
| After file 1 renamed, file 2 not | **mixed tree** | No | Marker absence saves you — why per-file atomicity isn't enough |
| After marker | complete tree + marker | Yes | This is the restart point |
| During marker write | marker durably there or not | Effectively no | Marker itself fsync'd; no torn "maybe" |

Three ordering guarantees:
1. **Marker deleted before any mutation** — "dirty" declared before it's true. Worst case = spurious missing marker (lose a good checkpoint), never a present marker on a bad tree.
2. **Data before name** — tmp fsync'd *before* rename; parent dir fsync'd after so the *name* is durable. File data / file name / directory entry are three separately durable things.
3. **Marker written last**, fully fsync'd — a **commit record**, same shape as a database transaction commit.

Nuance: **nothing in the filesystem enforces the marker** — `open`/`getattr` fall back to lower regardless; `readdir` hides the marker and `open` blocks it. The marker is a contract for the *launcher/training script*, checked on the backend path today. Gap between mechanism and enforcement — name it unprompted.

## Part 3 — Design critiques (raise them before the interviewer does)

1. **"fsync acked but checkpoint lost = broken contract?"** Deliberately weakened: fsync = *locally* durable + replication scheduled. Rational for checkpoints because they're redundant by design — fall back one interval. Roadmap: `aifs publish --wait` adds an opt-in strong boundary.
2. **"Why fsync the spool if restart quarantines it anyway?"** Sharp observation: under quarantine-everything, the spool fsync (the 1,400→879 gap!) buys almost nothing today. Defenses: keeps `LOCAL_DURABLE` honest for a future spool-scanning recovery; bounds loss if a future version reattaches instead of quarantining. Improvement rec: make spool fsync policy-optional.
3. **"The 'journal' is a misnomer."** Correct — in-memory state table, nothing replayed. Crash-safe *despite* it via quarantine + markers (conservative inference from durable state). Cost: recovery can't distinguish complete spool checkpoints from garbage, so discards both. **Highest-value future feature:** persist `MARK_DIRTY`/`FLUSH_START`/`FLUSH_COMPLETE` records to an fsync'd log on lower; replay at mount → salvages the fsync-then-crash row.

## Part 4 — Concurrency

Threads: N libfuse workers + 1 replicator. Shared: journal table (mutex), replication queue (mutex + condvar).

- Ring buffer: capacity `MAX_QUEUE−1` = 1023 (one slot sacrificed: `head==tail` = empty, `(tail+1)%N==head` = full).
- Consumer: `while (queue_empty() && g_running) pthread_cond_wait(...)` — `while` not `if` because of spurious wakeups + shutdown broadcast recheck.
- Shutdown: `g_running=0` + broadcast + join; loop exits on `!g_running && queue_empty()` → **drains remaining jobs** → clean unmount finishes replication.
- Backpressure: queue full → enqueue fails → app's `fsync` gets `EIO`. Real system would block the producer.

**The commit-marker TOCTOU race** (found by reading):
- Replicator: last file of `/step-1000` done → `journal_all_remote_durable_under_root` → true (journal lock released) → about to write marker.
- FUSE thread: `open`s a file under `/step-1000` for write → unlinks marker (not there yet) → marks `PENDING`.
- Replicator writes marker → **durable `.aifs_committed` on a dirty checkpoint.** Violates the core invariant.
- Fixes: per-root lock across check+write; or re-verify under journal lock after marker creation and unlink if dirtied; or generation counters (spec roadmap). Mitigation: realistic workload writes each `step-N` once — but that's mitigation, not correctness.

Also: `fsync` + `release` both enqueue → duplicate copy (benign, wasteful). Close-without-fsync still flushes via `release`.

## Part 5 — Performance

- 3,200 → 1,400 (raw → FUSE async): FUSE tax — boundary crossings + copies per request. ~56% loss typical for naive FUSE data path.
- 1,400 → 879 (async → fsync): spool fsync cost (see Critique 2 — barely monetized under quarantine).
- 30–40 s later: replication trickles at backend speed, off the critical path — the point.
- GPU math for the pitch: 4 GiB at 879 MB/s ≈ 4.9 s stall vs ~46 s if a ~100 MB/s NFS were on the critical path; saved-seconds × cluster size × ckpt frequency × $/GPU-hr = pricing-calculator story.
- Levers, ranked: FUSE `writeback_cache` + `max_write` tuning; `copy_file_range()` (in-kernel copy/reflink) instead of read/write loop; multiple replication workers; io_uring FUSE; long-term: bypass FUSE for the data plane.

## Part 6 — Mock deep-dive questions (practice out loud)

1. Walk me through a checkpoint write end-to-end (90 seconds, unbroken: T1–T5).
2. What exactly does fsync guarantee here? (local durable + enqueued; justify via checkpoint redundancy)
3. Power fails right after fsync returns? (matrix row 2; loss bounded to one interval)
4. Why `.tmp` + rename? (rename atomicity; data-fsync before, dir-fsync after)
5. Why the marker if files are per-file atomic? (mixed-tree row; set-wise torn; transaction commit record)
6. Journal dies with the process — how is this crash-safe? (runtime state, not recovery journal; quarantine + markers; persisted journal = roadmap)
7. Two FUSE threads at once — isolation story? (locks; then *volunteer the TOCTOU race* — strongest move available)
8. Isn't this just overlayfs / a cache? (union view vs tiering engine + durability protocol)
9. Why FUSE not kernel module? (velocity/safety/deploys; value is policy not path length; precedents; perf ceiling + levers)
10. How does a reader know a checkpoint is trustworthy? (marker contract; enforcement gap; publish API roadmap)
11. What breaks at scale? (journal 4096/linear/never-freed; 1023-slot queue → EIO; single copy thread; orphan leak; no delete/rename)
12. What next and why? (ranked: persistent journal → union readdir/restart visibility → publish API → chunking per spec)

## Vocabulary checklist

FUSE · VFS · page cache · write-back vs write-through · what fsync guarantees (and what `write()` doesn't) · atomic rename + parent-dir fsync · durability vs atomicity vs consistency · checkpoint stall / GPU idle economics · state-machine journaling · producer/consumer with mutex + condvar · conservative recovery (quarantine trades recency for correctness)
