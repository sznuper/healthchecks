#!/usr/bin/env bash
# Build all healthchecks and run them on a fresh Debian 13 VPS.
#
# Usage:
#   ./dev/test-on-vps.sh [--skip-build] [--keep]
#
#   --skip-build  Use existing build/ instead of rebuilding
#   --keep        Leave the VPS running after tests (prints SERVER_ID/SERVER_IP)
#
# Requires dev/.env with HETZNER_API_TOKEN and SSH_KEY.
# Optional: set CC= in dev/.env to override the cosmocc binary.

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

log() { echo "==> $*"; }

# ── Build ────────────────────────────────────────────────────────────────────

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    # Find cosmocc — honour CC env var, then PATH, then common location
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
    log "Building with $CC..."
    make -C "$HC_DIR" CC="$CC"
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
        echo ""
        echo "  To delete later: ./dev/delete-server.sh $SERVER_ID"
    fi
}
trap cleanup EXIT INT TERM

log "Creating VPS..."
# shellcheck source=create-server.sh
source <("$SCRIPT_DIR/create-server.sh")

SERVER_ID="${SERVER_ID:?create-server.sh did not set SERVER_ID}"
SERVER_IP="${SERVER_IP:?create-server.sh did not set SERVER_IP}"

# ── Run healthchecks ─────────────────────────────────────────────────────────

log "Running healthchecks on $SERVER_IP..."
echo ""

PASS=0
FAIL=0

for BINARY in "$HC_DIR"/build/*; do
    NAME="$(basename "$BINARY")"
    echo "── $NAME"

    EXIT_CODE=0
    case "$NAME" in
        ssh_journal)
            # pipe trigger: run journalctl on the server and feed output to binary
            "$SCRIPT_DIR/run-binary.sh" "$SERVER_IP" "$BINARY" \
                -- "journalctl -u ssh -u sshd --output=json --output-fields=MESSAGE,__REALTIME_TIMESTAMP --no-pager | /root/ssh_journal" \
                || EXIT_CODE=$?
            ;;
        *)
            # interval trigger: no stdin
            "$SCRIPT_DIR/run-binary.sh" "$SERVER_IP" "$BINARY" < /dev/null || EXIT_CODE=$?
            ;;
    esac

    if [[ $EXIT_CODE -ne 0 ]]; then
        echo "(exit code: $EXIT_CODE)"
        FAIL=$((FAIL + 1))
    else
        PASS=$((PASS + 1))
    fi
    echo ""
done

log "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
