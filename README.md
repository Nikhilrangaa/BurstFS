# ckptfs: Minimal Checkpoint-Acceleration FUSE Shim (C)

This is a **minimal prototype** of a checkpoint-focused FUSE filesystem in **C + libfuse3**.

## What it does
- Mounts a FUSE filesystem at a chosen mountpoint.
- Treats the mounted namespace as a **checkpoint namespace**.
- Creates and writes files into a **local spool** directory first.
- Reads from **spool first**, then falls back to the **lower durable storage**.
- Queues a **background replication job** on `fsync()` to copy files from spool to lower storage.

## What it does *not* do yet
- No `rename()` / atomic checkpoint commit flow yet.
- No persistent disk-backed journal yet.
- No startup crash recovery scan yet.
- No directory/namespace reconciliation beyond simple passthrough-style behavior.
- No checksum validation yet.

## Build
```bash
sudo apt update
sudo apt install -y build-essential fuse3 libfuse3-dev pkg-config
make
```

## Run
```bash
mkdir -p /mnt/backing/checkpoints
mkdir -p /mnt/localspool
mkdir -p /mnt/ckptfs

./ckptfs /mnt/ckptfs /mnt/backing/checkpoints /mnt/localspool
```

## Test
In another shell:
```bash
mkdir -p /mnt/ckptfs/run1
python3 - <<'PY'
with open('/mnt/ckptfs/run1/test.bin', 'wb') as f:
    f.write(b'0' * 1024 * 1024)
    f.flush()
    import os
    os.fsync(f.fileno())
PY
```

Then check:
```bash
find /mnt/localspool -type f
find /mnt/backing/checkpoints -type f
```

## Notes
- This is intended as a **starting point**, not a production filesystem.
- Best developed on **Linux or WSL2**.
