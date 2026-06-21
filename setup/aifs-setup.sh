
# 1. Detect NVMe device
DEVICE=$(lsblk -dpno NAME | grep nvme | head -n 1)

# 2. Format if needed
sudo mkfs.ext4 -F $DEVICE

# 3. Mount it
sudo mkdir -p /mnt/nvme
sudo mount $DEVICE /mnt/nvme

# 4. Create dirs
mkdir -p /mnt/nvme/aifs-cache
mkdir -p /mnt/aifs

# 5. Start AIFS
./ckptfs /mnt/aifs /your/backend/path /mnt/nvme/aifs-cache
