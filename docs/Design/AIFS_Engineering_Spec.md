# Formal Engineering Specification
## AIFS (AI Data Flow Filesystem)
### NVMe-accelerated, checkpoint-aware data orchestration layer for AI / HPC / EDA / Genomics workloads

*Prepared for architecture review and implementation planning*

> **Note:** This document is structured to be GitHub-friendly while remaining suitable for internal engineering review, version control attachment, and iterative updates. It is the Markdown version of the Word specification derived from **AIFS_Formal_Engineering_Spec.docx**.[Source: fetch_file reference content]

---

## 1. Executive Summary

AIFS is a userspace filesystem and data orchestration layer that presents a single POSIX filesystem namespace to applications while transparently combining local NVMe for high-speed writes and read caching with durable shared storage (initially NFS-based) as the source of truth.

Applications interact with one mount point, such as `/mnt/aifs`, while AIFS internally maps logical files to fixed-size chunks stored or cached on local NVMe and asynchronously persisted back to durable backing files.

The design goal is to deliver local-NVMe write performance, shared-storage durability, single-node simplicity, and workload-aware behavior for AI, HPC, EDA, and Genomics data pipelines.

---

## 2. Scope and Purpose

This specification defines the initial architecture, functional scope, internal module boundaries, data path behavior, durability semantics, and implementation plan for AIFS.

It is intended for engineering implementation, architectural review, and future conversion into repository documentation, detailed task breakdowns, and design review artifacts.

---

## 3. Problem Statement

- Checkpoint writes to durable shared storage are often too slow for AI and HPC workflows.
- Repeated training, restore, and restart operations frequently re-read cold data from remote storage.
- Durable NFS or shared storage is required, but its latency and throughput characteristics do not match local NVMe.
- Many high-performance distributed filesystems are operationally complex, cluster-heavy, or unnecessarily expensive for the initial target deployment model.
- There is a need for a simpler single-node acceleration layer that uses commodity NVMe while preserving durable backing storage and POSIX semantics.

---

## 4. Goals

### 4.1 Functional Goals

- Expose a single POSIX-compatible mount point to applications.
- Support Linux applications using standard filesystem APIs.
- Accelerate checkpoint-heavy workloads.
- Provide read caching for training, restore, scan, and replay workloads.
- Persist data durably to shared storage.
- Support `fsync`-driven durability semantics.
- Continue buffering locally during temporary durable-backend slowness, subject to policy and capacity limits.
- Support future workload-aware scheduling and checkpoint-aware APIs.

### 4.2 Non-Functional Goals

- Implementable in C using FUSE on Linux.
- Operationally simple for single-node deployment.
- Capable of handling large files and large chunk counts.
- Modular enough to evolve into production-grade code with recovery, policy, and observability extensions.
- Transparent to end users, who should see only a single mount point.

### 4.3 Explicit Non-Goals (v1)

- A fully distributed shared-writer clustered filesystem.
- Full multi-node cache coherence among independent AIFS nodes.
- A replacement for the durable backend itself.
- Deduplication, compression, or object-native reformatting in v1.
- Page-sized tracking or fine-grained dirty-range replication in the initial release.

---

## 5. System Overview

AIFS is composed of a foreground POSIX-facing FUSE module and a background durability and policy subsystem. The FUSE layer handles application syscalls. The background flush engine persists dirty chunks from local NVMe to the durable NFS backing store based on policy triggers and durability requirements.

### 5.1 Logical Architecture

```text
Application / Framework / Tool
        |
        v
   AIFS FUSE Filesystem
        |
        +--------------------+
        |                    |
        v                    v
 NVMe Chunk Store       Flush / Policy Engine
        |                    |
        +----------+---------+
                   |
                   v
        Durable Backing Storage
            (NFS / ANF / Dell)
```

---

## 6. User Experience and Mount Model

The user should interact with only one visible filesystem mount, such as `/mnt/aifs`. Internal mounts or storage paths used for local NVMe cache and durable backing storage are product-managed and not directly user-facing.

- Visible user mount: `/mnt/aifs`
- Hidden local cache path: `/var/lib/aifs/cache`
- Hidden metadata/runtime path: `/var/lib/aifs/meta` and `/var/lib/aifs/runtime`
- Hidden or product-managed backing path: mounted NFS volume or equivalent durable backend

The product should eventually support deployment through a single CLI or service-based model rather than requiring users to manually manage multiple mounts.

---

## 7. Storage Model

### 7.1 Logical File Representation

Applications see one normal file, for example `/mnt/aifs/model.pt`.

Internally, AIFS represents a file as an ordered sequence of fixed-size chunks.

```text
model.pt = [chunk_0][chunk_1][chunk_2]...[chunk_n]
```

### 7.2 Durable Representation

Durable storage retains a normal full backing file such as `/data/model.pt`. Dirty local chunks are written back to this file using offset-based `pwrite` operations at the correct byte ranges.

### 7.3 Local NVMe Representation

Only needed chunks are materialized locally on NVMe. These may be dirty, cached, prefetched, or otherwise active.

```text
/var/lib/aifs/cache/<file-id>/chunk_0
/var/lib/aifs/cache/<file-id>/chunk_1
...
```

---

## 8. Chunking Strategy

### 8.1 Rationale

File-level caching and eviction are too coarse for large AI/HPC datasets and checkpoints. Chunking allows selective caching, selective flush, and selective eviction while preserving the logical file abstraction.

### 8.2 Recommended Initial Chunk Size

- Preferred starting chunk sizes: **64 MB** or **128 MB**
- Avoid page-sized tracking in the initial implementation
- Choose a single fixed chunk size in v1 for simplicity

### 8.3 MVP Dirty Tracking Model

In the first version, an entire chunk should be treated as dirty when modified. Partial-range dirty tracking inside a chunk is intentionally deferred to reduce complexity and implementation risk.

---

## 9. Metadata Model

### 9.1 File Metadata

- `file_id`
- logical path
- backing NFS path
- logical size
- chunk size
- chunk count

### 9.2 Chunk Metadata

- `chunk_id`
- `file_id`
- `chunk_index`
- `chunk_size`
- state: absent, clean, dirty, flushing, error
- `last_dirty_ts`
- `last_flush_attempt_ts`
- `last_flush_success_ts`
- `generation`
- `flushing_generation`
- `checkpoint_critical`
- `fsync_waiter_present`
- `nvme_path`

### 9.3 Access Metadata

AIFS does not receive native block-level `atime` from the operating system. Instead, block or chunk-level access information must be derived by observing FUSE read calls and mapping `(offset, size)` to chunk indexes.

- coarse access timestamp or epoch
- access count (possibly approximate or sampled)
- prefetch status
- candidate eviction priority signals

---

## 10. Consistency and Durability Model

### 10.1 Default Write Policy

AIFS uses write-back behavior by default. Normal application writes are acknowledged after local NVMe persistence rather than waiting for immediate NFS durability.

### 10.2 Durability Boundaries

- `write()`: acknowledged after successful local NVMe chunk update
- `fsync()`: returns only after relevant dirty chunks are durably persisted to NFS
- checkpoint completion: policy-driven durability boundary, usually treated similarly to `fsync()` for checkpoint data
- `flush()`: no strong durable-media guarantee by itself
- `close()`: not assumed to imply durability unless explicitly configured

### 10.3 Failure Window

If a node crashes before dirty chunks are flushed to NFS, recently acknowledged write-back data resident only on local NVMe may be lost. This is mitigated by age-based flush, periodic flush, checkpoint-triggered flush, and future journaling and recovery mechanisms.

---

## 11. Foreground Read Path

1. Resolve path or file handle to file metadata.
2. Map requested `(offset, size)` into affected chunk indexes.
3. For each chunk, attempt local NVMe read first.
4. If the chunk is absent locally, fetch required data from the NFS backing file and optionally materialize/cache the chunk on NVMe.
5. Assemble requested byte ranges into the application buffer.
6. Update chunk-level access metadata.
7. Optionally launch asynchronous prefetch for adjacent or predicted-future chunks.

---

## 12. Foreground Write Path

1. Resolve path or file handle to file metadata.
2. Map requested `(offset, size)` into the affected chunk range.
3. Ensure each touched chunk exists locally on NVMe. If performing a partial overwrite and the chunk is not local, fetch the original chunk from NFS first.
4. Write modified bytes into the local chunk file at the appropriate intra-chunk offsets.
5. Increment chunk generation and mark the chunk dirty.
6. Update logical file size if the write extends the file.
7. Return success to the application after local NVMe write completion.

---

## 13. Flush Engine

### 13.1 Purpose

The flush engine is a background durability subsystem that persists dirty chunks from local NVMe to the durable NFS backing store, respects `fsync` and checkpoint durability boundaries, batches writes for throughput, and coordinates with eviction and capacity pressure.

### 13.2 Flush Triggers

- `fsync`-triggered flush (highest operational priority during normal runtime)
- checkpoint completion flush
- age-based flush for dirty chunks older than a threshold
- capacity-pressure flush when NVMe exceeds configured watermarks
- periodic safety flush on a timer
- eviction-triggered flush for dirty chunks that must be freed

### 13.3 Scheduler Requirements

- Maintain flush request priority ordering.
- Batch requests by file.
- Sort chunks by index or backing-file offset.
- Coalesce adjacent chunks into larger writes.
- Avoid incorrectly marking a chunk clean if it was rewritten during flush.
- Support retries and error state transitions.
- Provide wait-notify semantics for `fsync` callers.

### 13.4 Priority Order

1. shutdown
2. `fsync`
3. checkpoint
4. eviction
5. capacity pressure
6. aged dirty
7. periodic background flush

---

## 14. Generation-Based Correctness

Each chunk maintains a generation counter. Every successful write increments generation. When a flush begins, the flush engine snapshots the current generation into `flushing_generation`.

- If `generation == flushing_generation` after the flush completes, the flushed data is still current and the chunk may be marked clean.
- If `generation != flushing_generation`, the chunk was rewritten while the flush was in progress and must remain dirty and be re-enqueued.

This prevents stale-clean bugs and is essential for correctness in an asynchronous flush design.

---

## 15. `fsync` Semantics

1. Identify dirty chunks for the file.
2. Mark them as requiring durability.
3. Enqueue them with `fsync` priority.
4. Wait until the relevant dirty generations are durably written to NFS.
5. Optionally issue durable sync on the backing file descriptor if required by policy.
6. Return success or error to the application.

`fsync` is the primary point where the foreground FUSE path explicitly blocks on the background flush engine.

---

## 16. Checkpoint-Aware Durability

Checkpoint boundaries are important durability events in AI/HPC workflows. AIFS should provide policy hooks that enable immediate prioritized flush on checkpoint completion.

### 16.1 Initial Options

- Explicit application or framework signaling
- Directory or file naming conventions such as `checkpoint_*`
- Control-plane or helper CLI notification

---

## 17. Eviction Model

### 17.1 Recommended Unit of Eviction

AIFS should evict at chunk or segment granularity rather than full-file granularity. File-level eviction is acceptable only as a coarse fallback or for small-file scenarios, not as the primary design for large AI/HPC data.

### 17.2 Eviction Policy Signals

- coarse recency
- optional access frequency
- prefetch status
- workload importance (checkpoint versus dataset versus speculative prefetch)
- capacity pressure status

### 17.3 Dirty Chunk Rule

Dirty chunks must never be evicted directly. If a dirty chunk must be removed due to pressure, it must first be flushed durably and only then may the clean local copy be evicted.

---

## 18. FUSE Front-End Module

The FUSE module is the POSIX-facing syscall adapter. It should remain relatively thin and delegate substantive work to metadata, cache, and flush subsystems.

### 18.1 Core Callbacks

- `getattr`
- `readdir`
- `open`
- `read`
- `write`
- `flush`
- `fsync`
- `truncate`
- `create`
- `unlink`
- `rename`
- `release`

### 18.2 Responsibility Split

- **FUSE layer:** syscall adaptation and handle management
- **Metadata manager:** file and chunk lookup, logical size, state tracking
- **Cache manager:** local chunk IO, fetch-on-miss, read-modify-write
- **Flush engine:** asynchronous durability and `fsync` coordination
- **Backing store interface:** offset-based reads and writes to durable NFS files

---

## 19. Failure Scenarios

### 19.1 Node Crash Before Flush

Dirty data acknowledged only to NVMe and not yet flushed to NFS may be lost. This is an intrinsic write-back trade-off.

### 19.2 NFS Outage or Slowness

AIFS should continue buffering locally subject to capacity. Flush retries and backoff are required. `fsync` or checkpoint durability may block or fail depending on policy and backend state.

### 19.3 NVMe Capacity Exhaustion

When NVMe reaches configured thresholds, AIFS must increase flush aggressiveness and reduce available space for new dirty growth. Dirty chunks should be flushed before eviction.

---

## 20. Crash Recovery and Metadata Journal (Recommended Enhancement)

To become production-grade, AIFS should persist metadata transitions in a compact journal.

- `MARK_DIRTY`
- `FLUSH_START`
- `FLUSH_COMPLETE`

On restart, the journal can be replayed to reconstruct dirty chunk state and re-enqueue flush work as needed.

---

## 21. Internal Module Boundaries

- `aifs_fuse.c`: FUSE callbacks and file-handle management
- `metadata.c`: file and chunk metadata index
- `cache.c` or `aifs_io.c`: local chunk reads, writes, read-modify-write, cache materialization
- `flush_engine.c`: flush scheduling, batching, persistence, retries, `fsync` coordination
- `backing_nfs.c`: NFS-specific `pread`, `pwrite`, `fsync` helpers

---

## 22. Suggested Internal APIs

### 22.1 Metadata APIs

```c
file_meta_t *metadata_lookup_by_path(const char *path);
chunk_meta_t *lookup_chunk(file_id_t file_id, uint64_t chunk_index);
int metadata_update_size(file_id_t file_id, uint64_t new_size);
```

### 22.2 Cache APIs

```c
int cache_read_file_range(file_id_t file_id, char *buf, size_t size, off_t offset);
int cache_write_file_range(file_id_t file_id, const char *buf, size_t size, off_t offset);
int ensure_chunk_on_nvme(chunk_meta_t *chunk, const char *nfs_path);
```

### 22.3 Flush APIs

```c
int enqueue_flush(chunk_meta_t *chunk, flush_reason_t reason);
int aifs_fsync_file(file_id_t file_id);
int aifs_checkpoint_complete(file_id_t file_id);
```

### 22.4 Backing Store APIs

```c
ssize_t backing_pread(file_meta_t *file, void *buf, size_t len, off_t off);
ssize_t backing_pwrite(file_meta_t *file, const void *buf, size_t len, off_t off);
int backing_fsync(file_meta_t *file);
```

---

## 23. Example End-to-End Flows

### 23.1 Write and Checkpoint Flow

1. Application writes data into a logical file through AIFS.
2. FUSE write path maps bytes to chunks and updates local NVMe chunk files.
3. Chunks are marked dirty and generation is incremented.
4. A checkpoint or `fsync` boundary is reached.
5. Flush engine prioritizes and persists the relevant chunks to the durable NFS backing file.
6. Durability completion is acknowledged back to the caller if semantics require it.

### 23.2 Read Miss Flow

1. Application reads a logical file range.
2. AIFS resolves chunk ownership for that range.
3. Missing chunks are fetched from NFS.
4. Required bytes are returned to the caller.
5. The fetched chunk may be retained locally for future reads.

### 23.3 Eviction Flow

1. NVMe capacity pressure rises.
2. Eviction policy selects clean cold chunks first.
3. If a selected chunk is dirty, AIFS forces flush before eviction.
4. Clean local chunk files are removed while the durable NFS copy remains intact.

---

## 24. Performance Strategy

- Acknowledge normal writes after local NVMe persistence to decouple application latency from NFS latency.
- Use read-through caching to satisfy repeated reads from NVMe.
- Batch adjacent chunk flushes into larger `pwrite` operations to improve NFS efficiency.
- Use checkpoint and `fsync` as explicit durability boundaries rather than forcing synchronous durability on every write.
- Keep the initial design simple enough to benchmark, analyze, and refine.

---

## 25. Recommended Phased Implementation Plan

### 25.1 Phase 1: MVP

- FUSE layer
- file metadata index
- fixed-size chunk mapping
- local NVMe chunk store
- read-through cache
- write-back dirty tracking
- background flush engine
- `fsync` durability
- checkpoint flush hook
- basic chunk-level eviction
- periodic, age-based, and pressure-based flush triggers

### 25.2 Phase 2: Hardening

- queue deduplication and in-flight tracking
- metadata journal
- crash recovery
- retry backoff policies
- metrics and observability
- smarter eviction policy

### 25.3 Phase 3: Differentiation

- framework-aware checkpoint integration
- dataset pinning
- adaptive prefetch
- workload-aware scheduling
- control-plane API exposure
- Kubernetes and CSI integration

---

## 26. Open Issues and Decisions To Finalize

- Finalize chunk size: **64 MB** versus **128 MB**.
- Decide whether local metadata persistence is in-memory only for MVP or persisted from the start.
- Define checkpoint detection and signaling contract.
- Choose initial eviction algorithm: simple LRU-like chunk policy versus approximate CLOCK/two-queue.
- Define NFS file descriptor lifecycle and retry behavior.
- Define NVMe dirty-buffer budget and high/critical watermarks.

---

## 27. Proposed Repository Structure

```text
aifs/
  docs/
    AIFS_Engineering_Spec.md
  src/
    aifs_fuse.c
    metadata.c
    cache.c
    flush_engine.c
    backing_nfs.c
  include/
    aifs_fuse.h
    metadata.h
    cache.h
    flush_engine.h
    backing_nfs.h
  tests/
  scripts/
```

---

## 28. Summary

AIFS is a FUSE-based, chunk-oriented storage acceleration layer that exposes one POSIX filesystem while using local NVMe for write buffering and read caching, and NFS for durable persistence, driven by a background flush engine with explicit durability semantics for `fsync` and checkpoints.

This specification intentionally prioritizes a practical, buildable architecture over unnecessary early complexity, while preserving a clear path to product hardening and future differentiation.

---

## Appendix A. Suggested GitHub Upload Workflow

1. Upload the `.docx` to the repository documentation area if Word format is needed for collaboration.
2. Use this Markdown file for easier GitHub diff and review workflows.
3. Keep architecture diagrams, sequence diagrams, and API contracts in adjacent `docs/` subfolders.
4. Track open questions and implementation tasks in issues or project boards linked from the spec.
