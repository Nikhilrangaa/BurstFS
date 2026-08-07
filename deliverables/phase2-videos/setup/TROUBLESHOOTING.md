# AIFS / ckptfs — Troubleshooting

Issues actually encountered while producing the setup video (Ubuntu 24.04), plus known prototype defects that affect day-to-day use.

## Hit during real setup runs

### 1. Mount dies when the launching shell exits
**Symptom:** `ckptfs` was started with `&` in a script or SSH session; when the shell exits, the mount disappears (`mount | grep aifs` empty, or `Transport endpoint is not connected`).
**Cause:** ckptfs runs in the foreground and receives SIGHUP when its terminal goes away.
**Fix:** run it in a dedicated terminal/tmux pane, or detach it properly:
```bash
setsid nohup ./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache >/var/tmp/ckptfs.log 2>&1 </dev/null &
```

### 2. "Transport endpoint is not connected" / stale mount after a crash
**Symptom:** ckptfs died (crash or `pkill -9`) but `/mnt/aifs` errors on access and a new launch fails.
**Fix:** clear the stale FUSE mount, then relaunch:
```bash
fusermount3 -u /mnt/aifs    # or: sudo umount -l /mnt/aifs
```

### 3. Writes appear at GB/s but nothing shows up in spool/backend
**Symptom:** `dd` reports very high throughput; spool and backend directories stay empty.
**Cause:** the mount isn't actually live — you wrote into the plain `/mnt/aifs` directory on the root disk.
**Fix:** always verify `mount | grep aifs` before testing; unmount/remount if needed.

### 4. `Ignoring invalid max threads value 4294967295 > max (100000).` at startup
Harmless libfuse tuning message on Ubuntu 24.04 — not an error; the mount proceeds normally.

### 5. `fusermount3`/mount fails inside a container
Running in Docker requires `--device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor=unconfined`. Without those flags, `fuse: device not found` or permission errors.

## Known prototype defects (from the learning-track defect list)

### 6. `ls /mnt/aifs` is empty after a restart, but data exists
`readdir` lists only the (freshly quarantined, empty) spool. **Your data is safe** — check the backend directly (`ls /tmp/aifs-backend`). Restart logic should read checkpoints from the backend path, keyed on `.aifs_committed`.

### 7. Writing to a file that exists only in the backend is slow, then fails replication
Opening a lower-only file for write bypasses the spool (synchronous backend I/O; the follow-up replication job fails with `CKPT_FAILED` in the log). Write **new** files per checkpoint instead of overwriting old ones.

### 8. `fsync` starts returning EIO under heavy load
The replication queue holds 1,023 jobs; when full, `fsync` fails with EIO. Throttle checkpoint frequency or wait for the replicator to drain. (Single replication thread in the MVP.)

### 9. NVMe fills up over time
Two causes: quarantined `<spool>.orphan.<pid>` directories from each restart are never deleted (remove them manually once you've confirmed the backend is complete), and there is no cache eviction yet — the spool only grows.

### 10. No `rm` / `mv` / `truncate` through the mount
These FUSE operations aren't implemented. Manage retention directly on the backend storage; keep the mount for writes and reads of active checkpoints.

### 11. `.aifs_committed` never appears for a checkpoint
Check that every file under that checkpoint directory finished replicating (journal states in the ckptfs log). Common cause: the checkpoint was written as loose files at the mount root instead of inside its own directory — the commit model is directory-scoped.

## Setup-script hazard (repeat of the INSTALL warning)

`setup/aifs-setup.sh` picks the **first** NVMe device and runs `mkfs.ext4 -F` on it with no confirmation — on many cloud VMs that's the **OS disk**. Edit the device name (and the placeholder backend path) before use, or provision manually per INSTALL.md §3.
