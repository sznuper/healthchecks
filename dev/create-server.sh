#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=.env
[[ -f "$SCRIPT_DIR/.env" ]] && source "$SCRIPT_DIR/.env"

: "${HETZNER_API_TOKEN:?HETZNER_API_TOKEN must be set in .env}"
: "${SSH_KEY:?SSH_KEY must be set in .env}"

SERVER_NAME="${SERVER_NAME:-hc-e2e-test}"
SERVER_TYPE="cx23"
LOCATION="fsn1"
IMAGE="debian-13"

log() { echo "$*" >&2; }

# Delete any existing server with the same name
log "Checking for existing server '$SERVER_NAME'..."
EXISTING=$(curl -s \
    -H "Authorization: Bearer $HETZNER_API_TOKEN" \
    "https://api.hetzner.cloud/v1/servers?name=$SERVER_NAME")
EXISTING_ID=$(echo "$EXISTING" | python3 -c "
import sys, json
servers = json.load(sys.stdin).get('servers', [])
print(servers[0]['id'] if servers else '')
" 2>/dev/null || true)

if [[ -n "$EXISTING_ID" ]]; then
    log "Deleting existing server $EXISTING_ID..."
    curl -s -o /dev/null -X DELETE \
        -H "Authorization: Bearer $HETZNER_API_TOKEN" \
        "https://api.hetzner.cloud/v1/servers/$EXISTING_ID"
    sleep 3
fi

# Create server
log "Creating server '$SERVER_NAME'..."
RESPONSE=$(curl -s -X POST \
    -H "Authorization: Bearer $HETZNER_API_TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"name\":\"$SERVER_NAME\",\"server_type\":\"$SERVER_TYPE\",\"location\":\"$LOCATION\",\"image\":\"$IMAGE\",\"ssh_keys\":[\"$SSH_KEY\"]}" \
    "https://api.hetzner.cloud/v1/servers")

read -r SERVER_ID SERVER_IP < <(echo "$RESPONSE" | python3 -c "
import sys, json
d = json.load(sys.stdin)
s = d.get('server', {})
print(s.get('id', ''), s.get('public_net', {}).get('ipv4', {}).get('ip', ''))
")

if [[ -z "$SERVER_ID" || -z "$SERVER_IP" ]]; then
    log "Error: failed to parse server creation response:"
    echo "$RESPONSE" >&2
    exit 1
fi

log "Server created: ID=$SERVER_ID IP=$SERVER_IP"

# Wait for status=running
log "Waiting for server to reach running state..."
for i in $(seq 1 60); do
    STATUS=$(curl -s \
        -H "Authorization: Bearer $HETZNER_API_TOKEN" \
        "https://api.hetzner.cloud/v1/servers/$SERVER_ID" | python3 -c "
import sys, json
print(json.load(sys.stdin).get('server', {}).get('status', ''))
" 2>/dev/null || true)
    if [[ "$STATUS" == "running" ]]; then
        log "Server is running."
        break
    fi
    sleep 2
done

# Wait for SSH on port 22 (up to 60s)
log "Waiting for SSH on $SERVER_IP:22..."
for i in $(seq 1 60); do
    if timeout 2 bash -c "echo > /dev/tcp/$SERVER_IP/22" 2>/dev/null; then
        log "SSH is ready."
        break
    fi
    sleep 1
done

echo "SERVER_ID=$SERVER_ID"
echo "SERVER_IP=$SERVER_IP"
