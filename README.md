# checks

Official checks for [Barker](https://github.com/barker-app/barker).

Each check is a standalone C binary compiled with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) — a single portable executable that runs on any Linux architecture (x86_64, aarch64) with zero external dependencies. No `df`, no `awk`, no runtime needed.

## Available checks

| Check | Description |
|---|---|
| `disk_usage` | Disk space usage for a given mount point |
| `cpu_usage` | CPU utilization over a sampling interval |
| `memory_usage` | Memory and swap usage |
| `ssh_login` | Detects SSH logins from `auth.log` (watch trigger) |
| `systemd_unit` | Checks if a systemd unit is active |

## Check interface

Every check follows the same contract:

- **Input:** environment variables (`BARKER_ARG_*` for user-defined args, `BARKER_TRIGGER` for trigger metadata) and optionally stdin (for watch triggers).
- **Output:** `key=value` pairs on stdout, one per line. The `status` key is required and must be `ok`, `warning`, or `critical`.

Example output from `disk_usage`:

```
status=warning
usage=84
available=8G
```

Each check documents its arguments, outputs, and status logic in a header comment.

## Writing your own checks

A check can be any executable — Go, Rust, Python, Bash, anything. It just needs to read its config from `BARKER_ARG_*` environment variables and print `key=value` pairs to stdout.

For checks you want to distribute, Cosmopolitan C is recommended for the same reason the official checks use it: one binary, every architecture, no dependencies.

See the [Barker spec](https://github.com/barker-app/barker) for the full check interface documentation.

## License

MIT
