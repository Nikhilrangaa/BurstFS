source /demo/lib.sh
cd /root
note "The real thing: save a multi-file training checkpoint and watch both tiers live"
( sleep 3
  mkdir -p /mnt/aifs/ckpt-0100
  dd if=/dev/zero of=/mnt/aifs/ckpt-0100/model.pt      bs=1M count=600 conv=fsync 2>/dev/null
  dd if=/dev/zero of=/mnt/aifs/ckpt-0100/optimizer.pt  bs=1M count=600 conv=fsync 2>/dev/null
  dd if=/dev/zero of=/mnt/aifs/ckpt-0100/scheduler.pt  bs=1M count=400 conv=fsync 2>/dev/null
  dd if=/dev/zero of=/mnt/aifs/ckpt-0100/rng_state.pt  bs=1M count=200 conv=fsync 2>/dev/null
  echo '{"step":100}' > /mnt/aifs/ckpt-0100/state.json
) &
WRITER=$!
printf '\033[1;32m%s\033[0m:\033[1;34m~\033[0m$ ' "$PROMPT_HOST"
cmd="./watch-both.sh   # refreshes both listings 5x/sec"
for ((i=0; i<${#cmd}; i++)); do printf '%s' "${cmd:$i:1}"; sleep 0.02; done
printf '\n'; sleep 0.5
for t in $(seq 1 120); do
  printf '\033[H\033[2J'
  printf '\033[1m  %-38s  %s\033[0m\n' "NVMe SPOOL (local, fast)" "DURABLE BACKEND (source of truth)"
  printf '  %-38s  %s\n' "/mnt/nvme/aifs-cache/ckpt-0100" "/tmp/aifs-backend/ckpt-0100"
  printf '  %-38s  %s\n' "--------------------------------" "--------------------------------"
  for f in model.pt optimizer.pt scheduler.pt rng_state.pt state.json .aifs_committed; do
    L=$(ls -lh /mnt/nvme/aifs-cache/ckpt-0100/$f 2>/dev/null | awk '{print $5}')
    R=$(ls -lh /tmp/aifs-backend/ckpt-0100/$f 2>/dev/null | awk '{print $5}')
    [ -z "$L" ] && [ -z "$R" ] && continue
    printf '  %-24s %-13s  %-24s %s\n' "$f" "${L:--}" "$f" "${R:--}"
  done
  sleep 0.2
done
wait $WRITER 2>/dev/null
printf '\n'
note "All files replicated - and the commit marker arrived last:"
type_run ls -lahA /tmp/aifs-backend/ckpt-0100/
