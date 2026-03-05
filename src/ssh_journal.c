/*
 * ssh_journal - Detect SSH auth events from journalctl --output=cat stdin.
 *
 * Reads stdin line by line (journalctl -f --output=cat output). Parses SSH
 * failure and login events in real time. Designed for the pipe trigger:
 *
 *   - name: ssh_failures
 *     healthcheck: file://ssh_journal
 *     trigger:
 *       pipe: "journalctl -f -u ssh -u sshd --output=cat --no-pager"
 *     args:
 *       alert_on: failure
 *       threshold_warn_count: 1
 *       threshold_crit_count: 20
 *     template: |
 *       SSH failure: {{ healthcheck.user }} from {{ healthcheck.host }}
 *     notify:
 *       - telegram
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_ALERT_ON              - Which events trigger non-ok status:
 *                                           "failure" (default): warn/critical on failures
 *                                           "login":  warning on accepted logins
 *                                           "both":   either condition triggers non-ok
 *   HEALTHCHECK_ARG_THRESHOLD_WARN_COUNT  - Failure count for warning  (default: 1)
 *   HEALTHCHECK_ARG_THRESHOLD_CRIT_COUNT  - Failure count for critical (default: 20)
 *
 * Patterns matched (no double-counting):
 *   "Invalid user USER from HOST"                                -> failure
 *   "Connection closed by authenticating user USER HOST port N [preauth]" -> failure
 *   "Accepted publickey for USER from HOST"                      -> login
 *   "Accepted password for USER from HOST"                       -> login
 *
 * Note: "Connection closed by invalid user" is always paired with "Invalid user"
 * — it is skipped to avoid double-counting.
 *
 * Output (events matched):
 *   status=warning|critical
 *   event_count=N
 *   failure_count=N
 *   login_count=N
 *   event=failure          (first matching event type)
 *   user=USER
 *   host=HOST
 *   events=["failure","login"]
 *   users=["admin","root"]
 *   hosts=["1.2.3.4","5.6.7.8"]
 *
 * Output (no matches):
 *   status=ok
 *   event_count=0
 *   failure_count=0
 *   login_count=0
 *
 * Status logic (alert_on=failure):
 *   failure_count >= threshold_crit_count -> critical
 *   failure_count >= threshold_warn_count -> warning
 *   otherwise                             -> ok
 *
 * Status logic (alert_on=login):
 *   login_count > 0                       -> warning
 *   otherwise                             -> ok
 *
 * Status logic (alert_on=both):
 *   failure_count >= threshold_crit_count -> critical
 *   failure_count >= threshold_warn_count OR login_count > 0 -> warning
 *   otherwise                             -> ok
 */

#include "sznuper.h"

#define MAX_EVENTS 1024
#define MAX_STR    256

/* Parsed event */
#define EV_FAILURE 0
#define EV_LOGIN   1

struct event {
    int  type; /* EV_FAILURE or EV_LOGIN */
    char user[MAX_STR];
    char host[MAX_STR];
};

/* Dynamic string set (deduplication for output arrays) */
#define MAX_UNIQUE 256

struct strset {
    char items[MAX_UNIQUE][MAX_STR];
    int  count;
};

static int strset_add(struct strset *s, const char *str) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], str) == 0)
            return 0; /* already present */
    }
    if (s->count >= MAX_UNIQUE)
        return 0;
    strncpy(s->items[s->count], str, MAX_STR - 1);
    s->items[s->count][MAX_STR - 1] = '\0';
    s->count++;
    return 1;
}

/*
 * Emit a JSON string array: key=["a","b","c"]
 * Items are JSON-escaped minimally (backslash and double-quote only).
 */
static void emit_strarray(const char *key, struct strset *s) {
    printf("%s=[", key);
    for (int i = 0; i < s->count; i++) {
        if (i > 0)
            putchar(',');
        putchar('"');
        for (const char *p = s->items[i]; *p; p++) {
            if (*p == '"' || *p == '\\')
                putchar('\\');
            putchar(*p);
        }
        putchar('"');
    }
    puts("]");
}

/*
 * Parse one journalctl --output=cat line.
 * Returns 1 if matched, 0 otherwise. Fills ev on match.
 *
 * Patterns:
 *   "Invalid user USER from HOST"
 *   "Connection closed by authenticating user USER HOST port N [preauth]"
 *   "Accepted publickey for USER from HOST"
 *   "Accepted password for USER from HOST"
 *
 * Skipped (double-count avoidance):
 *   "Connection closed by invalid user ..."
 */
static int parse_line(const char *line, struct event *ev) {
    const char *p;

    /* --- failure: Invalid user USER from HOST --- */
    p = strstr(line, "Invalid user ");
    if (p) {
        /* Skip "Connection closed by invalid user" variant */
        if (strstr(line, "Connection closed by invalid user"))
            return 0;

        p += strlen("Invalid user ");
        /* USER is up to next space */
        const char *user_end = strchr(p, ' ');
        if (!user_end)
            return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR)
            return 0;

        /* " from HOST" */
        const char *from = strstr(user_end, " from ");
        if (!from)
            return 0;
        from += strlen(" from ");
        const char *host_end = strchr(from, ' ');
        size_t hlen = host_end ? (size_t)(host_end - from) : strlen(from);
        if (hlen == 0 || hlen >= MAX_STR)
            return 0;

        ev->type = EV_FAILURE;
        memcpy(ev->user, p, ulen);
        ev->user[ulen] = '\0';
        memcpy(ev->host, from, hlen);
        ev->host[hlen] = '\0';
        return 1;
    }

    /* --- failure: Connection closed by authenticating user USER HOST port N [preauth] --- */
    p = strstr(line, "Connection closed by authenticating user ");
    if (p) {
        p += strlen("Connection closed by authenticating user ");
        const char *user_end = strchr(p, ' ');
        if (!user_end)
            return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR)
            return 0;

        /* HOST follows USER, terminated by space */
        const char *host_start = user_end + 1;
        const char *host_end   = strchr(host_start, ' ');
        size_t hlen = host_end ? (size_t)(host_end - host_start) : strlen(host_start);
        if (hlen == 0 || hlen >= MAX_STR)
            return 0;

        /* Must have [preauth] somewhere after */
        if (!strstr(line, "[preauth]"))
            return 0;

        ev->type = EV_FAILURE;
        memcpy(ev->user, p, ulen);
        ev->user[ulen] = '\0';
        memcpy(ev->host, host_start, hlen);
        ev->host[hlen] = '\0';
        return 1;
    }

    /* --- login: Accepted publickey for USER from HOST --- */
    p = strstr(line, "Accepted publickey for ");
    if (p) {
        p += strlen("Accepted publickey for ");
        const char *user_end = strchr(p, ' ');
        if (!user_end)
            return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR)
            return 0;

        const char *from = strstr(user_end, " from ");
        if (!from)
            return 0;
        from += strlen(" from ");
        const char *host_end = strchr(from, ' ');
        size_t hlen = host_end ? (size_t)(host_end - from) : strlen(from);
        if (hlen == 0 || hlen >= MAX_STR)
            return 0;

        ev->type = EV_LOGIN;
        memcpy(ev->user, p, ulen);
        ev->user[ulen] = '\0';
        memcpy(ev->host, from, hlen);
        ev->host[hlen] = '\0';
        return 1;
    }

    /* --- login: Accepted password for USER from HOST --- */
    p = strstr(line, "Accepted password for ");
    if (p) {
        p += strlen("Accepted password for ");
        const char *user_end = strchr(p, ' ');
        if (!user_end)
            return 0;
        size_t ulen = (size_t)(user_end - p);
        if (ulen == 0 || ulen >= MAX_STR)
            return 0;

        const char *from = strstr(user_end, " from ");
        if (!from)
            return 0;
        from += strlen(" from ");
        const char *host_end = strchr(from, ' ');
        size_t hlen = host_end ? (size_t)(host_end - from) : strlen(from);
        if (hlen == 0 || hlen >= MAX_STR)
            return 0;

        ev->type = EV_LOGIN;
        memcpy(ev->user, p, ulen);
        ev->user[ulen] = '\0';
        memcpy(ev->host, from, hlen);
        ev->host[hlen] = '\0';
        return 1;
    }

    return 0;
}

int main(void) {
    const char *alert_on  = parse_string("HEALTHCHECK_ARG_ALERT_ON", "failure");
    long thresh_warn      = parse_int("HEALTHCHECK_ARG_THRESHOLD_WARN_COUNT", 1);
    long thresh_crit      = parse_int("HEALTHCHECK_ARG_THRESHOLD_CRIT_COUNT", 20);

    int do_alert_failure = (strcmp(alert_on, "failure") == 0 ||
                            strcmp(alert_on, "both")    == 0);
    int do_alert_login   = (strcmp(alert_on, "login")   == 0 ||
                            strcmp(alert_on, "both")    == 0);

    struct event events[MAX_EVENTS];
    int          event_count   = 0;
    long         failure_count = 0;
    long         login_count   = 0;

    /* First matched event (for scalar event= / user= / host= output) */
    int          first_set  = 0;
    int          first_type = EV_FAILURE;
    char         first_user[MAX_STR] = "";
    char         first_host[MAX_STR] = "";

    struct strset event_types = {0};
    struct strset users       = {0};
    struct strset hosts       = {0};

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0)
            continue;

        struct event ev;
        if (!parse_line(line, &ev))
            continue;

        if (ev.type == EV_FAILURE)
            failure_count++;
        else
            login_count++;

        if (!first_set) {
            first_set  = 1;
            first_type = ev.type;
            strncpy(first_user, ev.user, MAX_STR - 1);
            strncpy(first_host, ev.host, MAX_STR - 1);
        }

        strset_add(&event_types, ev.type == EV_FAILURE ? "failure" : "login");
        strset_add(&users, ev.user);
        strset_add(&hosts, ev.host);

        if (event_count < MAX_EVENTS)
            events[event_count++] = ev;
    }

    if (event_count == 0) {
        /* No events: single-record ok output (no separators). */
        printf("status=ok\n");
        printf("event_count=0\n");
        printf("failure_count=0\n");
        printf("login_count=0\n");
        return 0;
    }

    /*
     * Determine per-record status for failure and login events based on
     * the batch totals. Individual records inherit the batch severity.
     */
    const char *failure_status = "ok";
    if (do_alert_failure) {
        if (failure_count >= thresh_crit)
            failure_status = "critical";
        else if (failure_count >= thresh_warn)
            failure_status = "warning";
    }
    const char *login_status = do_alert_login ? "warning" : "ok";

    /* Global section: batch-level context. */
    printf("event_count=%d\n", event_count);
    printf("failure_count=%ld\n", failure_count);
    printf("login_count=%ld\n", login_count);

    /* Records array. */
    printf("--- records\n");
    for (int i = 0; i < event_count; i++) {
        if (i > 0)
            printf("--- record\n");
        const char *rec_status = events[i].type == EV_FAILURE ? failure_status : login_status;
        printf("status=%s\n", rec_status);
        printf("event=%s\n", events[i].type == EV_FAILURE ? "failure" : "login");
        printf("user=%s\n", events[i].user);
        printf("host=%s\n", events[i].host);
    }

    return 0;
}
