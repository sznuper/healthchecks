#!/usr/bin/env bash
#
# test-output.sh - Verify compiled healthcheck binaries produce correct v2 protocol output.
#
# Compiles with gcc, runs each binary, validates output format and required fields.
# Usage: dev/test-output.sh
#
set -euo pipefail

PASS=0
FAIL=0
BUILD_DIR="build"

pass() { PASS=$((PASS + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

assert_has() {
    local output="$1" pattern="$2" label="$3"
    if echo "$output" | grep -qE "$pattern"; then
        pass "$label"
    else
        fail "$label (expected pattern: $pattern)"
        echo "    output: $output"
    fi
}

assert_not() {
    local output="$1" pattern="$2" label="$3"
    if echo "$output" | grep -qE "$pattern"; then
        fail "$label (unexpected pattern: $pattern)"
        echo "    output: $output"
    else
        pass "$label"
    fi
}

# Build with gcc
echo "=== Building with gcc ==="
mkdir -p "$BUILD_DIR"
for src in src/*.c; do
    name=$(basename "$src" .c)
    gcc -o "$BUILD_DIR/$name" "$src"
done
echo ""

# --- disk_usage ---
echo "=== disk_usage ==="
output=$("$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^--- event$"            "starts with --- event"
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field"
assert_has "$output" "^mount="                "mount field"
assert_has "$output" "^usage_percent="        "usage_percent field"
assert_has "$output" "^total="                "total field"
assert_has "$output" "^used="                 "used field"
assert_has "$output" "^available="            "available field"
echo ""

# --- disk_usage with low threshold ---
echo "=== disk_usage (threshold) ==="
output=$(HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=0 "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^type=(high_usage|critical_usage)$" "type is NOT ok with threshold=0"
assert_not "$output" "^type=ok$" "type should not be ok"
echo ""

# --- cpu_usage ---
echo "=== cpu_usage ==="
output=$(HEALTHCHECK_ARG_SAMPLE_MS=100 "$BUILD_DIR/cpu_usage" 2>&1)
assert_has "$output" "^--- event$"            "starts with --- event"
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field"
assert_has "$output" "^usage_percent="        "usage_percent field"
assert_has "$output" "^user_percent="         "user_percent field"
assert_has "$output" "^system_percent="       "system_percent field"
assert_has "$output" "^idle_percent="         "idle_percent field"
assert_has "$output" "^iowait_percent="       "iowait_percent field"
assert_has "$output" "^cores="                "cores field"
echo ""

# --- memory_usage ---
echo "=== memory_usage ==="
output=$("$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^--- event$"            "starts with --- event"
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field"
assert_has "$output" "^usage_percent="        "usage_percent field"
assert_has "$output" "^total="                "total field"
assert_has "$output" "^used="                 "used field"
assert_has "$output" "^available="            "available field"
assert_has "$output" "^swap_total="           "swap_total field"
assert_has "$output" "^swap_used="            "swap_used field"
assert_has "$output" "^swap_usage_percent="   "swap_usage_percent field"
echo ""

# --- ssh_journal ---
echo "=== ssh_journal ==="
mock_input=$(cat <<'MOCK'
{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1772539305000000"}
{"MESSAGE":"Server listening on 0.0.0.0 port 22","__REALTIME_TIMESTAMP":"1772539306000000"}
{"MESSAGE":"Accepted publickey for deploy from 192.168.1.100 port 44222 ssh2","__REALTIME_TIMESTAMP":"1772539307000000"}
{"MESSAGE":"Connection closed by invalid user test 1.2.3.4 port 55000 [preauth]","__REALTIME_TIMESTAMP":"1772539308000000"}
MOCK
)
output=$(echo "$mock_input" | "$BUILD_DIR/ssh_journal" 2>&1)

# Count events
event_count=$(echo "$output" | grep -c "^--- event$" || true)
if [ "$event_count" -eq 2 ]; then
    pass "exactly 2 events"
else
    fail "expected 2 events, got $event_count"
fi

assert_has "$output" "^type=failure$"         "has failure event"
assert_has "$output" "^type=login$"           "has login event"
assert_has "$output" "^user="                 "user field"
assert_has "$output" "^host="                 "host field"
assert_has "$output" "^timestamp="            "timestamp field"
echo ""

# --- Summary ---
echo "================================"
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
