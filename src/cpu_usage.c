/*
 * cpu_usage - Check CPU usage by sampling /proc/stat.
 *
 * Requirements:
 *   - Linux 2.6.11+ (reads /proc/stat, requires steal field)
 *
 * Reads /proc/stat twice with a configurable delay, computes deltas to
 * determine current CPU usage percentages.
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT - Warning threshold as percentage 0-100 (default: 80)
 *   HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT - Critical threshold as percentage 0-100 (default: 95)
 *   HEALTHCHECK_ARG_SAMPLE_MS      - Sample delay in milliseconds (default: 250)
 *   HEALTHCHECK_ARG_ADVANCED       - If set, emit all fields (nice, irq, softirq, steal, guest, procs)
 *
 * Output:
 *   --- event
 *   type            - ok, high_usage, or critical_usage
 *   usage_percent   - Total CPU usage percentage as float (0-100)
 *   user_percent    - User CPU percentage as float (0-100)
 *   system_percent  - System CPU percentage as float (0-100)
 *   idle_percent    - Idle percentage as float (0-100)
 *   iowait_percent  - I/O wait percentage as float (0-100)
 *   cores           - Number of CPU cores
 *
 * Output (advanced adds):
 *   nice_percent       - Nice CPU percentage as float (0-100)
 *   irq_percent        - IRQ percentage as float (0-100)
 *   softirq_percent    - Soft IRQ percentage as float (0-100)
 *   steal_percent      - Steal percentage as float (0-100)
 *   guest_percent      - Guest CPU percentage as float (0-100, subset of user)
 *   guest_nice_percent - Guest nice CPU percentage as float (0-100, subset of nice)
 *   procs_running      - Number of currently running processes
 *   procs_blocked      - Number of processes blocked on I/O
 *
 * Event type logic:
 *   usage >= threshold_crit_percent -> critical_usage
 *   usage >= threshold_warn_percent -> high_usage
 *   otherwise                       -> ok
 */

#include <time.h>

#include "sznuper.h"

struct cpu_sample {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    unsigned long long guest_nice;
};

static int read_proc_stat(struct cpu_sample *s, int *cores, int *procs_running,
                          int *procs_blocked) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return -1;

    char line[256];
    int found_cpu = 0;
    int core_count = 0;

    while (fgets(line, sizeof(line), f)) {
        if (!found_cpu && strncmp(line, "cpu ", 4) == 0) {
            s->guest = 0;
            s->guest_nice = 0;
            if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &s->user,
                       &s->nice, &s->system, &s->idle, &s->iowait, &s->irq, &s->softirq, &s->steal,
                       &s->guest, &s->guest_nice) < 8) {
                fclose(f);
                return -1;
            }
            found_cpu = 1;
        } else if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
            core_count++;
        } else if (procs_running && strncmp(line, "procs_running ", 14) == 0) {
            sscanf(line, "procs_running %d", procs_running);
        } else if (procs_blocked && strncmp(line, "procs_blocked ", 14) == 0) {
            sscanf(line, "procs_blocked %d", procs_blocked);
        }
    }

    fclose(f);
    if (!found_cpu)
        return -1;
    if (cores)
        *cores = core_count;
    return 0;
}

int main() {
    double thresh_warn = parse_threshold("HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT", 80);
    double thresh_crit = parse_threshold("HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT", 95);

    int advanced = parse_bool("HEALTHCHECK_ARG_ADVANCED");

    long sample_ms = 250;
    const char *sample_val = getenv("HEALTHCHECK_ARG_SAMPLE_MS");
    if (sample_val) {
        char *end;
        long v = strtol(sample_val, &end, 10);
        if (end != sample_val && v > 0)
            sample_ms = v;
    }

    struct cpu_sample s1, s2;
    int cores = 0;
    int running = 0, blocked = 0;

    if (read_proc_stat(&s1, &cores, NULL, NULL) != 0) {
        fprintf(stderr, "cpu_usage: requires Linux (/proc/stat not found)\n");
        return 1;
    }

    struct timespec ts = {sample_ms / 1000, (sample_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);

    if (read_proc_stat(&s2, NULL, &running, &blocked) != 0) {
        fprintf(stderr, "cpu_usage: requires Linux (/proc/stat not found)\n");
        return 1;
    }

#define DELTA(field) ((s2.field >= s1.field) ? (s2.field - s1.field) : 0)

    unsigned long long d_user = DELTA(user);
    unsigned long long d_nice = DELTA(nice);
    unsigned long long d_system = DELTA(system);
    unsigned long long d_idle = DELTA(idle);
    unsigned long long d_iowait = DELTA(iowait);
    unsigned long long d_irq = DELTA(irq);
    unsigned long long d_softirq = DELTA(softirq);
    unsigned long long d_steal = DELTA(steal);
    unsigned long long d_guest = DELTA(guest);
    unsigned long long d_guest_nice = DELTA(guest_nice);

#undef DELTA

    /* guest/guest_nice are already counted in user/nice — subtract to avoid
       double-counting in the total */
    unsigned long long d_user_real = (d_user >= d_guest) ? d_user - d_guest : 0;
    unsigned long long d_nice_real = (d_nice >= d_guest_nice) ? d_nice - d_guest_nice : 0;

    unsigned long long total =
        d_user_real + d_nice_real + d_system + d_idle + d_iowait + d_irq + d_softirq + d_steal;

    if (total == 0) {
        printf("--- event\n");
        printf("type=ok\n");
        printf("usage_percent=0.0\n");
        printf("user_percent=0.0\n");
        printf("system_percent=0.0\n");
        printf("idle_percent=100.0\n");
        printf("iowait_percent=0.0\n");
        printf("cores=%d\n", cores);
        if (advanced) {
            printf("nice_percent=0.0\n");
            printf("irq_percent=0.0\n");
            printf("softirq_percent=0.0\n");
            printf("steal_percent=0.0\n");
            printf("guest_percent=0.0\n");
            printf("guest_nice_percent=0.0\n");
            printf("procs_running=%d\n", running);
            printf("procs_blocked=%d\n", blocked);
        }
        return 0;
    }

    double usage_pct = (double)(total - d_idle) / (double)total * 100.0;

    double user_pct = (double)d_user_real / (double)total * 100.0;
    double nice_pct = (double)d_nice_real / (double)total * 100.0;
    double system_pct = (double)d_system / (double)total * 100.0;
    double idle_pct = (double)d_idle / (double)total * 100.0;
    double iowait_pct = (double)d_iowait / (double)total * 100.0;
    double irq_pct = (double)d_irq / (double)total * 100.0;
    double softirq_pct = (double)d_softirq / (double)total * 100.0;
    double steal_pct = (double)d_steal / (double)total * 100.0;
    double guest_pct = (double)d_guest / (double)total * 100.0;
    double guest_nice_pct = (double)d_guest_nice / (double)total * 100.0;

    const char *type;
    if (usage_pct >= thresh_crit)
        type = "critical_usage";
    else if (usage_pct >= thresh_warn)
        type = "high_usage";
    else
        type = "ok";

    printf("--- event\n");
    printf("type=%s\n", type);
    printf("usage_percent=%.1f\n", usage_pct);
    printf("user_percent=%.1f\n", user_pct);
    printf("system_percent=%.1f\n", system_pct);
    printf("idle_percent=%.1f\n", idle_pct);
    printf("iowait_percent=%.1f\n", iowait_pct);
    printf("cores=%d\n", cores);

    if (advanced) {
        printf("nice_percent=%.1f\n", nice_pct);
        printf("irq_percent=%.1f\n", irq_pct);
        printf("softirq_percent=%.1f\n", softirq_pct);
        printf("steal_percent=%.1f\n", steal_pct);
        printf("guest_percent=%.1f\n", guest_pct);
        printf("guest_nice_percent=%.1f\n", guest_nice_pct);
        printf("procs_running=%d\n", running);
        printf("procs_blocked=%d\n", blocked);
    }

    return 0;
}
