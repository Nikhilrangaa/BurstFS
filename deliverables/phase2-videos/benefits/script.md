# Benefits Video — Script (3–5 min)

Audience: someone with **no prior knowledge** of AIFS who wants to see it actually work and come away understanding the idea.
Format: 1920×1080, dark slides + one live demo segment (real prototype footage recorded in a Linux container).
Narration: neural TTS (`en-US-AndrewNeural`, −4% rate), stitched sentence-by-sentence with deliberate pauses between ideas and loudness-normalized to −16 LUFS. Re-recordable by a human over the same visuals — the caption file carries the exact per-sentence timings.

| # | Scene | Visual | Narration |
|---|---|---|---|
| 1 | Title | `slides/01-title` | Every large AI training run has a hidden tax. This video is about what it costs you — and how to stop paying it. |
| 2 | Hook | `slides/02-hook` (stat tiles) | Every twenty to thirty minutes, a training job stops to save a checkpoint — the model's weights, and its optimizer state. While that file writes, your GPUs do nothing. At H100 rates, one large checkpoint can burn about five dollars of idle GPU time. Now multiply that by every checkpoint, every node, every day. |
| 3 | Problem | `slides/03-problem` (flow + stall chart) | So why does this happen? Checkpoints are big — ten to a hundred-plus gibibytes — and they're written to durable, shared storage, like NFS or Azure NetApp Files. Durable storage is the right place for them. It survives crashes. But it's slow — and that write sits on the training loop's critical path. The bigger the checkpoint, the longer the stall. At 128 gigabytes, the durable path can hold your GPUs for two and a half minutes — every single time. |
| 4 | The idea | `slides/04-idea` (3-step flow) | AIFS — the AI File System Accelerator — fixes this with one idea: decouple performance from durability. AIFS is a filesystem layer. Your training job just writes to a normal directory — no code changes. Behind the mount, writes land on fast local NVMe first — and training resumes immediately. In the background, AIFS replicates the data to your durable storage — which stays the source of truth. And when every file in a checkpoint is safely replicated, AIFS writes a commit marker: this checkpoint is complete, and trustworthy. |
| 5 | Live demo | container footage (`s05-watch-replication.cast`) | Now — let's watch it actually work. This is the real prototype — open source, written in C — running live. On the left: the local NVMe spool. On the right: the durable backend. We save a multi-file training checkpoint — model, optimizer, scheduler state. Watch the left side. The files appear instantly — the training job is already unblocked. Now the right side. The same files arrive one by one, as the background replicator copies them over. And last of all, the commit marker — .aifs_committed — appears, sealing the checkpoint. If the node crashed mid-copy, that marker would never be written — and training would simply resume from the previous committed checkpoint. Durability — without the wait. |
| 6 | Numbers: speed | `slides/06-chart-throughput` | On real Azure hardware with local NVMe, the prototype sustains 879 megabytes per second — on the fully durable path. A four-gibibyte checkpoint is locally safe in under five seconds. Replication finishes about half a minute later — off the critical path. |
| 7 | Numbers: money | `slides/07-chart-cost` | And that speed converts directly into money. Seconds saved per checkpoint, times checkpoints per day, times GPUs per node, times what you pay per GPU-hour. At 128 gigabytes, that's roughly five dollars of avoided idle per checkpoint — before you multiply across the cluster. |
| 8 | Who + CTA | `slides/08-cta` | So — if you run training jobs that checkpoint frequently, operate GPU infrastructure, or sell GPU capacity — AIFS turns storage speed into GPU efficiency. Try the pricing calculator to estimate your own savings — and find the prototype, the specs, and the benchmarks on GitHub. Stop paying the checkpoint tax. |

## Pronunciation guide (for TTS or human re-record)

- **AIFS** → "A-I-F-S" (letters) · **ckptfs** → "checkpoint-F-S" · **.aifs_committed** → "dot A-I-F-S committed" · **NVMe** → "N-V-M-E" · **879** → "eight hundred and seventy-nine".

## Sources for every claim

- 879 MB/s durable path, 4 GiB in 4.9 s, backend arrival 30–40 s later: README `dd` transcript, Azure Standard L8as_v3.
- 3,200 / 1,400 / 879 MB/s: `docs/ValueProp-GTM/benchmark.md`.
- ~$5 avoided idle per 128 GB checkpoint (Lambda $2.49, RunPod $2.69 per H100-hr): `docs/ValueProp-GTM/avoididlegpu.png` data.
- ~150 s durable-path stall at 128 GB: `docs/ValueProp-GTM/checkpointstall.png` data.
- Checkpoint sizes 10–100+ GiB every 5–60 min: `docs/ValueProp-GTM/AIFS - Sales One Pager`.
- Demo footage: recorded live from the actual `ckptfs` binary built from this repo (Ubuntu 24.04, FUSE3), unedited terminal capture (played at 0.8× speed in scene 5 for readability).
- Calculator: https://aifs-calculator.vercel.app

## Production notes

- Demo scene renders the asciinema cast; the rest are static slides timed to narration.
- The prototype is single-node, whole-file MVP — the video claims only measured/modeled numbers and describes the commit-marker recovery rule actually implemented.
