# Healthchecks — Testing on a real server

The `dev/` scripts build all healthchecks and run them on a live Debian 13 VPS
(Hetzner Cloud). This is the primary way to verify a binary behaves correctly before
pushing — containers don't replicate real system state (`/proc`, `/var/log/wtmp`,
journald, etc.).

## Setup

```sh
cp dev/.env.example dev/.env
```

| Variable            | Description                                                  |
|---------------------|--------------------------------------------------------------|
| `HETZNER_API_TOKEN` | Hetzner Cloud API token                                      |
| `SSH_KEY`           | Name of the SSH key registered in your Hetzner account       |
| `CC`                | Path to `cosmocc`. Auto-detected if not set.                 |

`dev/.env` is gitignored.

---

## `dev/test-on-vps.sh` — build and run all healthchecks

Builds all healthchecks, creates a fresh VPS, runs every binary the same way sznuper
would, prints full output, then deletes the VPS.

```sh
./dev/test-on-vps.sh
./dev/test-on-vps.sh --skip-build   # reuse existing build/
./dev/test-on-vps.sh --keep         # leave VPS running after tests
```

Each binary receives input matching its trigger type:

| Binary        | Trigger  | Input to binary                                                    |
|---------------|----------|--------------------------------------------------------------------|
| `cpu_usage`   | interval | none                                                               |
| `disk_usage`  | interval | none                                                               |
| `memory_usage`| interval | none                                                               |
| `ssh_login`   | interval | none                                                               |
| `ssh_btmp`    | watch    | `cat /var/log/btmp`                                                |
| `ssh_wtmp`    | watch    | `cat /var/log/wtmp`                                                |
| `ssh_journal` | pipe     | `journalctl -u ssh -u sshd --output=json --no-pager`               |

---

## `dev/test-ssh-journal.sh` — ssh_journal lifecycle test

Creates a VPS, generates real SSH events, then runs `ssh_journal` against actual
journald output. Verifies the full pipeline: event generation → journald capture →
JSON parsing → per-record output.

```sh
./dev/test-ssh-journal.sh
./dev/test-ssh-journal.sh --skip-build
./dev/test-ssh-journal.sh --keep
```

Lifecycle:

1. SSH as non-existent users (`admin`, `ubuntu`, `test`, `operator`, `deploy`) →
   sshd logs `Invalid user X from IP` in journald
2. SSH as `root` three times → sshd logs `Accepted publickey for root from IP`
3. Waits 3 seconds for journald to flush
4. Runs `ssh_journal` with `HEALTHCHECK_ARG_ALERT_ON=both` against `journalctl
   --no-pager` output

Expected output: 5 failure records + login records, all `status=warning`.

---

## Manual iteration

For iterating on a single binary without recreating the VPS each time:

```sh
# 1. Create a VPS and leave it running
./dev/test-on-vps.sh --keep
# → prints SERVER_ID=... SERVER_IP=...

# 2. Edit source, rebuild one binary
make build/ssh_journal CC=~/repos/cosmopolitan/cosmocc/bin/cosmocc

# 3. Run it on the server
./dev/run-binary.sh $SERVER_IP build/ssh_journal \
    -- "journalctl -u ssh -u sshd --output=json --no-pager | /root/ssh_journal"

# 4. Repeat 2–3

# 5. Delete the VPS
./dev/delete-server.sh $SERVER_ID
```

---

## Script reference

### `dev/run-binary.sh <IP> <BINARY> [-- REMOTE_CMD]`

Copies `BINARY` to `/root/<name>` on the server and runs it. Stdin passes through
to the remote process.

```sh
# Interval trigger — no stdin
./dev/run-binary.sh $SERVER_IP build/disk_usage

# Pipe trigger — pipe input from local machine
echo '{"MESSAGE":"Invalid user admin from 1.2.3.4 port 55000","__REALTIME_TIMESTAMP":"1772717618000000"}' \
    | ./dev/run-binary.sh $SERVER_IP build/ssh_journal

# Watch trigger — pipe file contents from the server via --
./dev/run-binary.sh $SERVER_IP build/ssh_btmp -- "cat /var/log/btmp | /root/ssh_btmp"

# With env var
./dev/run-binary.sh $SERVER_IP build/disk_usage -- "env HEALTHCHECK_ARG_RAW=1 /root/disk_usage"
```

Args after `--` replace the default remote command (`/root/<name>`). The binary is
always copied first.

### `dev/create-server.sh`

Creates a `cx23` Debian 13 server named `hc-e2e-test` in `fsn1`. Deletes any
existing server with the same name first. Prints `SERVER_ID=` and `SERVER_IP=` to
stdout once SSH is ready.

```sh
source <(./dev/create-server.sh)
# SERVER_ID and SERVER_IP are now in the environment
```

### `dev/delete-server.sh <SERVER_ID>`

Deletes the server. Idempotent — exits 0 if already gone.

```sh
./dev/delete-server.sh $SERVER_ID
```
