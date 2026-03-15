/*
 * Unit tests for sznuper.h utility functions.
 */

#include <unistd.h>

#include "../src/sznuper.h"
#include "munit.h"

/* --- stdout capture helper --- */

static char captured[4096];
static int  captured_len;

static void capture_start(int *saved_fd, int pipefd[2]) {
    fflush(stdout);
    pipe(pipefd);
    *saved_fd = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
}

static void capture_end(int saved_fd, int pipefd[2]) {
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);
    captured_len = (int)read(pipefd[0], captured, sizeof(captured) - 1);
    if (captured_len < 0) captured_len = 0;
    captured[captured_len] = '\0';
    close(pipefd[0]);
}

/* ========== format_bytes ========== */

static MunitResult test_format_bytes_zero(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(0, buf, sizeof(buf));
    munit_assert_string_equal(buf, "0B");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_one(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1B");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_max_bytes(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1023, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1023B");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_exact_1k(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1.00K");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_1_5k(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1536, buf, sizeof(buf)); /* 1.5K */
    munit_assert_string_equal(buf, "1.50K");
    return MUNIT_OK;
}

/* 10K boundary: size >= 10.0 uses %.1f */
static MunitResult test_format_bytes_10k(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(10ULL * 1024, buf, sizeof(buf));
    munit_assert_string_equal(buf, "10.0K");
    return MUNIT_OK;
}

/* 100K boundary: size >= 100.0 uses integer format */
static MunitResult test_format_bytes_100k(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(100ULL * 1024, buf, sizeof(buf));
    munit_assert_string_equal(buf, "100K");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_exact_1m(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1.00M");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_500m(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 500, buf, sizeof(buf));
    munit_assert_string_equal(buf, "500M");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_8g(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 8, buf, sizeof(buf));
    munit_assert_string_equal(buf, "8.00G");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_150g(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 150, buf, sizeof(buf));
    munit_assert_string_equal(buf, "150G");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_2t(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 1024 * 2, buf, sizeof(buf));
    munit_assert_string_equal(buf, "2.00T");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_1p(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 1024 * 1024, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1.00P");
    return MUNIT_OK;
}

/* Beyond petabytes — stays at P since i caps at 5 */
static MunitResult test_format_bytes_max_unit(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 1024 * 1024 * 10, buf, sizeof(buf));
    munit_assert_string_equal(buf, "10.0P");
    return MUNIT_OK;
}

/* ========== emit_bytes ========== */

static MunitResult test_emit_bytes_human(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_bytes("total", 1024ULL * 1024 * 1024 * 8, 0);
    capture_end(saved, pipefd);
    munit_assert_string_equal(captured, "total=8.00G\n");
    return MUNIT_OK;
}

static MunitResult test_emit_bytes_raw(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_bytes("total", 8589934592ULL, 1);
    capture_end(saved, pipefd);
    munit_assert_string_equal(captured, "total=8589934592\n");
    return MUNIT_OK;
}

static MunitResult test_emit_bytes_zero_human(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_bytes("avail", 0, 0);
    capture_end(saved, pipefd);
    munit_assert_string_equal(captured, "avail=0B\n");
    return MUNIT_OK;
}

static MunitResult test_emit_bytes_zero_raw(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_bytes("avail", 0, 1);
    capture_end(saved, pipefd);
    munit_assert_string_equal(captured, "avail=0\n");
    return MUNIT_OK;
}

/* ========== parse_bool ========== */

static MunitResult test_parse_bool_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_BOOL");
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 0);
    return MUNIT_OK;
}

static MunitResult test_parse_bool_empty(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 0);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

static MunitResult test_parse_bool_zero(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "0", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 0);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

static MunitResult test_parse_bool_false(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "false", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 0);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

static MunitResult test_parse_bool_no(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "no", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 0);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

static MunitResult test_parse_bool_one(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "1", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 1);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

static MunitResult test_parse_bool_true(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "true", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 1);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

static MunitResult test_parse_bool_yes(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "yes", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 1);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

/* Any other string is truthy */
static MunitResult test_parse_bool_arbitrary(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_BOOL", "anything", 1);
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 1);
    unsetenv("TEST_BOOL");
    return MUNIT_OK;
}

/* ========== parse_float ========== */

static MunitResult test_parse_float_valid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "3.14", 1);
    double v = parse_float("TEST_FLOAT", 0.0);
    munit_assert_double(v, >, 3.13);
    munit_assert_double(v, <, 3.15);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_negative(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "-2.5", 1);
    double v = parse_float("TEST_FLOAT", 0.0);
    munit_assert_double(v, >, -2.6);
    munit_assert_double(v, <, -2.4);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_integer(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "42", 1);
    munit_assert_double(parse_float("TEST_FLOAT", 0.0), ==, 42.0);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_invalid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "abc", 1);
    munit_assert_double(parse_float("TEST_FLOAT", 42.0), ==, 42.0);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_empty(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "", 1);
    munit_assert_double(parse_float("TEST_FLOAT", 42.0), ==, 42.0);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_FLOAT");
    munit_assert_double(parse_float("TEST_FLOAT", 99.9), ==, 99.9);
    return MUNIT_OK;
}

/* Trailing garbage after valid number — strtod accepts the number part */
static MunitResult test_parse_float_trailing(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "3.14abc", 1);
    double v = parse_float("TEST_FLOAT", 0.0);
    munit_assert_double(v, >, 3.13);
    munit_assert_double(v, <, 3.15);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

/* ========== parse_int ========== */

static MunitResult test_parse_int_valid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_INT", "250", 1);
    munit_assert_long(parse_int("TEST_INT", 0), ==, 250);
    unsetenv("TEST_INT");
    return MUNIT_OK;
}

static MunitResult test_parse_int_negative(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_INT", "-10", 1);
    munit_assert_long(parse_int("TEST_INT", 0), ==, -10);
    unsetenv("TEST_INT");
    return MUNIT_OK;
}

static MunitResult test_parse_int_zero(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_INT", "0", 1);
    munit_assert_long(parse_int("TEST_INT", 99), ==, 0);
    unsetenv("TEST_INT");
    return MUNIT_OK;
}

static MunitResult test_parse_int_invalid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_INT", "xyz", 1);
    munit_assert_long(parse_int("TEST_INT", 100), ==, 100);
    unsetenv("TEST_INT");
    return MUNIT_OK;
}

static MunitResult test_parse_int_empty(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_INT", "", 1);
    munit_assert_long(parse_int("TEST_INT", 77), ==, 77);
    unsetenv("TEST_INT");
    return MUNIT_OK;
}

static MunitResult test_parse_int_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_INT");
    munit_assert_long(parse_int("TEST_INT", 55), ==, 55);
    return MUNIT_OK;
}

/* ========== parse_string ========== */

static MunitResult test_parse_string_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_STR");
    munit_assert_string_equal(parse_string("TEST_STR", "/"), "/");
    return MUNIT_OK;
}

static MunitResult test_parse_string_set(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_STR", "/mnt/data", 1);
    munit_assert_string_equal(parse_string("TEST_STR", "/"), "/mnt/data");
    unsetenv("TEST_STR");
    return MUNIT_OK;
}

static MunitResult test_parse_string_empty(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_STR", "", 1);
    /* Empty string IS set — returns empty, not fallback */
    munit_assert_string_equal(parse_string("TEST_STR", "/"), "");
    unsetenv("TEST_STR");
    return MUNIT_OK;
}

/* ========== parse_threshold ========== */

static MunitResult test_parse_threshold_valid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "80.5", 1);
    double v = parse_threshold("TEST_THRESH", 50.0);
    munit_assert_double(v, >, 80.4);
    munit_assert_double(v, <, 80.6);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_zero(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "0", 1);
    munit_assert_double(parse_threshold("TEST_THRESH", 50.0), ==, 0.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_hundred(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "100", 1);
    munit_assert_double(parse_threshold("TEST_THRESH", 50.0), ==, 100.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_above_100(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "100.1", 1);
    munit_assert_double(parse_threshold("TEST_THRESH", 50.0), ==, 50.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_negative(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "-0.1", 1);
    munit_assert_double(parse_threshold("TEST_THRESH", 50.0), ==, 50.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_THRESH");
    munit_assert_double(parse_threshold("TEST_THRESH", 80.0), ==, 80.0);
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_invalid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "abc", 1);
    munit_assert_double(parse_threshold("TEST_THRESH", 80.0), ==, 80.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

/* ========== test suite ========== */

static MunitTest tests[] = {
    /* format_bytes */
    {"/format_bytes/zero", test_format_bytes_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/one", test_format_bytes_one, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/max_bytes", test_format_bytes_max_bytes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/exact_1k", test_format_bytes_exact_1k, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/1_5k", test_format_bytes_1_5k, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/10k", test_format_bytes_10k, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/100k", test_format_bytes_100k, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/exact_1m", test_format_bytes_exact_1m, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/500m", test_format_bytes_500m, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/8g", test_format_bytes_8g, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/150g", test_format_bytes_150g, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/2t", test_format_bytes_2t, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/1p", test_format_bytes_1p, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/max_unit", test_format_bytes_max_unit, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* emit_bytes */
    {"/emit_bytes/human", test_emit_bytes_human, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/emit_bytes/raw", test_emit_bytes_raw, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/emit_bytes/zero_human", test_emit_bytes_zero_human, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/emit_bytes/zero_raw", test_emit_bytes_zero_raw, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_bool */
    {"/parse_bool/unset", test_parse_bool_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/empty", test_parse_bool_empty, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/zero", test_parse_bool_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/false", test_parse_bool_false, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/no", test_parse_bool_no, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/one", test_parse_bool_one, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/true", test_parse_bool_true, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/yes", test_parse_bool_yes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/arbitrary", test_parse_bool_arbitrary, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_float */
    {"/parse_float/valid", test_parse_float_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/negative", test_parse_float_negative, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/integer", test_parse_float_integer, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/invalid", test_parse_float_invalid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/empty", test_parse_float_empty, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/unset", test_parse_float_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/trailing", test_parse_float_trailing, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_int */
    {"/parse_int/valid", test_parse_int_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/negative", test_parse_int_negative, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/zero", test_parse_int_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/invalid", test_parse_int_invalid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/empty", test_parse_int_empty, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/unset", test_parse_int_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_string */
    {"/parse_string/unset", test_parse_string_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_string/set", test_parse_string_set, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_string/empty", test_parse_string_empty, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_threshold */
    {"/parse_threshold/valid", test_parse_threshold_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/zero", test_parse_threshold_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/hundred", test_parse_threshold_hundred, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/above_100", test_parse_threshold_above_100, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/negative", test_parse_threshold_negative, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/unset", test_parse_threshold_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/invalid", test_parse_threshold_invalid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

static const MunitSuite suite = {
    "/sznuper", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};

int main(int argc, char *argv[]) {
    return munit_suite_main(&suite, NULL, argc, argv);
}
