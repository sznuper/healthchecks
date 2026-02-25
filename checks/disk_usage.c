/*
 * disk_usage - Check disk usage for a given mount point.
 *
 * Args (via environment):
 *   BARKER_ARG_MOUNT          - Mount point to check (default: "/")
 *   BARKER_ARG_THRESHOLD_WARN - Warning threshold as float 0.0-1.0 (default: 0.80)
 *   BARKER_ARG_THRESHOLD_CRIT - Critical threshold as float 0.0-1.0 (default: 0.95)
 *
 * Output:
 *   status    - ok, warning, or critical
 *   mount     - The mount point checked
 *   usage     - Usage percentage as integer (0-100)
 *   total     - Total disk space (human-readable, e.g. "50G")
 *   used      - Used disk space (human-readable)
 *   available - Available disk space (human-readable)
 *
 * Status logic:
 *   usage >= threshold_crit -> critical
 *   usage >= threshold_warn -> warning
 *   otherwise               -> ok
 */

#include "cosmopolitan.h"

static void format_bytes(unsigned long long bytes, char *buf, size_t len) {
    const char *units[] = {"B", "K", "M", "G", "T", "P"};
    int i = 0;
    double size = (double)bytes;
    while (size >= 1024.0 && i < 5) {
        size /= 1024.0;
        i++;
    }
    if (i == 0)
        snprintf(buf, len, "%llu%s", bytes, units[i]);
    else if (size >= 100.0)
        snprintf(buf, len, "%d%s", (int)(size + 0.5), units[i]);
    else if (size >= 10.0)
        snprintf(buf, len, "%.1f%s", size, units[i]);
    else
        snprintf(buf, len, "%.2f%s", size, units[i]);
}

static double parse_threshold(const char *env_key, double fallback) {
    const char *val = getenv(env_key);
    if (!val) return fallback;
    char *end;
    double d = strtod(val, &end);
    if (end == val || d < 0.0 || d > 1.0) return fallback;
    return d;
}

int main() {
    const char *mount = getenv("BARKER_ARG_MOUNT");
    if (!mount) mount = "/";

    double thresh_warn = parse_threshold("BARKER_ARG_THRESHOLD_WARN", 0.80);
    double thresh_crit = parse_threshold("BARKER_ARG_THRESHOLD_CRIT", 0.95);

    struct statvfs fs;
    if (statvfs(mount, &fs) != 0) {
        fprintf(stderr, "statvfs failed for %s: %s\n", mount, strerror(errno));
        return 1;
    }

    unsigned long long total = (unsigned long long)fs.f_frsize * fs.f_blocks;
    unsigned long long avail = (unsigned long long)fs.f_frsize * fs.f_bavail;
    unsigned long long used  = total - avail;

    double usage = (total > 0) ? (double)used / (double)total : 0.0;
    int usage_pct = (int)(usage * 100.0 + 0.5);

    char total_str[16], used_str[16], avail_str[16];
    format_bytes(total, total_str, sizeof(total_str));
    format_bytes(used, used_str, sizeof(used_str));
    format_bytes(avail, avail_str, sizeof(avail_str));

    const char *status;
    if (usage >= thresh_crit)      status = "critical";
    else if (usage >= thresh_warn) status = "warning";
    else                           status = "ok";

    printf("status=%s\n", status);
    printf("mount=%s\n", mount);
    printf("usage=%d\n", usage_pct);
    printf("total=%s\n", total_str);
    printf("used=%s\n", used_str);
    printf("available=%s\n", avail_str);

    return 0;
}
