#!/usr/bin/env bash
#
# test-output.sh - Verify compiled healthcheck binaries produce correct v2 protocol output.
#
# Compiles with gcc, runs each binary with various env configurations,
# validates output format, required fields, RAW mode, ADVANCED mode, and thresholds.
#
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

# Count occurrences of a pattern
count_matches() {
    echo "$1" | grep -cE "$2" || true
}

# Build with gcc
echo "=== Building with gcc ==="
mkdir -p "$BUILD_DIR"
for src in src/*.c; do
    name=$(basename "$src" .c)
    gcc -o "$BUILD_DIR/$name" "$src"
done
echo ""

# ============================================================
# disk_usage
# ============================================================

echo "=== disk_usage: default ==="
output=$("$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^--- event$"            "starts with --- event"
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field"
assert_has "$output" "^mount=/"               "mount field defaults to /"
assert_has "$output" "^usage_percent=[0-9]"   "usage_percent is numeric"
assert_has "$output" "^total="                "total field"
assert_has "$output" "^used="                 "used field"
assert_has "$output" "^available="            "available field"
# Default output should NOT have advanced fields
assert_not "$output" "^free="                 "no free field in default mode"
assert_not "$output" "^inodes="               "no inodes field in default mode"
# Exactly one event
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 1 ]; then pass "exactly 1 event"; else fail "expected 1 event, got $event_count"; fi
echo ""

echo "=== disk_usage: threshold warn ==="
output=$(HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=0 "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^type=(high_usage|critical_usage)$" "type is NOT ok with warn=0"
assert_not "$output" "^type=ok$" "type should not be ok"
echo ""

echo "=== disk_usage: threshold critical ==="
output=$(HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=0 HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT=0 "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^type=critical_usage$"  "type is critical with crit=0"
echo ""

echo "=== disk_usage: custom mount ==="
output=$(HEALTHCHECK_ARG_MOUNT=/tmp "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^mount=/tmp$"           "mount=/tmp when set"
echo ""

echo "=== disk_usage: RAW mode ==="
output=$(HEALTHCHECK_ARG_RAW=1 "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^total=[0-9]+$"         "total is raw integer"
assert_has "$output" "^used=[0-9]+$"          "used is raw integer"
assert_has "$output" "^available=[0-9]+$"     "available is raw integer"
echo ""

echo "=== disk_usage: ADVANCED mode ==="
output=$(HEALTHCHECK_ARG_ADVANCED=1 "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^free="                 "free field present"
assert_has "$output" "^inodes="               "inodes field present"
assert_has "$output" "^inodes_used="          "inodes_used field present"
assert_has "$output" "^inodes_free="          "inodes_free field present"
assert_has "$output" "^inodes_available="     "inodes_available field present"
assert_has "$output" "^inodes_usage_percent=" "inodes_usage_percent field present"
echo ""

echo "=== disk_usage: RAW + ADVANCED ==="
output=$(HEALTHCHECK_ARG_RAW=1 HEALTHCHECK_ARG_ADVANCED=1 "$BUILD_DIR/disk_usage" 2>&1)
assert_has "$output" "^free=[0-9]+$"          "free is raw integer"
echo ""

# ============================================================
# cpu_usage
# ============================================================

echo "=== cpu_usage: default ==="
output=$(HEALTHCHECK_ARG_SAMPLE_MS=100 "$BUILD_DIR/cpu_usage" 2>&1)
assert_has "$output" "^--- event$"            "starts with --- event"
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field"
assert_has "$output" "^usage_percent=[0-9]"   "usage_percent is numeric"
assert_has "$output" "^user_percent="         "user_percent field"
assert_has "$output" "^system_percent="       "system_percent field"
assert_has "$output" "^idle_percent="         "idle_percent field"
assert_has "$output" "^iowait_percent="       "iowait_percent field"
assert_has "$output" "^cores=[0-9]"           "cores is numeric"
# No advanced fields by default
assert_not "$output" "^nice_percent="         "no nice_percent in default mode"
assert_not "$output" "^irq_percent="          "no irq_percent in default mode"
assert_not "$output" "^procs_running="        "no procs_running in default mode"
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 1 ]; then pass "exactly 1 event"; else fail "expected 1 event, got $event_count"; fi
echo ""

echo "=== cpu_usage: threshold warn ==="
output=$(HEALTHCHECK_ARG_SAMPLE_MS=100 HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=0 "$BUILD_DIR/cpu_usage" 2>&1)
# With warn=0, any non-zero CPU usage triggers high_usage or critical_usage
# (could still be ok if truly idle, so just check it ran)
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field present"
echo ""

echo "=== cpu_usage: ADVANCED mode ==="
output=$(HEALTHCHECK_ARG_SAMPLE_MS=100 HEALTHCHECK_ARG_ADVANCED=1 "$BUILD_DIR/cpu_usage" 2>&1)
assert_has "$output" "^nice_percent="         "nice_percent field"
assert_has "$output" "^irq_percent="          "irq_percent field"
assert_has "$output" "^softirq_percent="      "softirq_percent field"
assert_has "$output" "^steal_percent="        "steal_percent field"
assert_has "$output" "^guest_percent="        "guest_percent field"
assert_has "$output" "^guest_nice_percent="   "guest_nice_percent field"
assert_has "$output" "^procs_running="        "procs_running field"
assert_has "$output" "^procs_blocked="        "procs_blocked field"
echo ""

# ============================================================
# memory_usage
# ============================================================

echo "=== memory_usage: default ==="
output=$("$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^--- event$"            "starts with --- event"
assert_has "$output" "^type=(ok|high_usage|critical_usage)$" "type field"
assert_has "$output" "^usage_percent=[0-9]"   "usage_percent is numeric"
assert_has "$output" "^total="                "total field"
assert_has "$output" "^used="                 "used field"
assert_has "$output" "^available="            "available field"
assert_has "$output" "^swap_total="           "swap_total field"
assert_has "$output" "^swap_used="            "swap_used field"
assert_has "$output" "^swap_usage_percent="   "swap_usage_percent field"
# No advanced fields by default
assert_not "$output" "^free="                 "no free field in default mode"
assert_not "$output" "^buffers="              "no buffers field in default mode"
assert_not "$output" "^cached="               "no cached field in default mode"
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 1 ]; then pass "exactly 1 event"; else fail "expected 1 event, got $event_count"; fi
echo ""

echo "=== memory_usage: threshold warn ==="
output=$(HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=0 "$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^type=(high_usage|critical_usage)$" "type is NOT ok with warn=0"
assert_not "$output" "^type=ok$" "type should not be ok"
echo ""

echo "=== memory_usage: threshold critical ==="
output=$(HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=0 HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT=0 "$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^type=critical_usage$"  "type is critical with crit=0"
echo ""

echo "=== memory_usage: RAW mode ==="
output=$(HEALTHCHECK_ARG_RAW=1 "$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^total=[0-9]+$"         "total is raw integer"
assert_has "$output" "^used=[0-9]+$"          "used is raw integer"
assert_has "$output" "^available=[0-9]+$"     "available is raw integer"
assert_has "$output" "^swap_total=[0-9]+$"    "swap_total is raw integer"
assert_has "$output" "^swap_used=[0-9]+$"     "swap_used is raw integer"
echo ""

echo "=== memory_usage: ADVANCED mode ==="
output=$(HEALTHCHECK_ARG_ADVANCED=1 "$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^free="                 "free field present"
assert_has "$output" "^buffers="              "buffers field present"
assert_has "$output" "^cached="               "cached field present"
assert_has "$output" "^swap_free="            "swap_free field present"
assert_has "$output" "^shared="               "shared field present"
assert_has "$output" "^slab="                 "slab field present"
echo ""

echo "=== memory_usage: RAW + ADVANCED ==="
output=$(HEALTHCHECK_ARG_RAW=1 HEALTHCHECK_ARG_ADVANCED=1 "$BUILD_DIR/memory_usage" 2>&1)
assert_has "$output" "^free=[0-9]+$"          "free is raw integer"
assert_has "$output" "^buffers=[0-9]+$"       "buffers is raw integer"
assert_has "$output" "^cached=[0-9]+$"        "cached is raw integer"
echo ""

# ============================================================
# ssh_journal
# ============================================================

echo "=== ssh_journal: mixed events ==="
mock_input=$(cat <<'MOCK'
{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1772539305000000"}
{"MESSAGE":"Server listening on 0.0.0.0 port 22","__REALTIME_TIMESTAMP":"1772539306000000"}
{"MESSAGE":"Accepted publickey for deploy from 192.168.1.100 port 44222 ssh2","__REALTIME_TIMESTAMP":"1772539307000000"}
{"MESSAGE":"Connection closed by invalid user test 1.2.3.4 port 55000 [preauth]","__REALTIME_TIMESTAMP":"1772539308000000"}
MOCK
)
output=$(echo "$mock_input" | "$BUILD_DIR/ssh_journal" 2>&1)
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 2 ]; then pass "exactly 2 events"; else fail "expected 2 events, got $event_count"; fi
assert_has "$output" "^type=failure$"         "has failure event"
assert_has "$output" "^type=login$"           "has login event"
assert_has "$output" "^user="                 "user field"
assert_has "$output" "^host="                 "host field"
assert_has "$output" "^timestamp="            "timestamp field"
assert_has "$output" "^timestamp=....-..-..T..:..:..Z$" "timestamp is ISO 8601"
echo ""

echo "=== ssh_journal: empty input ==="
output=$(echo "" | "$BUILD_DIR/ssh_journal" 2>&1)
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 0 ]; then pass "zero events on empty input"; else fail "expected 0 events, got $event_count"; fi
echo ""

echo "=== ssh_journal: no matching messages ==="
output=$(echo '{"MESSAGE":"Server listening on 0.0.0.0 port 22","__REALTIME_TIMESTAMP":"123"}' | "$BUILD_DIR/ssh_journal" 2>&1)
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 0 ]; then pass "zero events on non-matching input"; else fail "expected 0 events, got $event_count"; fi
echo ""

echo "=== ssh_journal: non-JSON input skipped ==="
output=$(printf 'not json\n{"MESSAGE":"Invalid user root from 1.1.1.1 port 22","__REALTIME_TIMESTAMP":"1000000"}\n' | "$BUILD_DIR/ssh_journal" 2>&1)
event_count=$(count_matches "$output" "^--- event$")
if [ "$event_count" -eq 1 ]; then pass "1 event, non-JSON line skipped"; else fail "expected 1 event, got $event_count"; fi
echo ""

echo "=== ssh_journal: all failure types ==="
mock_input=$(cat <<'MOCK'
{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1000000"}
{"MESSAGE":"Connection closed by authenticating user root 10.0.0.1 port 22 [preauth]","__REALTIME_TIMESTAMP":"2000000"}
MOCK
)
output=$(echo "$mock_input" | "$BUILD_DIR/ssh_journal" 2>&1)
event_count=$(count_matches "$output" "^type=failure$")
if [ "$event_count" -eq 2 ]; then pass "2 failure events"; else fail "expected 2 failure events, got $event_count"; fi
echo ""

echo "=== ssh_journal: all login types ==="
mock_input=$(cat <<'MOCK'
{"MESSAGE":"Accepted publickey for alice from 10.0.0.1 port 22 ssh2","__REALTIME_TIMESTAMP":"1000000"}
{"MESSAGE":"Accepted password for bob from 10.0.0.2 port 22 ssh2","__REALTIME_TIMESTAMP":"2000000"}
MOCK
)
output=$(echo "$mock_input" | "$BUILD_DIR/ssh_journal" 2>&1)
event_count=$(count_matches "$output" "^type=login$")
if [ "$event_count" -eq 2 ]; then pass "2 login events"; else fail "expected 2 login events, got $event_count"; fi
echo ""

echo "=== ssh_journal: ADVANCED mode ==="
output=$(echo '{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1000000","_HOSTNAME":"vps"}' \
    | HEALTHCHECK_ARG_ADVANCED=1 "$BUILD_DIR/ssh_journal" 2>&1)
assert_has "$output" "^_HOSTNAME=vps$"        "extra field emitted in advanced mode"
# MESSAGE and __REALTIME_TIMESTAMP should be skipped in extra fields
assert_not "$output" "^MESSAGE="              "MESSAGE not in extra output"
assert_not "$output" "^__REALTIME_TIMESTAMP=" "__REALTIME_TIMESTAMP not in extra output"
echo ""

# ============================================================
# Summary
# ============================================================

echo "================================"
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
