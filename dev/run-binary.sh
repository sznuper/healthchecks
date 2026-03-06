#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <SERVER_IP> <LOCAL_BINARY> [-- REMOTE_CMD...]" >&2
    echo "" >&2
    echo "  Copies LOCAL_BINARY to /root/<basename> on the server and runs it." >&2
    echo "  Stdin is passed through to the remote process." >&2
    echo "  Args after -- replace the default remote command." >&2
    echo "" >&2
    echo "Examples:" >&2
    echo "  $0 1.2.3.4 build/disk_usage" >&2
    echo "  echo '{...}' | $0 1.2.3.4 build/ssh_journal" >&2
    echo "  $0 1.2.3.4 build/disk_usage -- env HEALTHCHECK_ARG_RAW=1 /root/disk_usage" >&2
    exit 1
fi

SERVER_IP="$1"
LOCAL_BINARY="$2"
shift 2

# Parse optional -- separator
REMOTE_ARGS=()
if [[ $# -gt 0 && "$1" == "--" ]]; then
    shift
    REMOTE_ARGS=("$@")
fi

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
BINARY_NAME="$(basename "$LOCAL_BINARY")"
REMOTE_PATH="/root/$BINARY_NAME"

# Copy binary to server
scp -q -O "${SSH_OPTS[@]}" "$LOCAL_BINARY" "root@$SERVER_IP:$REMOTE_PATH"

# Build remote command: chmod+x the binary, then execute
if [[ ${#REMOTE_ARGS[@]} -gt 0 ]]; then
    REMOTE_CMD="${REMOTE_ARGS[*]}"
else
    REMOTE_CMD="$REMOTE_PATH"
fi

exec ssh "${SSH_OPTS[@]}" "root@$SERVER_IP" "chmod +x $REMOTE_PATH && $REMOTE_CMD"
