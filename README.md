# healthchecks

Official healthchecks for [Sznuper](https://github.com/sznuper/sznuper).

Each healthcheck is a standalone C binary compiled with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) — a single portable executable that runs on any Linux architecture (x86_64, aarch64) with zero external dependencies. No `df`, no `awk`, no runtime needed.

## Available healthchecks

| Healthcheck | Description |
|---|---|
| `disk_usage` | Disk space usage for a given mount point |
| `cpu_usage` | CPU utilization over a sampling interval |
| `memory_usage` | Memory and swap usage |
| `ssh_login` | Detects SSH logins from `auth.log` (watch trigger) |
| `systemd_unit` | Checks if a systemd unit is active |

## Healthcheck interface

Every healthcheck follows the same contract:

- **Input:** environment variables (`HEALTHCHECK_ARG_*` for user-defined args, `HEALTHCHECK_TRIGGER` for trigger metadata) and optionally stdin (for watch triggers).
- **Output:** `key=value` pairs on stdout, one per line. The `status` key is required and must be `ok`, `warning`, or `critical`.

Example output from `disk_usage`:

```
status=warning
usage=84
available=8G
```

Each healthcheck documents its arguments, outputs, and status logic in a header comment.

## Writing your own healthchecks

A healthcheck can be any executable — Go, Rust, Python, Bash, anything. It just needs to read its config from `HEALTHCHECK_ARG_*` environment variables and print `key=value` pairs to stdout.

For healthchecks you want to distribute, Cosmopolitan C is recommended for the same reason the official healthchecks use it: one binary, every architecture, no dependencies.

See the [Sznuper spec](https://github.com/sznuper/sznuper) for the full healthcheck interface documentation.

## License

MIT
