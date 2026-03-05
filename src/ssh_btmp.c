/*
 * ssh_btmp - Real-time failed SSH login detection via watch trigger.
 *
 * Receives newly-appended /var/log/btmp bytes on stdin (piped by the watch
 * trigger). Parses complete 384-byte utmp records; trailing partial records
 * are silently discarded by fread. All USER_PROCESS records in btmp are
 * failed authentication attempts.
 *
 * Designed for the watch trigger:
 *
 *   - name: ssh_failed
 *     healthcheck: file://ssh_btmp
 *     trigger:
 *       watch: /var/log/btmp
 *
 * Output (failure_count > 0):
 *   status         - warning
 *   failure_count  - number of failed attempts in this batch
 *   user           - first attempt's username (convenience field)
 *   host           - first attempt's remote host
 *   users          - JSON array of usernames
 *   hosts          - JSON array of remote hosts
 *
 * Output (failure_count == 0):
 *   status         - ok
 *   failure_count  - 0
 *
 * Filtering:
 *   ut_type == USER_PROCESS (7) — the only record type written to btmp
 */

#include <stdint.h>
#include <stdlib.h>

#include "sznuper.h"

/*
 * Linux utmp binary record layout — 384 bytes on x86-64.
 * Manually defined to avoid <utmp.h> (absent in Cosmopolitan Libc).
 */
#define UTMP_TYPE_USER_PROCESS 7
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
    char user[UTMP_NAMESIZE + 1];
    char host[UTMP_HOSTSIZE + 1];
} attempt_t;

int main(void) {
    attempt_t      *attempts = NULL;
    int             count    = 0;
    int             cap      = 0;
    struct utmp_rec rec;

    while (fread(&rec, UTMP_RECSIZE, 1, stdin) == 1) {
        if (rec.ut_type != UTMP_TYPE_USER_PROCESS)
            continue;

        if (count >= cap) {
            cap      = cap ? cap * 2 : 8;
            attempts = realloc(attempts, (size_t)cap * sizeof(attempt_t));
            if (!attempts)
                return 1;
        }

        memcpy(attempts[count].user, rec.ut_user, UTMP_NAMESIZE);
        attempts[count].user[UTMP_NAMESIZE] = '\0';
        memcpy(attempts[count].host, rec.ut_host, UTMP_HOSTSIZE);
        attempts[count].host[UTMP_HOSTSIZE] = '\0';
        count++;
    }

    if (count == 0) {
        printf("status=ok\n");
        printf("failure_count=0\n");
        free(attempts);
        return 0;
    }

    printf("status=warning\n");
    printf("failure_count=%d\n", count);
    printf("user=%s\n", attempts[0].user);
    printf("host=%s\n", attempts[0].host);

    printf("users=[\"%s\"", attempts[0].user);
    for (int i = 1; i < count; i++)
        printf(",\"%s\"", attempts[i].user);
    printf("]\n");

    printf("hosts=[\"%s\"", attempts[0].host);
    for (int i = 1; i < count; i++)
        printf(",\"%s\"", attempts[i].host);
    printf("]\n");

    free(attempts);
    return 0;
}
