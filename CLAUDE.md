# Sznuper Healthchecks

Official portable healthchecks for [Sznuper](https://github.com/sznuper/sznuper), a monitoring daemon for Linux servers.

## What this repo is

Each healthcheck is a standalone C program compiled with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) into an Actually Portable Executable — one binary that runs on any Linux architecture (x86_64, aarch64) with zero external dependencies.

## Healthcheck interface contract

Every healthcheck follows this protocol:

**Input:**
- Args via environment variables: `HEALTHCHECK_ARG_<KEY>` (uppercase). Config keys like `threshold_warn_percent` become `HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT`.
- Daemon metadata: `HEALTHCHECK_TRIGGER` (always), `HEALTHCHECK_FILE` and `HEALTHCHECK_LINE_COUNT` (watch triggers only).
- Stdin: new bytes from watched file (watch triggers), stdout of pipe command (pipe triggers), or empty (interval/cron).

**Output:**
- `key=value` pairs on stdout, one per line, lowercase keys.
- `status` key is **required** in every record: must be `ok`, `warning`, or `critical`.
- Split on first `=` only. Lines without `=` are ignored.

**Single-record output** (interval/cron/watch healthchecks):
```
status=warning
usage_percent=84.3
available=8G
```

**Multi-record output** (pipe healthchecks that process a batch of events):

Two structural tokens control the format, matched as exact trimmed lines:

| Token          | Meaning                                              |
|----------------|------------------------------------------------------|
| `--- records`  | Ends the global props section; starts the records array |
| `--- record`   | Starts the next record within the array              |

```
event_count=3
failure_count=2
login_count=1
--- records
status=warning
event=failure
user=admin
host=1.2.3.4
timestamp=2026-03-05T13:55:05Z
--- record
status=warning
event=failure
user=test
host=1.2.3.4
timestamp=2026-03-05T13:55:06Z
--- record
status=ok
event=login
user=root
host=83.22.197.254
timestamp=2026-03-05T13:55:07Z
```

Everything before `--- records` is the **global section** — batch-level context (counts,
summaries). It is not itself a notification; its fields are merged into every record's
template data. Each block after `--- records` / `--- record` is an independent record
processed through its own status check, cooldown, and template render.

## Project structure

```
src/
  sznuper.h         # shared utilities (header-only, no link dependency)
  cpu_usage.c       # interval trigger
  disk_usage.c      # interval trigger
  memory_usage.c    # interval trigger
  ssh_journal.c     # pipe trigger   — journalctl --output=json on stdin
Makefile
docs/
  testing.md        # VPS testing with dev/ scripts
dev/
  .env.example
  create-server.sh
  delete-server.sh
  run-binary.sh
  test-on-vps.sh
  test-ssh-journal.sh
```

## Build

Requires the `cosmocc` toolchain. See https://cosmo.zip/pub/cosmocc/ for setup.

```bash
make                    # build all healthchecks
make build/disk_usage   # build one healthcheck
make clean
```

Each healthcheck compiles to a single portable binary with no libc dependency.

## Writing a healthcheck

1. Use direct syscalls or Cosmopolitan's libc — no shelling out to `df`, `awk`, `bc`, etc.
2. Read args from `getenv("HEALTHCHECK_ARG_<KEY>")`.
3. Print `key=value` pairs to stdout. Always print `status=ok|warning|critical`.
4. Exit 0 on success. Non-zero exit with no `status` output = broken healthcheck (daemon logs error, never notifies).
5. Keep it minimal — one healthcheck, one concern, one file.

## Conventions

- One `.c` file per healthcheck, no shared libraries. Common utilities live in `sznuper.h` (header-only, `static inline`).
- Use `printf("key=value\n")` for output — no JSON, no fancy formatting.
- Document args, outputs, and status logic in a comment block at the top of each file.
- Threshold args should be floats in 0-100 range for percentages.
- Percentage output keys use the `_percent` suffix as floats in 0-100 range (e.g., `usage_percent=84.3`, `swap_usage_percent=12.0`).
- Human-readable values in output by default (e.g., `available=8G`). When `HEALTHCHECK_ARG_RAW` is set, emit raw byte integers instead (e.g., `available=8589934592`).

## Standard optional args

These args are recognized across all healthchecks for consistent behavior:

- `HEALTHCHECK_ARG_RAW` — When set (non-empty), byte-valued fields emit raw integers instead of human-readable strings. Percentages and counts are unaffected. No effect on healthchecks without byte fields (e.g., cpu_usage).
- `HEALTHCHECK_ARG_ADVANCED` — When set (non-empty), emit the full set of output fields. Default output includes only the essential subset for each healthcheck.

## Testing

Run a healthcheck manually by setting its env vars:

```bash
HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT=80 HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT=95 HEALTHCHECK_ARG_MOUNT=/ ./build/disk_usage
```

Verify output is valid `key=value` lines with a `status` key. For testing on a real
server see `docs/testing.md`.

## Do not

- Do not add runtime dependencies (Python, Bash, Node, etc.).
- Do not use `system()` or `popen()` to call external commands.
- Do not print anything to stderr in normal operation (stderr is for fatal errors only).
- Do not output keys with `HEALTHCHECK_` prefix — that namespace is reserved for the daemon.
