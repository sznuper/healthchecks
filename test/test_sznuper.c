/*
 * Unit tests for sznuper.h utility functions.
 */

#include "../src/sznuper.h"
#include "munit.h"

/* ---------- format_bytes ---------- */

static MunitResult test_format_bytes_zero(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(0, buf, sizeof(buf));
    munit_assert_string_equal(buf, "0B");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_bytes(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1023, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1023B");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_exact_k(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024, buf, sizeof(buf));
    munit_assert_string_equal(buf, "1.00K");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_megabytes(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 500, buf, sizeof(buf)); /* 500M */
    munit_assert_string_equal(buf, "500M");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_gigabytes(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 8, buf, sizeof(buf)); /* 8G */
    munit_assert_string_equal(buf, "8.00G");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_terabytes(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    format_bytes(1024ULL * 1024 * 1024 * 1024 * 2, buf, sizeof(buf)); /* 2T */
    munit_assert_string_equal(buf, "2.00T");
    return MUNIT_OK;
}

static MunitResult test_format_bytes_large_value(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[16];
    /* 150G -> rounds to >= 100, so integer format */
    format_bytes(1024ULL * 1024 * 1024 * 150, buf, sizeof(buf));
    munit_assert_string_equal(buf, "150G");
    return MUNIT_OK;
}

/* ---------- parse_bool ---------- */

static MunitResult test_parse_bool_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_BOOL");
    munit_assert_int(parse_bool("TEST_BOOL"), ==, 0);
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

/* ---------- parse_float ---------- */

static MunitResult test_parse_float_valid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "3.14", 1);
    double v = parse_float("TEST_FLOAT", 0.0);
    munit_assert_double(v, >, 3.13);
    munit_assert_double(v, <, 3.15);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_invalid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_FLOAT", "abc", 1);
    double v = parse_float("TEST_FLOAT", 42.0);
    munit_assert_double(v, ==, 42.0);
    unsetenv("TEST_FLOAT");
    return MUNIT_OK;
}

static MunitResult test_parse_float_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_FLOAT");
    double v = parse_float("TEST_FLOAT", 99.9);
    munit_assert_double(v, ==, 99.9);
    return MUNIT_OK;
}

/* ---------- parse_int ---------- */

static MunitResult test_parse_int_valid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_INT", "250", 1);
    munit_assert_long(parse_int("TEST_INT", 0), ==, 250);
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

static MunitResult test_parse_int_unset(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    unsetenv("TEST_INT");
    munit_assert_long(parse_int("TEST_INT", 55), ==, 55);
    return MUNIT_OK;
}

/* ---------- parse_threshold ---------- */

static MunitResult test_parse_threshold_valid(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "80.5", 1);
    double v = parse_threshold("TEST_THRESH", 50.0);
    munit_assert_double(v, >, 80.4);
    munit_assert_double(v, <, 80.6);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_too_high(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "101", 1);
    double v = parse_threshold("TEST_THRESH", 50.0);
    munit_assert_double(v, ==, 50.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_negative(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "-1", 1);
    double v = parse_threshold("TEST_THRESH", 50.0);
    munit_assert_double(v, ==, 50.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_boundary_zero(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "0", 1);
    double v = parse_threshold("TEST_THRESH", 50.0);
    munit_assert_double(v, ==, 0.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

static MunitResult test_parse_threshold_boundary_hundred(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    setenv("TEST_THRESH", "100", 1);
    double v = parse_threshold("TEST_THRESH", 50.0);
    munit_assert_double(v, ==, 100.0);
    unsetenv("TEST_THRESH");
    return MUNIT_OK;
}

/* ---------- test suite ---------- */

static MunitTest tests[] = {
    /* format_bytes */
    {"/format_bytes/zero", test_format_bytes_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/bytes", test_format_bytes_bytes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/exact_k", test_format_bytes_exact_k, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/megabytes", test_format_bytes_megabytes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/gigabytes", test_format_bytes_gigabytes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/terabytes", test_format_bytes_terabytes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_bytes/large_value", test_format_bytes_large_value, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_bool */
    {"/parse_bool/unset", test_parse_bool_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/zero", test_parse_bool_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/false", test_parse_bool_false, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/no", test_parse_bool_no, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/one", test_parse_bool_one, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/true", test_parse_bool_true, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_bool/yes", test_parse_bool_yes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_float */
    {"/parse_float/valid", test_parse_float_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/invalid", test_parse_float_invalid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_float/unset", test_parse_float_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_int */
    {"/parse_int/valid", test_parse_int_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/invalid", test_parse_int_invalid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_int/unset", test_parse_int_unset, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_threshold */
    {"/parse_threshold/valid", test_parse_threshold_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/too_high", test_parse_threshold_too_high, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/negative", test_parse_threshold_negative, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/boundary_zero", test_parse_threshold_boundary_zero, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_threshold/boundary_hundred", test_parse_threshold_boundary_hundred, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

static const MunitSuite suite = {
    "/sznuper", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};

int main(int argc, char *argv[]) {
    return munit_suite_main(&suite, NULL, argc, argv);
}
