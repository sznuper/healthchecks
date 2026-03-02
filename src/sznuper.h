/*
 * sznuper.h - Shared utilities for Sznuper healthchecks.
 *
 * Header-only: all functions are static inline so each healthcheck compiles
 * to a fully self-contained binary with no shared library dependency.
 */

#ifndef HEALTHCHECK_H
#define HEALTHCHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse a HEALTHCHECK_ARG_* env var as a boolean.
 * Returns 0 for unset, empty, "0", "false", "no"; 1 otherwise. */
static inline int parse_bool(const char *env_key) {
    const char *val = getenv(env_key);
    if (!val || val[0] == '\0')
        return 0;
    if (strcmp(val, "0") == 0 || strcmp(val, "false") == 0 || strcmp(val, "no") == 0)
        return 0;
    return 1;
}

/* Parse a HEALTHCHECK_ARG_* env var as a float.
 * Returns fallback if unset or not a valid number. */
static inline double parse_float(const char *env_key, double fallback) {
    const char *val = getenv(env_key);
    if (!val)
        return fallback;
    char *end;
    double d = strtod(val, &end);
    if (end == val)
        return fallback;
    return d;
}

/* Parse a HEALTHCHECK_ARG_* env var as an integer.
 * Returns fallback if unset or not a valid integer. */
static inline long parse_int(const char *env_key, long fallback) {
    const char *val = getenv(env_key);
    if (!val)
        return fallback;
    char *end;
    long n = strtol(val, &end, 10);
    if (end == val)
        return fallback;
    return n;
}

/* Parse a HEALTHCHECK_ARG_* env var as a string.
 * Returns fallback if unset. */
static inline const char *parse_string(const char *env_key, const char *fallback) {
    const char *val = getenv(env_key);
    if (!val)
        return fallback;
    return val;
}

/* Parse a HEALTHCHECK_ARG_* env var as a percentage threshold (0-100).
 * Returns fallback if unset or out of range. */
static inline double parse_threshold(const char *env_key, double fallback) {
    double d = parse_float(env_key, fallback);
    if (d < 0.0 || d > 100.0)
        return fallback;
    return d;
}

/* Format a byte count as a human-readable string (e.g. "8.5G", "120M"). */
static inline void format_bytes(unsigned long long bytes, char *buf, size_t len) {
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

/* Emit a byte-valued key=value pair, raw or human-readable. */
static inline void emit_bytes(const char *key, unsigned long long bytes, int raw) {
    if (raw) {
        printf("%s=%llu\n", key, bytes);
    } else {
        char buf[16];
        format_bytes(bytes, buf, sizeof(buf));
        printf("%s=%s\n", key, buf);
    }
}

#endif /* HEALTHCHECK_H */
