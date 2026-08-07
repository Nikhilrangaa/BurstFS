source /demo/lib.sh
cd /root
note "Crash safety: kill ckptfs mid-flight (simulating a node crash)"
type_run sudo pkill -9 ckptfs
sleep 1
fusermount3 -u /mnt/aifs 2>/dev/null || umount -l /mnt/aifs 2>/dev/null || true
sleep 1
note "Restart it..."
cd /root/AI-Accelerator
printf '\033[1;32m%s\033[0m:\033[1;34m~/AI-Accelerator\033[0m$ ./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache\n' "$PROMPT_HOST"
./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache >/tmp/ckptfs2.log 2>&1 &
sleep 2
note "On startup the old spool is quarantined - never trusted after a crash:"
type_run ls -d /mnt/nvme/aifs-cache\*
note "The durable backend still holds every COMMITTED checkpoint - the source of truth:"
type_run ls -lahA /tmp/aifs-backend/ckpt-0100/ \| head -8
note "Recovery rule: resume training from the latest checkpoint with .aifs_committed"
