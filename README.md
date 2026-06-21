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

rm -rf /tmp/aifs-backend/*
rm -rf /mnt/nvme/aifs-cache/*


mkdir -p /mnt/aifs
mkdir -p /tmp/aifs-backend
mkdir -p /mnt/nvme/aifs-cache


sudo chown -R $USER:$USER /mnt/aifs
sudo chown -R $USER:$USER /tmp/aifs-backend
sudo chown -R $USER:$USER /mnt/nvme/aifs-cache

./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache

The above can be run from one ssh window. From another Terminal( ssh window)  make sure 
mount | grep aifs lists ckptfs on /mnt/aifs
```

## Test
```bash
dd if=/dev/zero of=/mnt/aifs/test.bin bs=1M count=100

sudo dd if=/dev/zero of=/mnt/nvme/test.bin bs=1M count=9000 conv=fsync   (this is actual write to the disk bypassing the cache)
```
## check 
```bash
ls -lh /mnt/nvme/aifs-cache
ls -lh /tmp/aifs-backend
```

## mounting on the nvmedisk
```bash
azureuser@ranga-ubuntu-vm:~$ lsblk
NAME         MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS
loop0          7:0    0 49.3M  1 loop /snap/snapd/26865
loop1          7:1    0   74M  1 loop /snap/core22/2411
loop2          7:2    0 13.5M  1 loop /snap/canonical-livepatch/406
sr0           11:0    1  628K  0 rom
nvme0n1      259:0    0   30G  0 disk
├─nvme0n1p1  259:1    0   29G  0 part /
├─nvme0n1p14 259:2    0    4M  0 part
├─nvme0n1p15 259:3    0  106M  0 part /boot/efi
└─nvme0n1p16 259:4    0  913M  0 part /boot
nvme1n1      259:5    0  110G  0 disk

azureuser@ranga-ubuntu-vm:~$ sudo fdisk /dev/nvme1n1

Welcome to fdisk (util-linux 2.39.3).
Changes will remain in memory only, until you decide to write them.
Be careful before using the write command.

Device does not contain a recognized partition table.
Created a new DOS (MBR) disklabel with disk identifier 0x3708403b.

Command (m for help): n
Partition type
   p   primary (0 primary, 0 extended, 4 free)
   e   extended (container for logical partitions)
Select (default p): p
Partition number (1-4, default 1): 1
First sector (2048-230686719, default 2048):
Last sector, +/-sectors or +/-size{K,M,G,T,P} (2048-230686719, default 230686719):

Created a new partition 1 of type 'Linux' and of size 110 GiB.

Command (m for help): w
The partition table has been altered.
Calling ioctl() to re-read partition table.
Syncing disks.
azureuser@ranga-ubuntu-vm:~$ sudo mkfs.ext4 /dev/nvme1n1p1
mke2fs 1.47.0 (5-Feb-2023)
Discarding device blocks: done
Creating filesystem with 28835584 4k blocks and 7208960 inodes
Filesystem UUID: 331eaa1d-ea0e-49a6-aeac-c94ca46f9661
Superblock backups stored on blocks:
        32768, 98304, 163840, 229376, 294912, 819200, 884736, 1605632, 2654208,
        4096000, 7962624, 11239424, 20480000, 23887872

Allocating group tables: done
Writing inode tables: done
Creating journal (131072 blocks): done
Writing superblocks and filesystem accounting information: done

## create a file system
sudo mkfs.ext4 /dev/nvme0n1p1

azureuser@ranga-ubuntu-vm:~$ sudo mkdir -p /mnt/nvme
azureuser@ranga-ubuntu-vm:~$ sudo mount /dev/nvme1n1p1 /mnt/nvme
azureuser@ranga-ubuntu-vm:~$ df -h
Filesystem       Size  Used Avail Use% Mounted on
/dev/root         29G  6.4G   22G  23% /
tmpfs            3.9G     0  3.9G   0% /dev/shm
tmpfs            1.6G  2.0M  1.6G   1% /run
tmpfs            5.0M     0  5.0M   0% /run/lock
/dev/nvme0n1p16  881M   64M  756M   8% /boot
/dev/nvme0n1p15  105M  6.2M   99M   6% /boot/efi
tmpfs            794M   12K  794M   1% /run/user/1000
/dev/nvme1n1p1   108G   24K  103G   1% /mnt/nvme
azureuser@ranga-ubuntu-vm:~$

## to watch the file written to NVME and then to the backend persistent drive (/tmp/** or NFS share)
watch -n 0.2 'find /tmp/aifs-backend -type f -ls'      -> writing to the backend (Tmp or NFS share)
watch -n 0.2 'find /mnt/nvme/aifs-cache -type f -ls'   -> writing to NVMe


## Test shows local NVme at 879 MB/s and immediately showing up whereas the same file shows up in /tmp/aifs-backend after a noticeable (30 or 40 seconds later)
## Skew is Standard L8as v3 (8 vcpus, 64 GiB memory)

azureuser@ubuntu-vm:~/ai/ai$ sudo dd if=/dev/zero of=/mnt/aifs/test9.bin bs=1M count=4096 conv=fsync
4096+0 records in
4096+0 records out
4294967296 bytes (4.3 GB, 4.0 GiB) copied, 4.8876 s, 879 MB/s
azureuser@ubuntu-vm:~/ai/ai$
```
## Users usage model 
aifs start <br>
export CKPT_PATH=/mnt/aifs <br>
run_training.sh <br>

The aifs-setup.sh script should hide all the fdisk/mkfs/mount described above.

## Pricing calculator
The **AIFS Pricing Calculator** is designed to quantify the impact of checkpoint latency on GPU efficiency and estimate the economic value of accelerating checkpoint operations using AIFS (AI File System Accelerator).<br>
Try the calculator to see your savings:  https://aifs-calculator.vercel.app/

## Notes
- This is intended as a **starting point**, not a production filesystem.
- Best developed on **Linux or WSL2**.
