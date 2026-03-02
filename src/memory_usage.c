/*
 * memory_usage - Check memory usage by reading /proc/meminfo.
 *
 * Reads /proc/meminfo once (memory values are instantaneous, no sampling
 * needed) and computes usage as (MemTotal - MemAvailable) / MemTotal,
 * matching free/htop behavior that accounts for buffers, cache, and
 * reclaimable memory.
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT - Warning threshold as percentage 0-100 (default: 80)
 *   HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT - Critical threshold as percentage 0-100 (default: 95)
 *   HEALTHCHECK_ARG_RAW            - If set, emit byte values as raw integers instead of human-readable
 *   HEALTHCHECK_ARG_ADVANCED       - If set, emit all fields plus generic pass-through of /proc/meminfo
 *
 * Output (basic):
 *   status            - ok, warning, or critical
 *   usage_percent     - Memory usage percentage as float (0-100)
 *   total             - MemTotal (human-readable, or raw bytes if RAW set)
 *   used              - MemTotal - MemAvailable (human-readable, or raw bytes)
 *   available         - MemAvailable, free + reclaimable (human-readable, or raw bytes)
 *   swap_total        - SwapTotal (human-readable, or raw bytes)
 *   swap_used         - SwapTotal - SwapFree (human-readable, or raw bytes)
 *   swap_usage_percent - Swap usage percentage as float (0-100)
 *
 * Output (advanced adds):
 *   free              - MemFree, truly unused (human-readable, or raw bytes)
 *   buffers           - Buffers (human-readable, or raw bytes)
 *   cached            - Cached (human-readable, or raw bytes)
 *   swap_free         - SwapFree (human-readable, or raw bytes)
 *   shared            - Shmem (human-readable, or raw bytes)
 *   slab              - Slab (human-readable, or raw bytes)
 *   (plus all remaining /proc/meminfo fields as lowercase keys)
 *
 * Status logic:
 *   usage >= threshold_crit_percent -> critical
 *   usage >= threshold_warn_percent -> warning
 *   otherwise               -> ok
 */

#include <ctype.h>

#include "sznuper.h"

/* Transform a /proc/meminfo key to lowercase output key.
 * Lowercases, replaces '(' with '_', removes ')'.
 * e.g. "Active(anon)" -> "active_anon", "HugePages_Total" -> "hugepages_total"
 */
static void transform_key(const char *src, char *dst, size_t len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < len - 1; i++) {
        if (src[i] == '(') {
            dst[j++] = '_';
        } else if (src[i] == ')') {
            /* skip */
        } else {
            dst[j++] = (char)tolower((unsigned char)src[i]);
        }
    }
    dst[j] = '\0';
}

struct meminfo_entry {
    char key[64];
    unsigned long long value;
    int is_kb;
};

int main() {
    double thresh_warn = parse_threshold("HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT", 80);
    double thresh_crit = parse_threshold("HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT", 95);

    int raw = parse_bool("HEALTHCHECK_ARG_RAW");
    int advanced = parse_bool("HEALTHCHECK_ARG_ADVANCED");

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        fprintf(stderr, "memory_usage: requires Linux (/proc/meminfo not found)\n");
        return 1;
    }

    unsigned long long mem_total = 0, mem_free = 0, mem_available = 0;
    unsigned long long buffers = 0, cached = 0;
    unsigned long long swap_total = 0, swap_free = 0;
    unsigned long long shmem = 0, slab = 0;

    struct meminfo_entry extra[128];
    int extra_count = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';

        unsigned long long val;
        char unit[8] = "";
        int fields = sscanf(colon + 1, " %llu %7s", &val, unit);
        if (fields < 1)
            continue;
        int is_kb = (fields >= 2 && strcmp(unit, "kB") == 0);

        if (strcmp(line, "MemTotal") == 0)
            mem_total = val;
        else if (strcmp(line, "MemFree") == 0)
            mem_free = val;
        else if (strcmp(line, "MemAvailable") == 0)
            mem_available = val;
        else if (strcmp(line, "Buffers") == 0)
            buffers = val;
        else if (strcmp(line, "Cached") == 0)
            cached = val;
        else if (strcmp(line, "SwapTotal") == 0)
            swap_total = val;
        else if (strcmp(line, "SwapFree") == 0)
            swap_free = val;
        else if (strcmp(line, "Shmem") == 0)
            shmem = val;
        else if (strcmp(line, "Slab") == 0)
            slab = val;
        else if (advanced && extra_count < 128) {
            /* Store remaining fields for pass-through */
            transform_key(line, extra[extra_count].key, sizeof(extra[extra_count].key));
            extra[extra_count].value = val;
            extra[extra_count].is_kb = is_kb;
            extra_count++;
        }
    }

    fclose(f);

    if (mem_total == 0) {
        fprintf(stderr, "failed to parse MemTotal from /proc/meminfo\n");
        return 1;
    }

    /* /proc/meminfo values are in kB — convert to bytes for format_bytes */
    unsigned long long used_kb = mem_total - mem_available;
    double usage_pct = (double)used_kb / (double)mem_total * 100.0;

    unsigned long long swap_used = (swap_total >= swap_free) ? swap_total - swap_free : 0;
    double swap_usage_pct = (swap_total > 0) ? (double)swap_used / (double)swap_total * 100.0 : 0.0;

    const char *status;
    if (usage_pct >= thresh_crit)
        status = "critical";
    else if (usage_pct >= thresh_warn)
        status = "warning";
    else
        status = "ok";

    printf("status=%s\n", status);
    printf("usage_percent=%.1f\n", usage_pct);
    emit_bytes("total", mem_total * 1024, raw);
    emit_bytes("used", used_kb * 1024, raw);
    emit_bytes("available", mem_available * 1024, raw);
    emit_bytes("swap_total", swap_total * 1024, raw);
    emit_bytes("swap_used", swap_used * 1024, raw);
    printf("swap_usage_percent=%.1f\n", swap_usage_pct);

    if (advanced) {
        emit_bytes("free", mem_free * 1024, raw);
        emit_bytes("buffers", buffers * 1024, raw);
        emit_bytes("cached", cached * 1024, raw);
        emit_bytes("swap_free", swap_free * 1024, raw);
        emit_bytes("shared", shmem * 1024, raw);
        emit_bytes("slab", slab * 1024, raw);

        for (int i = 0; i < extra_count; i++) {
            if (extra[i].is_kb)
                emit_bytes(extra[i].key, extra[i].value * 1024, raw);
            else
                printf("%s=%llu\n", extra[i].key, extra[i].value);
        }
    }

    return 0;
}
