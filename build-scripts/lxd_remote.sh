#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="${LXD_ENV_FILE:-.lxd.env}"
if [[ -f "$ENV_FILE" ]]; then
  # shellcheck disable=SC1090
  source "$ENV_FILE"
fi

: "${LXD_HOST:?Missing LXD_HOST (set in .lxd.env or environment)}"
: "${LXD_SSH_USER:?Missing LXD_SSH_USER (set in .lxd.env or environment)}"

LXD_SSH_PORT="${LXD_SSH_PORT:-22}"
LXD_SSH_AUTH="${LXD_SSH_AUTH:-key}"
LXD_SSH_PRIVATE_KEY_PATH="${LXD_SSH_PRIVATE_KEY_PATH:-}"
LXD_WORKDIR="${LXD_WORKDIR:-}"

expand_home() {
  local p="$1"
  if [[ "$p" == ~* ]]; then
    printf '%s\n' "${HOME}${p:1}"
  else
    printf '%s\n' "$p"
  fi
}

ssh_opts=(
  -p "$LXD_SSH_PORT"
  -o BatchMode=yes
  -o ConnectTimeout=10
  -o StrictHostKeyChecking=accept-new
)

if [[ "$LXD_SSH_AUTH" == "key" ]]; then
  : "${LXD_SSH_PRIVATE_KEY_PATH:?Missing LXD_SSH_PRIVATE_KEY_PATH for key auth}"
  key_path="$(expand_home "$LXD_SSH_PRIVATE_KEY_PATH")"
  ssh_opts+=( -i "$key_path" )
fi

if [[ "${1:-}" == "heartbeat" ]]; then
  shift
  cmd='hostname && whoami && uptime && df -h /'
else
  if [[ "$#" -eq 0 ]]; then
    cat <<'USAGE' >&2
Usage:
  build-scripts/lxd_remote.sh heartbeat
  build-scripts/lxd_remote.sh <remote command...>

Environment source: .lxd.env by default (override with LXD_ENV_FILE)
USAGE
    exit 2
  fi

  cmd="$*"
fi

if [[ -n "$LXD_WORKDIR" ]]; then
  remote_cmd="cd $LXD_WORKDIR && ( $cmd )"
else
  remote_cmd="$cmd"
fi

exec ssh "${ssh_opts[@]}" "${LXD_SSH_USER}@${LXD_HOST}" "$remote_cmd"
