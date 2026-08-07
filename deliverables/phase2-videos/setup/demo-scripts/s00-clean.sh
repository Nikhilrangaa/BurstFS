pkill -9 ckptfs 2>/dev/null
sleep 1
fusermount3 -u /mnt/aifs 2>/dev/null || umount -l /mnt/aifs 2>/dev/null
rm -rf /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache /mnt/nvme/aifs-cache.orphan.* /tmp/ckptfs.log /tmp/ckptfs2.log
mkdir -p /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
echo cleaned
