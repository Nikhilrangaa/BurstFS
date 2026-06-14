# AIFS Publish-Based Coherence Model
## Multi-Node Visibility Without Full Cluster-Wide Shared-Write Coherency

**Document Owner:** AIFS Core Engineering / Product Team  
**Version:** 0.1 Draft  
**Status:** Working Draft  
**Audience:** Engineering, Product, Architecture Reviewers, GTM Stakeholders, Early Design Partners  

---

## 1. Purpose

This document defines the **publish-based coherence model** for AIFS. The purpose of this model is to enable **practical multi-node correctness** while preserving the core advantages of AIFS:

- local NVMe write acceleration
- asynchronous durability to a lower storage tier
- hot-read caching
- transparent POSIX filesystem access

The model is explicitly designed to avoid the complexity and performance cost of building a fully coherent distributed filesystem in the initial product version.

---

## 2. Problem Statement

The current AIFS architecture is ideal for **single-node** or **per-node acceleration**, but many AI, HPC, and EDA environments use **multiple compute nodes**.

In a multi-node environment, the following problems arise if each node has an independent local AIFS cache/write buffer:

- Node A may see its own local writes immediately.
- Node B may not see those writes until they are flushed to durable storage.
- Cached data on other nodes may become stale.
- Concurrent shared-write behavior becomes difficult to coordinate safely.

A fully coherent clustered filesystem solves these problems, but doing so requires:

- distributed metadata coordination
- lock or lease management
- cluster-wide invalidation
- write ordering and arbitration
- failure-aware distributed state management

That level of functionality significantly increases complexity and typically introduces meaningful write-path overhead.

The publish-based coherence model provides a simpler alternative.

---

## 3. Design Goal

The goal of the publish-based coherence model is to:

1. preserve **near-local write performance**
2. support **multi-node read sharing**
3. provide **safe workflow handoff** from writer node to reader nodes
4. avoid **strong shared-write coherency** in the initial design
5. align to **checkpoint, stage-complete, and artifact-publication workflows**

---

## 4. Core Idea

Instead of making every local write immediately visible cluster-wide, AIFS introduces an explicit or policy-driven **publish boundary**.

The lifecycle is:

```text
write locally -> flush durably -> publish version -> other nodes consume
```

This means there are two distinct views of data:

### 4.1 Local Working View
- visible to the writer node
- may include unflushed dirty data
- not yet cluster-consumable

### 4.2 Published Cluster View
- visible to all nodes
- durably persisted to the lower storage tier
- safe for downstream readers
- version-stable

This model allows AIFS to preserve local-NVMe performance while introducing a clean correctness boundary for multi-node access.

---

## 5. Key Consistency Principle

> **Readers on other nodes only read published data. Writers modify unpublished working state until publish completes.**

This is the central rule that makes the model simple and useful.

---

## 6. Why This Fits AI / HPC / EDA Workflows

Many AI, HPC, and EDA workflows naturally have **phase boundaries**, such as:

- checkpoint complete
- epoch complete
- stage complete
- artifact finalized
- restart image ready
- intermediate result tree sealed for downstream use

Most of these workflows do **not** require byte-by-byte visibility of in-progress writes across all nodes.

They typically require:

1. fast local write generation
2. durable completion
3. clean handoff to readers

The publish-based coherence model formalizes that handoff.

---

## 7. Scope of the Model

### 7.1 Goals

The publish model shall:

- preserve local write-back acceleration
- support multi-node access to stable outputs
- allow remote readers to see only durable published data
- simplify cache invalidation through version transitions
- align to stage-based workflow boundaries

### 7.2 Non-Goals

The publish model shall not initially attempt to provide:

- unrestricted simultaneous shared-write across nodes
- immediate write visibility across all nodes
- strict cluster-wide POSIX write coherence
- lock-heavy distributed write arbitration on every operation

---

## 8. Conceptual Data States

Every publishable object (file or directory tree) exists in one of two major logical views:

### 8.1 Working State
- mutable
- owned by a writer node or owner context
- visible only to the local writer view
- not yet safe for remote readers

### 8.2 Published State
- durable
- visible cluster-wide
- stable for readers
- immutable or version-stable for consumption

This leads to a clear mental model:

```text
working -> flush -> publish -> cluster-visible
```

---

## 9. Publishable Object Model

AIFS should treat a **publish domain** as a versioned object.

Examples of publishable objects include:

- a single checkpoint file
- a checkpoint directory tree
- a stage output directory
- a model artifact bundle
- a finalized EDA result tree

Each publishable object has:

- an object identifier
- an owner
- a working generation
- a published generation
- a state
- a publish manifest

---

## 10. Recommended Publish Granularity

### 10.1 Directory-Level Publish (Recommended)

For AI/HPC/EDA workflows, publishing a **directory subtree** is often more useful than publishing one file.

Example:

```text
/mnt/aifs/checkpoints/ckpt_100/
    model.pt
    optimizer.pt
    state.json
```

AIFS should ideally make the entire subtree visible atomically to remote readers.

### 10.2 File-Level Publish (Optional Later)

File-level publish can be supported later for finer-grained workflows, but directory-level publish is the most practical starting point.

---

## 11. Version Model

Each publishable object maintains versions.

Example:

```text
object: /mnt/aifs/checkpoints/ckpt_100
working_generation = 17
published_generation = 16
```

After publication:

```text
published_generation = 17
```

This allows AIFS to distinguish between:

- what the writer is currently building
- what the cluster is allowed to consume

---

## 12. Ownership Model

To reduce complexity, AIFS should use a **single-writer ownership model** for each publish domain in v1.

Ownership may be defined by:

- one node
- one job
- one rank
- one workflow stage
- one path-based ownership rule

This avoids shared-write ambiguity.

### 12.1 Good Fit Patterns

- per-node checkpoint shards
- per-rank output files
- node-local scratch trees
- stage-finalized output trees
- write-once publish-many workflows

### 12.2 Poor Fit Patterns for v1

- multiple nodes concurrently mutating the same file
- unrestricted shared-write workspaces
- lock-heavy multi-writer metadata workloads

---

## 13. Visibility Rules

The publish-based model depends on explicit visibility rules.

### Rule 1: Local Writer Sees Working State
The writer node may see its local working state immediately, including unflushed or unpublished updates.

### Rule 2: Remote Nodes See Only Published State
Nodes other than the writer must resolve the object to the latest published generation.

### Rule 3: Publish Requires Durability
A generation cannot be published until all associated data is durably persisted to the lower tier.

### Rule 4: Published State Is Stable for Readers
Once a generation is published, readers should see a stable view of that generation.

### Rule 5: Readers May Pin a Published Generation
A reader that opens a published generation may continue to read that generation even if a newer generation is later published.

---

## 14. Publish State Machine

Each publishable object transitions through the following state machine:

```text
WORKING -> FLUSHING -> PUBLISHING -> PUBLISHED
```

Possible failure branches:

```text
FLUSHING -> FLUSH_FAILED
PUBLISHING -> PUBLISH_FAILED
```

### 14.1 State Definitions

#### WORKING
- writes allowed
- dirty local chunks may exist
- remote readers do not see latest working state

#### FLUSHING
- the selected generation is being durably flushed
- generation is sealed for publication
- writer policy may either block further writes to that generation or divert them to the next working generation

#### PUBLISHING
- all data for the generation is durable
- metadata commit / manifest publication is in progress

#### PUBLISHED
- generation is visible to remote readers
- generation is stable for consumption

---

## 15. Publish Manifest

Each published generation should have a durable metadata manifest.

### 15.1 Manifest Contents
A manifest should include at minimum:

- object_id
- logical path
- generation number
- owner
- file list or subtree membership
- size metadata
- timestamp
- durability-complete flag
- optional integrity metadata later (checksums / hashes)

This manifest becomes the reader contract.

Remote readers should only consume generations that have a committed publish manifest.

---

## 16. Namespace Model Options

### 16.1 Same Path, Generation-Aware Resolution (Recommended)
Applications use a stable path, while AIFS internally resolves:

- local writer -> working generation view
- remote reader -> latest published generation view

Example:

```text
/mnt/aifs/checkpoints/latest
```

Internally:
- writer sees local current generation if owner
- remote readers see latest published generation

### 16.2 Explicit Working and Published Paths (Simpler But Less Transparent)
Example:

```text
/mnt/aifs/work/job123/checkpoint.tmp
/mnt/aifs/published/job123/checkpoint_100
```

This is easier to reason about, but less transparent for applications.

### 16.3 Recommended Choice
Use same-path generation-aware resolution internally, with optional explicit publish APIs to control visibility.

---

## 17. Publish Flow

The publish operation should proceed as follows.

### Step 1: Writer creates working state
Node A writes locally through AIFS:
- data lands on NVMe
- dirty chunks are tracked
- working generation is advanced

### Step 2: Publish requested
Publish may be triggered by:
- explicit CLI or API call
- workflow hook
- checkpoint-completion signal
- policy-based rule

Example:

```text
aifs publish /mnt/aifs/checkpoints/ckpt_100
```

### Step 3: Generation sealed
AIFS freezes the current working generation as the publish candidate.

### Step 4: Durable flush
The flush engine persists all dirty chunks required for the candidate generation.

### Step 5: Manifest committed
After durable flush completes, AIFS commits the publish manifest and updates published_generation.

### Step 6: Remote visibility begins
Other nodes now resolve the object to the published generation.

---

## 18. Read Resolution Model

### 18.1 Writer Read Behavior
The owner node may resolve reads to the working generation so it can continue local iterative access.

### 18.2 Remote Reader Behavior
Remote readers shall resolve the path to the latest published generation only.

This guarantees that remote readers consume a durable, stable view.

---

## 19. Cache Behavior Under Publish Semantics

The publish model dramatically simplifies cache behavior.

### 19.1 Local Writer Cache
The writer node may cache working chunks freely.

### 19.2 Remote Reader Cache
Remote readers may cache chunks belonging to a published generation.

### 19.3 Invalidation Rule
Invalidate or version-switch cached data only when the published generation changes.

This means cache invalidation is **generation-based**, not **write-by-write**.

That is one of the biggest architectural simplifications in the model.

---

## 20. API Surface

AIFS can keep the initial publish API surface minimal.

### 20.1 Explicit Publish
```text
aifs publish <path>
```

### 20.2 Publish With Wait
```text
aifs publish --wait <path>
```

### 20.3 Status Query
```text
aifs status <path>
```

Expected status output may include:
- owner
- state
- working_generation
- published_generation
- durability progress

### 20.4 Optional Published-Only Read Policy
```text
aifs open --published-only <path>
```

### 20.5 Future Auto-Publish Policies
Future policy options may include:
- publish on checkpoint naming pattern
- publish on `fsync()`
- publish on stage-complete event
- publish on directory close

---

## 21. Commit Semantics

### 21.1 Immutable Publish (Recommended)
Once a generation is published, it should be treated as immutable for readers.

Best for:
- checkpoints
- build artifacts
- EDA stage outputs
- finalized results

### 21.2 Latest-Pointer Publish
AIFS may also maintain a pointer to the latest stable generation.

Example:

```text
/checkpoints/latest -> generation 42
```

This is useful for restart and resume workflows.

### 21.3 Recommended Combination
Support both:
- immutable published generations
- movable latest-stable pointer

---

## 22. Failure Handling

The publish model must be failure-safe.

### 22.1 Crash Before Publish
If a node crashes before publish is committed:
- latest working state remains unpublished
- remote nodes continue to see the previous published generation

This is correct behavior.

### 22.2 Crash During Flush
If a node crashes while flushing:
- no new generation becomes visible
- remote nodes continue to see the previous published generation

This is also correct behavior.

### 22.3 Crash After Flush But Before Publish Commit
This case requires metadata recovery logic:
- either replay a sealed durable generation into published state
- or leave it unpublished until recovery decides

This is why a publish journal is important.

---

## 23. Recovery Metadata and Publish Journal

To recover safely, AIFS should persist lightweight publish metadata.

### 23.1 Required Recovery Metadata
For each object generation:
- object_id
- generation
- state
- owner
- durable_complete
- manifest_created
- publish_committed

### 23.2 Suggested Journal Events
```text
BEGIN_GENERATION
MARK_DIRTY
BEGIN_PUBLISH
FLUSH_COMPLETE
COMMIT_PUBLISH
ABORT_PUBLISH
```

This journal is much simpler than a full distributed coherency metadata service because it only tracks publish visibility, not every read/write/invalidation event.

---

## 24. Shared Metadata Requirement

AIFS still requires a small amount of shared metadata for published state.

However, this metadata is limited to:
- ownership
- generation numbers
- manifest locations
- latest published pointers
- publish status

This is significantly simpler than a full distributed metadata plane.

Possible implementations include:
- shared metadata files on the durable backend
- a lightweight metadata database
- a centralized metadata service in later versions

---

## 25. Performance Advantage Over Strong Coherency

The publish model is attractive because **coordination happens only at publish boundaries**, not on every write.

### 25.1 Strong Coherency Model
In a strong coherent multi-node model, writes may require:
- lock acquisition
- metadata updates
- invalidation messages
- ordering guarantees
- remote coordination before acknowledgment

### 25.2 Publish Model
In the publish model:
- local writes remain local and fast
- coordination occurs only when publishing a completed version
- remote readers consume only durable stable outputs

This preserves much more of the local NVMe performance profile and reduces impact on GPU/CPU job efficiency.

---

## 26. Best-Fit Workloads

The publish-based coherence model is especially well-suited for:

### 26.1 AI / ML
- training checkpoints
- restore/resume workflows
- model artifact publication
- read-mostly shared datasets

### 26.2 EDA
- stage-based output handoff
- restart checkpoints
- reference library caching
- intermediate result tree publication

### 26.3 HPC
- rank-local outputs
- stage barriers
- write-local / read-shared patterns
- checkpoint publication and later restart use

### 26.4 Genomics / Life Sciences
- hot working-set generation
- publish-after-stage-complete pipelines
- active-data acceleration with durable archival below

---

## 27. Workloads That Still Need More Than This Model

The publish model is weaker for:

- unrestricted shared-write workspaces
- multiple nodes mutating the same file simultaneously
- applications expecting immediate read-after-write visibility across nodes
- lock-heavy metadata workflows

Those use cases require stronger distributed coordination mechanisms and should be treated as future-evolution scenarios, not v1 product requirements.

---

## 28. Recommended Initial Product Scope

The recommended v1 scope is:

- per-node AIFS instances
- local NVMe working state
- directory-level publish
- single-writer ownership per publish domain
- durable flush before publication
- immutable published generations
- generation-based remote reader visibility
- generation-based cache invalidation
- no unrestricted shared-write cluster behavior

This gives a strong and practical balance of correctness, performance, and buildability.

---

## 29. Example End-to-End Scenario

### 29.1 Multi-Node Checkpoint Handoff

#### Node A writes a checkpoint
```text
/mnt/aifs/checkpoints/ckpt_100/
```

AIFS on node A:
- writes locally to NVMe
- tracks dirty chunks
- marks working generation 100

#### Publish is requested
```text
aifs publish /mnt/aifs/checkpoints/ckpt_100
```

AIFS:
1. seals generation 100
2. flushes all dirty chunks durably
3. commits publish manifest for generation 100
4. updates latest published pointer

#### Node B consumes published checkpoint
Node B resolves the same path and sees:
- published generation 100
- durable, stable content only

Node B may now fetch and cache the published chunks locally.

This is clean, deterministic, and appropriate for restart and downstream-consumption patterns.

---

## 30. Roadmap

### 30.1 v1
- directory-level publish
- explicit publish API
- generation manifests
- single-writer ownership
- remote readers consume published-only
- simple publish journal

### 30.2 v2
- latest-stable pointer
- optional auto-publish policies
- read-after-publish revalidation
- ownership leases
- improved journal recovery

### 30.3 v3
- optional file-level publish
- ownership handoff
- selective invalidation enhancements
- partial shared-read / single-writer coordination services

---

## 31. Positioning Statement

> **AIFS provides publish-based cluster visibility: nodes write locally at NVMe speed, and completed outputs become cluster-visible only after durable publish. This preserves high performance while providing practical multi-node correctness for checkpoint and stage-based workflows.**

---

## 32. Summary

The publish-based coherence model is the recommended path for introducing practical multi-node behavior into AIFS without turning AIFS into a fully coherent distributed filesystem.

It works by separating:
- **local working state** from
- **published cluster-visible state**

That separation allows AIFS to deliver:
- local-speed write performance
- durable publish semantics
- stable remote-reader behavior
- simpler cache invalidation
- strong alignment with checkpoint, stage-complete, and workflow-driven handoff patterns

In short:

> **Treat unfinished data as local working state and make it cluster-visible only at an explicit durable publish boundary.**
