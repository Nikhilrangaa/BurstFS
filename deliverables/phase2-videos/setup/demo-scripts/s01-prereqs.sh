source /demo/lib.sh
note "Step 1 - install build prerequisites (Ubuntu 24.04)"
type_run sudo apt-get update -qq
type_run sudo apt-get install -y -qq build-essential fuse3 libfuse3-dev pkg-config
type_run gcc --version
