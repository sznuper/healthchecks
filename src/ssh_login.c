/*
 * ssh_login - Report SSH logins and failed attempts within a time window.
 *
 * Reads /var/log/wtmp (successful logins) and /var/log/btmp (failed attempts)
 * as utmp binary records. Works on any Linux system without depending on
 * distribution-specific log files — auth.log is absent on Debian 13+ with
 * systemd-wtmpdb, Fedora with journald-only setups, etc.
 *
 * Designed for the interval trigger. Match window_minutes to the interval so
 * each invocation covers only what happened since the last check:
 *
 *   - name: ssh_login
 *     healthcheck: file://ssh_login
 *     trigger:
 *       interval: 1m
 *     args:
 *       alert_on: both
 *       window_minutes: 1
 *       threshold_warn_count: 5
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_ALERT_ON              - Which events trigger non-ok status:
 *                                           "login"   (default): warning on accepted logins
 *                                           "failure": warning/critical on failed attempts
 *                                           "both":   either condition triggers non-ok
 *   HEALTHCHECK_ARG_WINDOW_MINUTES        - Look-back window in minutes (default: 60)
 *   HEALTHCHECK_ARG_THRESHOLD_WARN_COUNT  - Failure count for warning  (default: 5)
 *   HEALTHCHECK_ARG_THRESHOLD_CRIT_COUNT  - Failure count for critical (default: 20)
 *   HEALTHCHECK_ARG_WTMP_FILE             - Path to wtmp file (default: /var/log/wtmp)
 *   HEALTHCHECK_ARG_BTMP_FILE             - Path to btmp file (default: /var/log/btmp)
 *   HEALTHCHECK_ARG_ADVANCED              - If set, emit last_user and last_host
 *
 * Output (basic):
 *   status         - ok, warning, or critical
 *   login_count    - Remote logins in the window (from wtmp)
 *   failure_count  - Failed remote attempts in the window (from btmp; 0 if unreadable)
 *
 * Output (advanced adds):
 *   last_user      - Username from the last login in the window (empty if none)
 *   last_host      - Hostname or IP from the last login in the window (empty if none)
 *
 * Notes:
 *   - Remote logins: ut_type=USER_PROCESS with a non-empty ut_host field.
 *   - /var/log/btmp requires root or group adm. If unreadable, failure_count=0
 *     with no error output (normal for unprivileged users).
 *   - On Debian 13+, wtmpdb (SQLite) is used by `last`, but /var/log/wtmp is
 *     still written by PAM/sshd for compatibility — both coexist.
 *
 * Status logic (alert_on=login):
 *   login_count > 0                       -> warning
 *   otherwise                             -> ok
 *
 * Status logic (alert_on=failure):
 *   failure_count >= threshold_crit_count -> critical
 *   failure_count >= threshold_warn_count -> warning
 *   otherwise                             -> ok
 *
 * Status logic (alert_on=both):
 *   failure_count >= threshold_crit_count -> critical
 *   login_count > 0 OR failure >= warn    -> warning
 *   otherwise                             -> ok
 */

#include <stdint.h>
#include <time.h>

#include "sznuper.h"

/*
 * Linux utmp binary record layout — 384 bytes on x86-64.
 *
 * This is the on-disk format written by the kernel and glibc. It is part of
 * the stable Linux ABI and has not changed since the 64-bit transition.
 * We define it here rather than including <utmp.h> to avoid issues with
 * toolchains that do not ship that header (e.g. Cosmopolitan Libc).
 *
 * Field offsets (manually verified to match glibc bits/utmp.h):
 *   +0   ut_type    short
 *   +2   (pad)      short
 *   +4   ut_pid     int32
 *   +8   ut_line    char[32]
 *   +40  ut_id      char[4]
 *   +44  ut_user    char[32]
 *   +76  ut_host    char[256]
 *   +332 ut_exit    int16 + int16
 *   +336 ut_session int32
 *   +340 ut_tv_sec  int32   (wall-clock seconds of login)
 *   +344 ut_tv_usec int32
 *   +348 ut_addr_v6 int32[4]
 *   +364 (reserved) char[20]
 *   = 384 bytes total
 */
#define UTMP_TYPE_USER_PROCESS 7
#define UTMP_LINESIZE 32
#define UTMP_NAMESIZE 32
#define UTMP_HOSTSIZE 256
#define UTMP_RECSIZE  384

struct utmp_rec {
    int16_t  ut_type;                 /* +0   type of record               */
    int16_t  _pad0;                   /* +2   alignment pad                */
    int32_t  ut_pid;                  /* +4   PID of login process         */
    char     ut_line[UTMP_LINESIZE];  /* +8   tty device name              */
    char     ut_id[4];                /* +40  terminal name suffix         */
    char     ut_user[UTMP_NAMESIZE];  /* +44  username                     */
    char     ut_host[UTMP_HOSTSIZE];  /* +76  remote hostname or IP        */
    int16_t  ut_exit_term;            /* +332 termination signal           */
    int16_t  ut_exit_exit;            /* +334 exit status                  */
    int32_t  ut_session;              /* +336 session ID                   */
    int32_t  ut_tv_sec;               /* +340 login time (unix seconds)    */
    int32_t  ut_tv_usec;              /* +344 login time (microseconds)    */
    int32_t  ut_addr_v6[4];           /* +348 remote IPv4/v6 address       */
    char     _unused[20];             /* +364 reserved                     */
};                                    /* 384 bytes total                   */

/*
 * Scan all utmp records in f, counting entries that are:
 *   - ut_type == USER_PROCESS  (active login, not boot/runlevel/reboot)
 *   - ut_host non-empty        (remote login — SSH sets this; local ttys do not)
 *   - ut_tv_sec >= since       (within the requested time window)
 *
 * If last_user / last_host are non-NULL they are updated on each match, so
 * after the scan they hold the values from the most recent matching record
 * (utmp files are append-only, so later = more recent).
 */
static long count_remote(FILE *f, time_t since,
                          char *last_user, char *last_host) {
    struct utmp_rec rec;
    long count = 0;

    while (fread(&rec, UTMP_RECSIZE, 1, f) == 1) {
        if (rec.ut_type != UTMP_TYPE_USER_PROCESS)
            continue;
        if (rec.ut_host[0] == '\0')
            continue;
        if ((time_t)rec.ut_tv_sec < since)
            continue;

        count++;

        if (last_user) {
            /* Fields are fixed-size and may not be null-terminated at maxlen */
            memcpy(last_user, rec.ut_user, UTMP_NAMESIZE);
            last_user[UTMP_NAMESIZE] = '\0';
            memcpy(last_host, rec.ut_host, UTMP_HOSTSIZE);
            last_host[UTMP_HOSTSIZE] = '\0';
        }
    }

    return count;
}

int main(void) {
    const char *alert_on  = parse_string("HEALTHCHECK_ARG_ALERT_ON", "login");
    long window_min       = parse_int("HEALTHCHECK_ARG_WINDOW_MINUTES", 60);
    long thresh_warn      = parse_int("HEALTHCHECK_ARG_THRESHOLD_WARN_COUNT", 5);
    long thresh_crit      = parse_int("HEALTHCHECK_ARG_THRESHOLD_CRIT_COUNT", 20);
    const char *wtmp_path = parse_string("HEALTHCHECK_ARG_WTMP_FILE", "/var/log/wtmp");
    const char *btmp_path = parse_string("HEALTHCHECK_ARG_BTMP_FILE", "/var/log/btmp");
    int advanced          = parse_bool("HEALTHCHECK_ARG_ADVANCED");

    int do_alert_login   = (strcmp(alert_on, "login")   == 0 ||
                            strcmp(alert_on, "both")    == 0);
    int do_alert_failure = (strcmp(alert_on, "failure") == 0 ||
                            strcmp(alert_on, "both")    == 0);

    if (window_min <= 0)
        window_min = 60;

    time_t since = time(NULL) - (time_t)(window_min * 60);

    /* +1 for the guaranteed null terminator we write after the fixed field */
    char last_user[UTMP_NAMESIZE + 1] = "";
    char last_host[UTMP_HOSTSIZE + 1] = "";

    /* Successful remote logins from wtmp */
    long login_count = 0;
    FILE *wf = fopen(wtmp_path, "rb");
    if (wf) {
        login_count = count_remote(wf, since,
                                   advanced ? last_user : NULL,
                                   advanced ? last_host : NULL);
        fclose(wf);
    } else {
        fprintf(stderr, "ssh_login: cannot open %s\n", wtmp_path);
    }

    /* Failed remote attempts from btmp (may be unreadable without root) */
    long failure_count = 0;
    FILE *bf = fopen(btmp_path, "rb");
    if (bf) {
        failure_count = count_remote(bf, since, NULL, NULL);
        fclose(bf);
    }

    /* Determine status */
    const char *status = "ok";
    if (do_alert_failure) {
        if (failure_count >= thresh_crit)
            status = "critical";
        else if (failure_count >= thresh_warn)
            status = "warning";
    }
    if (do_alert_login && login_count > 0) {
        if (strcmp(status, "critical") != 0)
            status = "warning";
    }

    printf("status=%s\n", status);
    printf("login_count=%ld\n", login_count);
    printf("failure_count=%ld\n", failure_count);

    if (advanced) {
        printf("last_user=%s\n", last_user);
        printf("last_host=%s\n", last_host);
    }

    return 0;
}
