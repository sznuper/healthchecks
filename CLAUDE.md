# Barker Checks

Official portable checks for [Barker](https://github.com/barker-app/barker), a monitoring daemon for Linux servers.

## What this repo is

Each check is a standalone C program compiled with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) into an Actually Portable Executable — one binary that runs on any Linux architecture (x86_64, aarch64) with zero external dependencies.

## Check interface contract

Every check follows this protocol:

**Input:**
- Args via environment variables: `BARKER_ARG_<KEY>` (uppercase). Config keys like `threshold_warn` become `BARKER_ARG_THRESHOLD_WARN`.
- Daemon metadata: `BARKER_TRIGGER` (always), `BARKER_FILE` and `BARKER_LINE_COUNT` (watch triggers only).
- Stdin: new lines from watched file (watch triggers) or empty (interval/cron).

**Output:**
- `key=value` pairs on stdout, one per line, lowercase keys.
- `status` key is **required**: must be `ok`, `warning`, or `critical`.
- Split on first `=` only. Lines without `=` are ignored.

Example output:
```
status=warning
usage=84
available=8G
```

## Project structure

```
checks/
  disk_usage.c
  cpu_usage.c
  memory_usage.c
  ssh_login.c
  systemd_unit.c
Makefile
```

## Build

Requires the `cosmocc` toolchain. See https://cosmo.zip/pub/cosmocc/ for setup.

```bash
make            # build all checks
make disk_usage # build one check
make clean
```

Each check compiles to a single portable binary with no libc dependency.

## Writing a check

1. Use direct syscalls or Cosmopolitan's libc — no shelling out to `df`, `awk`, `bc`, etc.
2. Read args from `getenv("BARKER_ARG_<KEY>")`.
3. Print `key=value` pairs to stdout. Always print `status=ok|warning|critical`.
4. Exit 0 on success. Non-zero exit with no `status` output = broken check (daemon logs error, never notifies).
5. Keep it minimal — one check, one concern, one file.

## Conventions

- One `.c` file per check, no shared libraries.
- Use `printf("key=value\n")` for output — no JSON, no fancy formatting.
- Document args, outputs, and status logic in a comment block at the top of each file.
- Threshold args should be floats (0.0–1.0) for percentages.
- Human-readable values in output (e.g., `available=8G` not `available=8589934592`).

## Testing

Run a check manually by setting its env vars:

```bash
BARKER_ARG_THRESHOLD_WARN=0.80 BARKER_ARG_THRESHOLD_CRIT=0.95 BARKER_ARG_MOUNT=/ ./disk_usage
```

Verify output is valid `key=value` lines with a `status` key.

## Do not

- Do not add runtime dependencies (Python, Bash, Node, etc.).
- Do not use `system()` or `popen()` to call external commands.
- Do not print anything to stderr in normal operation (stderr is for fatal errors only).
- Do not output keys with `BARKER_` prefix — that namespace is reserved for the daemon.
