# Piece 1 — Hardware: CPU, RAM, and Storage

**The question this piece answers: where does data physically live, and what dies when the power is cut?**

## The three machines inside your machine

Every computer — your MacBook, an Azure VM, a GPU training server — is built from the same three kinds of parts:

### 1. CPU (Central Processing Unit) — the worker
The CPU executes instructions: add these numbers, compare these values, copy these bytes. It is absurdly fast (billions of instructions per second) but it has almost no storage of its own — just a few dozen "registers" (think: the numbers a person can hold in their head) and small on-chip caches. The CPU is the only part that *does* anything; everything else just holds data for it.

### 2. RAM (memory) — the desk
RAM is where running programs and their data live while in use. Access time: ~100 nanoseconds — effectively instant.
**The one property that drives everything in this project: RAM is *volatile*. Power off = every byte in RAM is gone, instantly and unrecoverably.**

### 3. Storage (disk) — the filing cabinet
Storage keeps data when the power is off: *non-volatile / persistent / durable* (three words for the same idea). Two technologies you'll meet:
- **SSD / NVMe drive** — flash memory chips, no moving parts. NVMe is the modern fast interface for SSDs, plugged close to the CPU. Sequential speed on a good NVMe drive: ~3,000+ MB/s (the repo's benchmark measured 3,200 MB/s).
- **Hard disk (HDD)** — spinning magnetic platters. Cheap, big, slow. Mostly relevant as bulk/archive storage behind network file servers.

## The speed ladder (the single most important table in systems engineering)

| Where | Access time | Speed vs RAM | Survives power loss? |
|---|---|---|---|
| CPU register/cache | ~1 ns | faster | No |
| RAM | ~100 ns | 1× | **No** |
| NVMe SSD | ~100 µs (1,000× RAM) | ~3 GB/s streaming | **Yes** |
| Network storage (NFS) | ~1–10 ms+ | often ~0.1–1 GB/s shared | **Yes** |

Two facts to burn in:
1. **Each rung down is roughly 1,000× slower than the one above.**
2. **Everything fast forgets; everything that remembers is slow.** Fast+volatile at the top, slow+durable at the bottom.

## The universal trick: caching / tiering

Because of that trade-off, all of computing plays the same trick: **put the data you're using right now on a fast rung, keep the authoritative copy on a durable rung, and move data between rungs behind the scenes.** A "cache" is just a fast layer holding a temporary copy of data whose real home is a slower layer.

The hard part is never the copying — it's the bookkeeping: *which copy is newest? which copy can I trust after a crash?* That bookkeeping problem has a name: **consistency**. Getting it right when power can vanish at any instant is most of what storage engineers do.

## Connect it to AIFS

AIFS is exactly this trick, applied one more time:
- Training data being written *right now* → **NVMe** (fast rung, "spool").
- The authoritative durable copy → **network storage / NFS** (slow rung, "lower").
- A background process moves data down the ladder, and marker files (`.aifs_committed`) do the "which copy can I trust?" bookkeeping.

The repo's benchmark numbers are just readings from the ladder: NVMe rung ≈ 3,200 MB/s; the durable path through the system ≈ 879 MB/s; the network rung below trickles along afterwards.

## Check questions (answer without looking)

1. Your program has an important result sitting in RAM. The power cord is pulled. What happens to it, and why?
2. Rank fastest → slowest: NFS over the network, RAM, NVMe SSD, CPU cache. Which of them survive power loss?
3. In one sentence: what is a cache?
4. Why does "fast" almost always come packaged with "forgets when power dies," and what is the universal trick computers use to get the best of both?
5. Preview question (reason it out, no wrong answers): if a checkpoint file is sitting on the NVMe spool but hasn't been copied to the lower/NFS tier yet, and the machine reboots cleanly (no power loss) — is the data physically still on the NVMe drive? So why might a *cautious* recovery design still refuse to use it?
