source /demo/lib.sh
cd /root/AI-Accelerator
note "Step 3 - create the three directories: mountpoint, durable backend, NVMe spool"
type_run sudo mkdir -p /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
type_run sudo chown -R '$USER:$USER' /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache
note "Step 4 - launch ckptfs: <mountpoint> <durable backend> <NVMe spool>"
note "(runs in the foreground - keep this terminal open, work in a second one)"
printf '\033[1;32m%s\033[0m:\033[1;34m~/AI-Accelerator\033[0m$ ' "$PROMPT_HOST"
cmd="./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache"
for ((i=0; i<${#cmd}; i++)); do printf '%s' "${cmd:$i:1}"; sleep 0.02; done
printf '\n'; sleep 0.3
setsid nohup ./ckptfs /mnt/aifs /tmp/aifs-backend /mnt/nvme/aifs-cache >/tmp/ckptfs.log 2>&1 </dev/null &
disown
sleep 2
grep -v "max threads" /tmp/ckptfs.log | head -3
mount | grep -q aifs && printf '\033[2m# mounted OK - leave this running\033[0m\n'
sleep 1
