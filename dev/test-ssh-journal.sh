#!/usr/bin/env bash
# Test ssh_journal with real SSH events on a live VPS.
#
# Lifecycle:
#   1. Create a fresh Debian 13 VPS
#   2. Attempt failed logins as non-existent users  → "Invalid user" events in journald
#   3. Perform successful logins and logouts         → "Accepted publickey" events
#   4. Run ssh_journal against actual journald output
#   5. Delete the VPS
#
# Usage: ./dev/test-ssh-journal.sh [--skip-build] [--keep]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

KEEP=0
SKIP_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --keep)       KEEP=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

# shellcheck source=.env
[[ -f "$SCRIPT_DIR/.env" ]] && source "$SCRIPT_DIR/.env"

: "${HETZNER_API_TOKEN:?HETZNER_API_TOKEN must be set in .env}"
: "${SSH_KEY:?SSH_KEY must be set in .env}"

log()     { echo "==> $*"; }
section() { echo ""; echo "── $* ──"; }

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)

# ── Build ────────────────────────────────────────────────────────────────────

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    if [[ -z "${CC:-}" ]]; then
        if command -v cosmocc &>/dev/null; then
            CC="cosmocc"
        elif [[ -x "$HOME/repos/cosmopolitan/cosmocc/bin/cosmocc" ]]; then
            CC="$HOME/repos/cosmopolitan/cosmocc/bin/cosmocc"
        else
            echo "Error: cosmocc not found. Set CC= in dev/.env or add it to PATH." >&2
            exit 1
        fi
    fi
    log "Building ssh_journal with $CC..."
    make -C "$HC_DIR" build/ssh_journal CC="$CC"
else
    log "Skipping build (--skip-build)"
fi

# ── Create VPS ───────────────────────────────────────────────────────────────

SERVER_ID=""
SERVER_IP=""

cleanup() {
    echo ""
    if [[ "$KEEP" -eq 0 ]]; then
        if [[ -n "$SERVER_ID" ]]; then
            log "Deleting server $SERVER_ID..."
            "$SCRIPT_DIR/delete-server.sh" "$SERVER_ID"
        fi
    else
        log "Server kept alive."
        echo "  SERVER_ID=$SERVER_ID"
        echo "  SERVER_IP=$SERVER_IP"
        echo "  To delete: ./dev/delete-server.sh $SERVER_ID"
    fi
}
trap cleanup EXIT INT TERM

log "Creating VPS..."
# shellcheck source=create-server.sh
source <("$SCRIPT_DIR/create-server.sh")
: "${SERVER_ID:?create-server.sh did not set SERVER_ID}"
: "${SERVER_IP:?create-server.sh did not set SERVER_IP}"

# ── Generate SSH events ───────────────────────────────────────────────────────

section "Generating failed login attempts"
# Non-existent users → sshd logs "Invalid user X from IP"
for USER in admin ubuntu test operator deploy; do
    echo "  trying $USER@$SERVER_IP..."
    ssh "${SSH_OPTS[@]}" -o BatchMode=yes -o ConnectTimeout=5 \
        "$USER@$SERVER_IP" true 2>/dev/null || true
    sleep 0.5
done

section "Generating successful logins and logouts"
# Accepted publickey for root → sshd logs "Accepted publickey for root from IP"
for i in 1 2 3; do
    echo "  login $i..."
    ssh "${SSH_OPTS[@]}" "root@$SERVER_IP" "echo session $i"
    sleep 0.5
done

log "Waiting for journald to record all events..."
sleep 3

# ── Run ssh_journal ───────────────────────────────────────────────────────────

section "ssh_journal output (alert_on=both)"
echo ""

"$SCRIPT_DIR/run-binary.sh" "$SERVER_IP" "$HC_DIR/build/ssh_journal" \
    -- "journalctl SYSLOG_FACILITY=10 SYSLOG_FACILITY=4 --output=json --output-fields=MESSAGE,__REALTIME_TIMESTAMP --no-pager \
        | env HEALTHCHECK_ARG_ALERT_ON=both /root/ssh_journal"
