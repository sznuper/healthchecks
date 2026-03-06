#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <SERVER_ID>" >&2
    exit 1
fi

SERVER_ID="$1"

if [[ ! -f "$SCRIPT_DIR/.env" ]]; then
    echo "Error: $SCRIPT_DIR/.env not found. Copy .env.example and fill in the values." >&2
    exit 1
fi

# shellcheck source=.env
source "$SCRIPT_DIR/.env"

: "${HETZNER_API_TOKEN:?HETZNER_API_TOKEN must be set in .env}"

HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X DELETE \
    -H "Authorization: Bearer $HETZNER_API_TOKEN" \
    "https://api.hetzner.cloud/v1/servers/$SERVER_ID")

case "$HTTP_CODE" in
    200|204|404)
        exit 0
        ;;
    *)
        echo "Error: unexpected HTTP status $HTTP_CODE when deleting server $SERVER_ID" >&2
        exit 1
        ;;
esac
