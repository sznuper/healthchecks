# healthchecks

Official healthchecks for [Sznuper](https://github.com/sznuper/sznuper).

Each healthcheck is a standalone C binary compiled with [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) — a single portable executable that runs on any Linux architecture (x86_64, aarch64) with zero external dependencies. No `df`, no `awk`, no runtime needed.

## Available healthchecks

| Healthcheck | Description |
|---|---|
| `disk_usage` | Disk space usage for a given mount point |
| `cpu_usage` | CPU utilization over a sampling interval |
| `memory_usage` | Memory and swap usage |
| `ssh_journal` | Detects SSH login events from journald (pipe trigger) |

## Healthcheck interface

Every healthcheck follows the same contract:

- **Input:** environment variables (`HEALTHCHECK_ARG_*` for user-defined args, `HEALTHCHECK_TRIGGER` for trigger metadata) and optionally stdin (for pipe/watch triggers).
- **Output:** events on stdout using `--- event` delimiter, followed by `key=value` pairs. The `type` field is required in every event.

Example output from `disk_usage`:

```
--- event
type=high_usage
mount=/
usage_percent=84.3
available=8G
```

Each healthcheck documents its arguments, outputs, and event type logic in a header comment.

## Writing your own healthchecks

A healthcheck can be any executable — Go, Rust, Python, Bash, anything. It just needs to read its config from `HEALTHCHECK_ARG_*` environment variables and print `--- event` blocks with `type=` and `key=value` fields to stdout.

For healthchecks you want to distribute, Cosmopolitan C is recommended for the same reason the official healthchecks use it: one binary, every architecture, no dependencies.

See the [Sznuper spec](https://github.com/sznuper/sznuper) for the full healthcheck interface documentation.

## License

MIT
