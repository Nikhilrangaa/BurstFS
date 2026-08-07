source /demo/lib.sh
cd /root
note "Step 2 - clone the repository and build"
type_run git clone https://github.com/Nikhilrangaa/AI-Accelerator.git
printf '\033[1;32m%s\033[0m:\033[1;34m~\033[0m$ cd AI-Accelerator\n' "$PROMPT_HOST"
cd /root/AI-Accelerator; sleep 0.5
type_run make
type_run ls -lh ckptfs
