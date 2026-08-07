source /demo/lib.sh
cd /root
note "Second terminal - verify the mount is live"
type_run mount \| grep aifs
note "Write a first test file through the mount"
type_run mkdir -p /mnt/aifs/ckpt-0001
type_run dd if=/dev/zero of=/mnt/aifs/ckpt-0001/test.bin bs=1M count=100 conv=fsync
note "The file landed on the fast local spool immediately:"
type_run ls -lh /mnt/nvme/aifs-cache/ckpt-0001/
sleep 2
note "...and was replicated to the durable backend in the background:"
type_run ls -lah /tmp/aifs-backend/ckpt-0001/
note "The .aifs_committed marker means: this checkpoint is complete and durable."
