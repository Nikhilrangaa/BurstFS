
# AIFS crash/restart patch notes

This patch adds a **minimal conservative recovery model** that matches your requirement:

> If a crash happens before NVMe data is flushed to persistent storage, recovery should use the **prior valid checkpoint**.

## What changed

1. **Main startup now quarantines the spool/NVMe root** before mounting.
   - This prevents leftover unflushed NVMe data from shadowing the persistent store on restart.
   - It intentionally favors correctness over recovering the newest staged checkpoint.

2. **Replication is now atomic per file**.
   - Files are copied to `lower/file.tmp` and renamed into place.

3. **Internal checkpoint commit marker**
   - The replicator writes `.aifs_committed` into a checkpoint root only when **all journaled files under that root** have reached `REMOTE_DURABLE`.
   - This assumes a checkpoint is organized under a **top-level directory**, e.g.:
     - `/step-1000/model.pt`
     - `/step-1000/optimizer.pt`

4. **Create/write path removes old commit marker** for the affected checkpoint root.
   - This marks the checkpoint root dirty while new writes are in progress.

5. **COMMIT marker is hidden from directory listings**.

## Important assumption

This patch assumes each checkpoint is written under its own **top-level directory** under `/mnt/aifs`.

Examples that work well:
- `/checkpoint-001/...`
- `/step-1000/...`

If your training framework writes a single flat file directly at the mount root, the checkpoint-root commit model will need slight adjustment.

## Recovery semantics after this patch

- **Visible after restart**: persistent-store checkpoints that were fully replicated and marked `.aifs_committed`
- **Not trusted after restart**: leftover unflushed NVMe/spool contents
- **Fallback behavior**: prior committed checkpoint remains the safe restart point


