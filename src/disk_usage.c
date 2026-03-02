/*
 * disk_usage - Check disk usage for a given mount point.
 *
 * Args (via environment):
 *   HEALTHCHECK_ARG_MOUNT          - Mount point to check (default: "/")
 *   HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT - Warning threshold as percentage 0-100 (default: 80)
 *   HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT - Critical threshold as percentage 0-100 (default: 95)
 *   HEALTHCHECK_ARG_RAW            - If set, emit byte values as raw integers instead of human-readable
 *   HEALTHCHECK_ARG_ADVANCED       - If set, emit all fields (free, inode stats)
 *
 * Output (basic):
 *   status           - ok, warning, or critical
 *   mount            - The mount point checked
 *   usage_percent    - Usage percentage as float (0-100)
 *   total            - Total disk space (human-readable, or raw bytes if RAW set)
 *   used             - Used disk space (human-readable, or raw bytes)
 *   available        - Available disk space for non-root (human-readable, or raw bytes)
 *
 * Output (advanced adds):
 *   free             - Free disk space including root-reserved (human-readable, or raw bytes)
 *   inodes           - Total inodes
 *   inodes_used      - Used inodes
 *   inodes_free      - Free inodes including root-reserved
 *   inodes_available - Available inodes for non-root
 *   inodes_usage_percent - Inode usage percentage as float (0-100)
 *
 * Note: Filesystems like btrfs do not expose inode counts via statvfs.
 * On these filesystems all inode fields will be 0 — this is expected.
 *
 * Status logic:
 *   usage >= threshold_crit_percent -> critical
 *   usage >= threshold_warn_percent -> warning
 *   otherwise               -> ok
 */

#include <errno.h>
#include <sys/statvfs.h>

#include "sznuper.h"

int main() {
    const char *mount = getenv("HEALTHCHECK_ARG_MOUNT");
    if (!mount)
        mount = "/";

    double thresh_warn = parse_threshold("HEALTHCHECK_ARG_THRESHOLD_WARN_PERCENT", 80);
    double thresh_crit = parse_threshold("HEALTHCHECK_ARG_THRESHOLD_CRIT_PERCENT", 95);

    int raw = parse_bool("HEALTHCHECK_ARG_RAW");
    int advanced = parse_bool("HEALTHCHECK_ARG_ADVANCED");

    struct statvfs fs;
    if (statvfs(mount, &fs) != 0) {
        fprintf(stderr, "statvfs failed for %s: %s\n", mount, strerror(errno));
        return 1;
    }

    unsigned long long total = (unsigned long long)fs.f_frsize * fs.f_blocks;
    unsigned long long free_bytes = (unsigned long long)fs.f_frsize * fs.f_bfree;
    unsigned long long avail = (unsigned long long)fs.f_frsize * fs.f_bavail;
    unsigned long long used = total - free_bytes;

    /* usage = used / (used + available), matching df behavior */
    unsigned long long nonroot_total = used + avail;
    double usage_pct = (nonroot_total > 0) ? (double)used / (double)nonroot_total * 100.0 : 0.0;

    unsigned long long inodes_total = fs.f_files;
    unsigned long long inodes_free = fs.f_ffree;
    unsigned long long inodes_avail = fs.f_favail;
    unsigned long long inodes_used = inodes_total - inodes_free;
    /* same df-style denominator for inodes */
    unsigned long long inodes_nonroot = inodes_used + inodes_avail;
    double inode_usage_pct =
        (inodes_nonroot > 0) ? (double)inodes_used / (double)inodes_nonroot * 100.0 : 0.0;

    const char *status;
    if (usage_pct >= thresh_crit)
        status = "critical";
    else if (usage_pct >= thresh_warn)
        status = "warning";
    else
        status = "ok";

    printf("status=%s\n", status);
    printf("mount=%s\n", mount);
    printf("usage_percent=%.1f\n", usage_pct);
    emit_bytes("total", total, raw);
    emit_bytes("used", used, raw);
    emit_bytes("available", avail, raw);

    if (advanced) {
        emit_bytes("free", free_bytes, raw);
        printf("inodes=%llu\n", inodes_total);
        printf("inodes_used=%llu\n", inodes_used);
        printf("inodes_free=%llu\n", inodes_free);
        printf("inodes_available=%llu\n", inodes_avail);
        printf("inodes_usage_percent=%.1f\n", inode_usage_pct);
    }

    return 0;
}
