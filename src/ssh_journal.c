/*
 * ssh_journal - Detect SSH auth events from journalctl --output=json stdin.
 *
 * Reads stdin line by line (journalctl -f --output=json output). Parses SSH
 * failure, login, and logout events in real time. Designed for the pipe trigger:
 *
 *   - name: ssh_journal
 *     healthcheck: file://ssh_journal
 *     trigger:
 *       pipe: "journalctl -f --since=now SYSLOG_FACILITY=10 SYSLOG_FACILITY=4 --output=json --output-fields=MESSAGE,__REALTIME_TIMESTAMP --no-pager"
 *     template: "SSH {{event.type}} from {{event.host}} as {{event.user}}"
 *     cooldown: 5m
 *     notify:
 *       - telegram
 *     events:
 *       on_unmatched: drop
 *       override:
 *         login: {}
 *         logout: {}
 *
 * Uses facility-based journalctl filtering (SYSLOG_FACILITY=10 for auth,
 * SYSLOG_FACILITY=4 for auth-priv) instead of unit-based (-u ssh -u sshd)
 * to capture events from sshd-session (e.g. disconnects).
 * See: https://unix.stackexchange.com/a/401398
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_ADVANCED - When set, emit all extra JSON fields per event
 *
 * Input format: JSON lines from journalctl --output=json.
 * Required fields per line: MESSAGE, __REALTIME_TIMESTAMP.
 * Non-JSON lines (not starting with '{') are silently skipped.
 *
 * Patterns matched (no double-counting):
 *   "Invalid user USER from HOST"                                -> failure
 *   "Connection closed by authenticating user USER HOST port N [preauth]" -> failure
 *   "Accepted publickey for USER from HOST"                      -> login
 *   "Accepted password for USER from HOST"                       -> login
 *   "Disconnected from user USER HOST port N"                    -> logout
 *
 * Note: "Connection closed by invalid user" is always paired with "Invalid user"
 * — it is skipped to avoid double-counting.
 *
 * Output (per matched event):
 *   --- event
 *   type=failure|login|logout
 *   user=USER
 *   host=HOST
 *   timestamp=YYYY-MM-DDTHH:MM:SSZ
 *   [extra fields if advanced=1]
 *
 * Output (no matches):
 *   (empty — zero events)
 */

#include "sznuper.h"
#include <time.h>

#define MAX_LINE   4096
#define MAX_EVENTS 256
#define MAX_STR    256

/* Parsed event */
#define EV_FAILURE 0
#define EV_LOGIN   1
#define EV_LOGOUT  2

struct event {
    int  type; /* EV_FAILURE, EV_LOGIN, or EV_LOGOUT */
    char user[MAX_STR];
    char host[MAX_STR];
    char timestamp[32];      /* "2026-03-05T13:55:05Z" */
    char raw_json[MAX_LINE]; /* full JSON line, stored only when advanced=1 */
};

/*
 * Extract a JSON string value for the given key from a compact JSON object line.
 * Pattern: "KEY":"VALUE"
 * Handles escape sequences: \", \\, \/, \n, \r, \t.
 * Returns 1 on success, 0 if key not found or buffer too small.
 */
static int json_str(const char *json, const char *key, char *buf, size_t bufsz) {
    char pat[MAX_STR + 4];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p)
        return 0;
    p += strlen(pat);

    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p)
                break;
            char c;
            switch (*p) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                default:   c = *p;   break;
            }
            if (i + 1 >= bufsz)
                return 0;
            buf[i++] = c;
        } else {
            if (i + 1 >= bufsz)
                return 0;
            buf[i++] = *p;
        }
        p++;
    }
    if (*p != '"')
        return 0;
    buf[i] = '\0';
    return 1;
}

/*
 * Convert __REALTIME_TIMESTAMP (microseconds since epoch, decimal string)
 * to "YYYY-MM-DDTHH:MM:SSZ". Writes to buf.
 */
static void format_ts(const char *usec_str, char *buf, size_t bufsz) {
    unsigned long long usec = strtoull(usec_str, NULL, 10);
    time_t sec = (time_t)(usec / 1000000ULL);
    struct tm *tm = gmtime(&sec);
    if (!tm) {
        strncpy(buf, usec_str, bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }
    strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%SZ", tm);
}

/*
 * Emit all "KEY":"VALUE" pairs in the JSON line, skipping keys in skip[].
 * Key names are emitted as-is. Non-string values are skipped silently.
 */
static void emit_extra_fields(const char *json, const char **skip, int n_skip) {
    const char *p = json;
    if (*p == '{') p++;

    while (*p && *p != '}') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '"') break;
        p++; /* skip opening quote of key */

        const char *key_start = p;
        while (*p && *p != '"') p++;
        if (*p != '"') break;
        size_t klen = (size_t)(p - key_start);
        p++; /* skip closing quote of key */

        while (*p == ' ') p++;
        if (*p != ':') break;
        p++;
        while (*p == ' ') p++;

        if (*p != '"') {
            /* Non-string value — skip to next separator */
            while (*p && *p != ',' && *p != '}') p++;
            if (*p == ',') p++;
            continue;
        }
        p++; /* skip opening quote of value */

        char val[MAX_LINE];
        size_t vi = 0;
        while (*p && *p != '"') {
            if (*p == '\\') {
                p++;
                if (!*p) break;
                char c;
                switch (*p) {
                    case '"':  c = '"';  break;
                    case '\\': c = '\\'; break;
                    case '/':  c = '/';  break;
                    case 'n':  c = '\n'; break;
                    case 'r':  c = '\r'; break;
                    case 't':  c = '\t'; break;
                    default:   c = *p;   break;
                }
                if (vi + 1 < sizeof(val))
                    val[vi++] = c;
            } else {
                if (vi + 1 < sizeof(val))
                    val[vi++] = *p;
            }
            p++;
        }
        if (*p == '"') p++; /* skip closing quote of value */
        val[vi] = '\0';

        int should_skip = 0;
        for (int i = 0; i < n_skip; i++) {
            if (strlen(skip[i]) == klen && memcmp(skip[i], key_start, klen) == 0) {
                should_skip = 1;
                break;
            }
        }
        if (!should_skip) {
            char key[MAX_STR];
            if (klen >= MAX_STR) klen = MAX_STR - 1;
            memcpy(key, key_start, klen);
            key[klen] = '\0';
            printf("%s=%s\n", key, val);
        }

        while (*p == ' ') p++;
        if (*p == ',') p++;
    }
}

/*
 * Parse one SSH log message (the MESSAGE field content).
 * Returns 1 if matched, 0 otherwise. Fills ev->type, ev->user, ev->host on match.
 *
 * Patterns:
 *   "Invalid user USER from HOST"
 *   "Connection closed by authenticating user USER HOST port N [preauth]"
 *   "Accepted publickey for USER from HOST"
 *   "Accepted password for USER from HOST"
 *   "Disconnected from user USER HOST port N"
 *
 * Skipped (double-count avoidance):
 *   "Connection closed by invalid user ..."
 */
static int parse_message(const char *line, struct event *ev) {
    const char *p;

    /* --- failure: Invalid user USER from HOST --- */
    p = strstr(line, "Invalid user ");
    if (p) {
        if (strstr(line, "Connection closed by invalid user"))
            return 0;

        p += strlen("Invalid user ");
        const char *user_end = strchr(p, ' ');
        if (!user_end) return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR) return 0;

        const char *from = strstr(user_end, " from ");
        if (!from) return 0;
        from += strlen(" from ");
        const char *host_end = strchr(from, ' ');
        size_t hlen = host_end ? (size_t)(host_end - from) : strlen(from);
        if (hlen == 0 || hlen >= MAX_STR) return 0;

        ev->type = EV_FAILURE;
        memcpy(ev->user, p, ulen); ev->user[ulen] = '\0';
        memcpy(ev->host, from, hlen); ev->host[hlen] = '\0';
        return 1;
    }

    /* --- failure: Connection closed by authenticating user USER HOST port N [preauth] --- */
    p = strstr(line, "Connection closed by authenticating user ");
    if (p) {
        p += strlen("Connection closed by authenticating user ");
        const char *user_end = strchr(p, ' ');
        if (!user_end) return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR) return 0;

        const char *host_start = user_end + 1;
        const char *host_end   = strchr(host_start, ' ');
        size_t hlen = host_end ? (size_t)(host_end - host_start) : strlen(host_start);
        if (hlen == 0 || hlen >= MAX_STR) return 0;

        if (!strstr(line, "[preauth]")) return 0;

        ev->type = EV_FAILURE;
        memcpy(ev->user, p, ulen); ev->user[ulen] = '\0';
        memcpy(ev->host, host_start, hlen); ev->host[hlen] = '\0';
        return 1;
    }

    /* --- login: Accepted publickey for USER from HOST --- */
    p = strstr(line, "Accepted publickey for ");
    if (p) {
        p += strlen("Accepted publickey for ");
        const char *user_end = strchr(p, ' ');
        if (!user_end) return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR) return 0;

        const char *from = strstr(user_end, " from ");
        if (!from) return 0;
        from += strlen(" from ");
        const char *host_end = strchr(from, ' ');
        size_t hlen = host_end ? (size_t)(host_end - from) : strlen(from);
        if (hlen == 0 || hlen >= MAX_STR) return 0;

        ev->type = EV_LOGIN;
        memcpy(ev->user, p, ulen); ev->user[ulen] = '\0';
        memcpy(ev->host, from, hlen); ev->host[hlen] = '\0';
        return 1;
    }

    /* --- login: Accepted password for USER from HOST --- */
    p = strstr(line, "Accepted password for ");
    if (p) {
        p += strlen("Accepted password for ");
        const char *user_end = strchr(p, ' ');
        if (!user_end) return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR) return 0;

        const char *from = strstr(user_end, " from ");
        if (!from) return 0;
        from += strlen(" from ");
        const char *host_end = strchr(from, ' ');
        size_t hlen = host_end ? (size_t)(host_end - from) : strlen(from);
        if (hlen == 0 || hlen >= MAX_STR) return 0;

        ev->type = EV_LOGIN;
        memcpy(ev->user, p, ulen); ev->user[ulen] = '\0';
        memcpy(ev->host, from, hlen); ev->host[hlen] = '\0';
        return 1;
    }

    /* --- logout: Disconnected from user USER HOST port N --- */
    p = strstr(line, "Disconnected from user ");
    if (p) {
        p += strlen("Disconnected from user ");
        const char *user_end = strchr(p, ' ');
        if (!user_end) return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR) return 0;

        const char *host_start = user_end + 1;
        const char *host_end   = strchr(host_start, ' ');
        size_t hlen = host_end ? (size_t)(host_end - host_start) : strlen(host_start);
        if (hlen == 0 || hlen >= MAX_STR) return 0;

        ev->type = EV_LOGOUT;
        memcpy(ev->user, p, ulen); ev->user[ulen] = '\0';
        memcpy(ev->host, host_start, hlen); ev->host[hlen] = '\0';
        return 1;
    }

    return 0;
}

int main(void) {
    int advanced = parse_bool("HEALTHCHECK_ARG_ADVANCED");

    static struct event events[MAX_EVENTS];
    int event_count = 0;
    int validated   = 0; /* set after first JSON line passes field validation */

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0)
            continue;

        /* Skip non-JSON lines silently */
        if (line[0] != '{')
            continue;

        /* Validate required fields on first JSON line encountered */
        if (!validated) {
            char check[MAX_STR];
            if (!json_str(line, "MESSAGE", check, sizeof(check))) {
                fprintf(stderr, "ssh_journal: JSON input missing required field: MESSAGE\n");
                return 1;
            }
            if (!json_str(line, "__REALTIME_TIMESTAMP", check, sizeof(check))) {
                fprintf(stderr, "ssh_journal: JSON input missing required field: __REALTIME_TIMESTAMP\n");
                return 1;
            }
            validated = 1;
        }

        char msg[MAX_LINE];
        char ts_raw[MAX_STR];
        if (!json_str(line, "MESSAGE", msg, sizeof(msg)))
            continue;
        if (!json_str(line, "__REALTIME_TIMESTAMP", ts_raw, sizeof(ts_raw)))
            continue;

        struct event ev;
        if (!parse_message(msg, &ev))
            continue;

        format_ts(ts_raw, ev.timestamp, sizeof(ev.timestamp));

        if (advanced) {
            strncpy(ev.raw_json, line, MAX_LINE - 1);
            ev.raw_json[MAX_LINE - 1] = '\0';
        }

        if (event_count < MAX_EVENTS)
            events[event_count++] = ev;
    }

    /* Empty output = zero events (valid in v2 protocol) */
    if (event_count == 0)
        return 0;

    const char *skip_keys[] = {"MESSAGE", "__REALTIME_TIMESTAMP"};

    for (int i = 0; i < event_count; i++) {
        printf("--- event\n");
        const char *type_str = events[i].type == EV_FAILURE ? "failure"
                             : events[i].type == EV_LOGIN   ? "login"
                             :                                 "logout";
        printf("type=%s\n", type_str);
        printf("user=%s\n", events[i].user);
        printf("host=%s\n", events[i].host);
        printf("timestamp=%s\n", events[i].timestamp);
        if (advanced)
            emit_extra_fields(events[i].raw_json, skip_keys, 2);
    }

    return 0;
}
