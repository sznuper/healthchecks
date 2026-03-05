/*
 * ssh_wtmp - Real-time SSH login/logout detection via watch trigger.
 *
 * Receives newly-appended /var/log/wtmp bytes on stdin (piped by the watch
 * trigger). Parses complete 384-byte utmp records; trailing partial records
 * are silently discarded by fread.
 *
 * Designed for the watch trigger:
 *
 *   - name: ssh_login
 *     healthcheck: file://ssh_wtmp
 *     trigger:
 *       watch: /var/log/wtmp
 *     args:
 *       alert_on: login
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_ALERT_ON  - Which events trigger non-ok status:
 *                               "login"  (default): warn on accepted logins
 *                               "logout": warn on logouts
 *                               "both":  warn on either
 *
 * Output (event_count > 0):
 *   status        - warning
 *   event_count   - total matching events
 *   event         - first event type ("login" or "logout")
 *   user          - first event's username
 *   host          - first event's remote host (empty for logouts)
 *   events        - JSON array of event types, e.g. ["login","logout"]
 *   users         - JSON array of usernames
 *   hosts         - JSON array of remote hosts
 *
 * Output (event_count == 0):
 *   status        - ok
 *   event_count   - 0
 *
 * Filtering:
 *   Login:  ut_type == USER_PROCESS (7) && ut_host[0] != '\0'  (remote SSH)
 *   Logout: ut_type == DEAD_PROCESS (8)
 */

#include <stdint.h>
#include <stdlib.h>

#include "sznuper.h"

/*
 * Linux utmp binary record layout — 384 bytes on x86-64.
 * Manually defined to avoid <utmp.h> (absent in Cosmopolitan Libc).
 */
#define UTMP_TYPE_USER_PROCESS 7
#define UTMP_TYPE_DEAD_PROCESS 8
#define UTMP_LINESIZE          32
#define UTMP_NAMESIZE          32
#define UTMP_HOSTSIZE          256
#define UTMP_RECSIZE           384

struct utmp_rec {
    int16_t ut_type;                /* +0   type of record               */
    int16_t _pad0;                  /* +2   alignment pad                */
    int32_t ut_pid;                 /* +4   PID of login process         */
    char    ut_line[UTMP_LINESIZE]; /* +8   tty device name              */
    char    ut_id[4];               /* +40  terminal name suffix         */
    char    ut_user[UTMP_NAMESIZE]; /* +44  username                     */
    char    ut_host[UTMP_HOSTSIZE]; /* +76  remote hostname or IP        */
    int16_t ut_exit_term;           /* +332 termination signal           */
    int16_t ut_exit_exit;           /* +334 exit status                  */
    int32_t ut_session;             /* +336 session ID                   */
    int32_t ut_tv_sec;              /* +340 login time (unix seconds)    */
    int32_t ut_tv_usec;             /* +344 login time (microseconds)    */
    int32_t ut_addr_v6[4];          /* +348 remote IPv4/v6 address       */
    char    _unused[20];            /* +364 reserved                     */
};                                  /* 384 bytes total                   */

typedef struct {
    const char *type; /* "login" or "logout" — points to string literal */
    char        user[UTMP_NAMESIZE + 1];
    char        host[UTMP_HOSTSIZE + 1];
} event_t;

int main(void) {
    const char *alert_on = parse_string("HEALTHCHECK_ARG_ALERT_ON", "login");
    int do_login  = (strcmp(alert_on, "login")  == 0 || strcmp(alert_on, "both") == 0);
    int do_logout = (strcmp(alert_on, "logout") == 0 || strcmp(alert_on, "both") == 0);

    event_t        *events = NULL;
    int             count  = 0;
    int             cap    = 0;
    struct utmp_rec rec;

    while (fread(&rec, UTMP_RECSIZE, 1, stdin) == 1) {
        int is_login  = (rec.ut_type == UTMP_TYPE_USER_PROCESS && rec.ut_host[0] != '\0');
        int is_logout = (rec.ut_type == UTMP_TYPE_DEAD_PROCESS);

        if (is_login && !do_login)
            continue;
        if (is_logout && !do_logout)
            continue;
        if (!is_login && !is_logout)
            continue;

        if (count >= cap) {
            cap    = cap ? cap * 2 : 8;
            events = realloc(events, (size_t)cap * sizeof(event_t));
            if (!events)
                return 1;
        }

        events[count].type = is_login ? "login" : "logout";
        memcpy(events[count].user, rec.ut_user, UTMP_NAMESIZE);
        events[count].user[UTMP_NAMESIZE] = '\0';
        memcpy(events[count].host, rec.ut_host, UTMP_HOSTSIZE);
        events[count].host[UTMP_HOSTSIZE] = '\0';
        count++;
    }

    if (count == 0) {
        printf("status=ok\n");
        printf("event_count=0\n");
        free(events);
        return 0;
    }

    printf("status=warning\n");
    printf("event_count=%d\n", count);
    printf("event=%s\n", events[0].type);
    printf("user=%s\n", events[0].user);
    printf("host=%s\n", events[0].host);

    printf("events=[\"%s\"", events[0].type);
    for (int i = 1; i < count; i++)
        printf(",\"%s\"", events[i].type);
    printf("]\n");

    printf("users=[\"%s\"", events[0].user);
    for (int i = 1; i < count; i++)
        printf(",\"%s\"", events[i].user);
    printf("]\n");

    printf("hosts=[\"%s\"", events[0].host);
    for (int i = 1; i < count; i++)
        printf(",\"%s\"", events[i].host);
    printf("]\n");

    free(events);
    return 0;
}
