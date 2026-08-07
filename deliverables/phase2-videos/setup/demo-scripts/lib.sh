# shared helpers for recorded scenes
PROMPT_HOST="azureuser@gpu-node"
type_run() {
  printf '\033[1;32m%s\033[0m:\033[1;34m%s\033[0m$ ' "$PROMPT_HOST" "${PWD/#\/root/~}"
  local cmd="$*"
  local i
  for ((i=0; i<${#cmd}; i++)); do printf '%s' "${cmd:$i:1}"; sleep 0.02; done
  printf '\n'; sleep 0.3
  eval "$cmd"
  sleep 0.7
}
note() {  # dim comment line, typed fast
  printf '\033[2m# %s\033[0m\n' "$*"; sleep 0.8
}
