# AIFS / ckptfs — Installation Guide

Step-by-step install, verified end-to-end on **Ubuntu 24.04** (also applies to Ubuntu 22.04 and WSL2). Every command below was executed exactly as written during the recording of the setup video; where the container run differs from real hardware, it's called out.

## 0. Requirements

- Linux (Ubuntu 22.04/24.04 or WSL2). **macOS is not supported** — FUSE3/Linux only.
- `sudo` access.
- For real performance: a machine with a **local NVMe disk** (e.g. Azure L-series) and durable shared storage (NFS / Azure NetApp Files) as the backend. For a functional try-out, any two directories work.

## 1. Install build prerequisites

```bash
sudo apt-get update
sudo apt-get install -y build-essential fuse3 libfuse3-dev pkg-config
```

Verify: `gcc --version` (13.x on Ubuntu 24.04) and `pkg-config --modversion fuse3` (3.14+).

## 2. Clone and build

```bash
git clone https://github.com/Nikhilrangaa/AI-Accelerator.git
cd AI-Accelerator
make
```

Produces a single binary, `ckptfs` (~74 KB). No install step — run it from the repo directory.

## 3. (Real hardware only) Provision the NVMe data disk

Skip this on WSL2 or if you're just trying it out. On a cloud VM, identify the **data** NVMe disk with `lsblk`, then partition, format, and mount it (full transcript in the top-level README, recorded on Azure L8as_v3):

```bash
lsblk                              # find the data disk — e.g. nvme1n1, NOT the OS disk
sudo fdisk /dev/nvme1n1            # n → p → 1 → defaults → w
sudo mkfs.ext4 /dev/nvme1n1p1
sudo mkdir -p /mnt/nvme
sudo mount /dev/nvme1n1p1 /mnt/nvme
```

> ⚠️ **Warning about `setup/aifs-setup.sh`:** the bundled script auto-selects the **first** NVMe device (`lsblk … | grep nvme | head -n 1`) and runs `mkfs.ext4 -F` on it **without confirmation**. On many VMs (including the Azure VM in the README) the first NVMe device is the **OS disk**. Read the script and fix the device name before running it — or follow the manual steps above instead. The script also leaves a placeholder backend path you must edit.

## 4. Create the three directories

| Argument | Role | Example |
|---|---|---|
| 1. mountpoint | Virtual FS your apps use | `/mnt/aifs` |
| 2. lower root | Durable backend (NFS in production) | `/tmp/aifs-backend` |
| 3. spool root | Fast local NVMe staging | `/mnt/nvme/aifs-cache` |

```bash
sudo mkdir -p /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
sudo chown -R $USER:$USER /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
```

## 5. Launch

```bash
./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
```

- Runs in the **foreground** — keep this terminal open; use a second terminal for everything else.
- A message like `Ignoring invalid max threads value 4294967295 > max (100000).` is harmless (libfuse default tuning).
- On startup, any pre-existing spool content is quarantined to `<spool>.orphan.<pid>` — expected behavior, see the recovery model.

## 6. Verify

From a second terminal:

```bash
mount | grep aifs        # → ckptfs on /mnt/aifs type fuse.ckptfs (…)

# write a checkpoint THROUGH the mount — always inside its own directory:
mkdir -p /mnt/aifs/ckpt-0001
dd if=/dev/zero of=/mnt/aifs/ckpt-0001/test.bin bs=1M count=100 conv=fsync

ls -lh  /mnt/nvme/aifs-cache/ckpt-0001/   # file is on the spool immediately
ls -lah /tmp/aifs-backend/ckpt-0001/      # arrives here shortly after, plus .aifs_committed
```

The hidden `.aifs_committed` file appears in the backend copy of a checkpoint directory only when **every** file under it is durably replicated — that's the "this checkpoint is complete and trustworthy" signal, and the marker your restart logic should look for.

Watch replication live (two terminals or a tmux split):

```bash
watch -n 0.2 'find /mnt/nvme/aifs-cache -type f -ls'   # spool: instant
watch -n 0.2 'find /tmp/aifs-backend  -type f -ls'     # backend: trails behind
```

## 7. Shutdown / restart

```bash
# stop
pkill ckptfs
fusermount3 -u /mnt/aifs   # if the mountpoint is left disconnected

# start again — old spool is quarantined automatically
./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
```

Recovery rule: after any crash, trust only backend checkpoint directories containing `.aifs_committed`; resume from the newest one.

## Usage rules (prototype limitations that affect setup)

1. **Each checkpoint goes in its own top-level directory** under the mount (`/mnt/aifs/ckpt-N/...`). Loose files at the mount root break the commit-marker model.
2. Write **new** files; don't append to files that exist only in the backend (that path bypasses the spool).
3. `rm`, `mv`, `truncate` through the mount are not implemented yet — manage lifecycle directly on the backend.

See `TROUBLESHOOTING.md` for symptoms and fixes.
