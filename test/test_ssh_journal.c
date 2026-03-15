/*
 * Unit tests for ssh_journal.c internal functions.
 *
 * Uses the #include-the-source pattern to access static functions.
 */

#define main ssh_journal_main
#include "../src/ssh_journal.c"
#undef main

#include "munit.h"

/* ---------- parse_message ---------- */

static MunitResult test_parse_invalid_user(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message("Invalid user admin from 1.2.3.4 port 55000", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_FAILURE);
    munit_assert_string_equal(ev.user, "admin");
    munit_assert_string_equal(ev.host, "1.2.3.4");
    return MUNIT_OK;
}

static MunitResult test_parse_connection_closed_auth(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Connection closed by authenticating user root 10.0.0.1 port 22 [preauth]", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_FAILURE);
    munit_assert_string_equal(ev.user, "root");
    munit_assert_string_equal(ev.host, "10.0.0.1");
    return MUNIT_OK;
}

static MunitResult test_parse_accepted_publickey(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Accepted publickey for deploy from 192.168.1.100 port 44222 ssh2", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_LOGIN);
    munit_assert_string_equal(ev.user, "deploy");
    munit_assert_string_equal(ev.host, "192.168.1.100");
    return MUNIT_OK;
}

static MunitResult test_parse_accepted_password(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Accepted password for alice from 172.16.0.5 port 33000 ssh2", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_LOGIN);
    munit_assert_string_equal(ev.user, "alice");
    munit_assert_string_equal(ev.host, "172.16.0.5");
    return MUNIT_OK;
}

static MunitResult test_parse_connection_closed_invalid_skip(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Connection closed by invalid user test 1.2.3.4 port 55000 [preauth]", &ev);
    munit_assert_int(matched, ==, 0);
    return MUNIT_OK;
}

static MunitResult test_parse_unrelated_message(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    munit_assert_int(parse_message("Server listening on 0.0.0.0 port 22", &ev), ==, 0);
    munit_assert_int(parse_message("pam_unix(sshd:session): session opened", &ev), ==, 0);
    munit_assert_int(parse_message("", &ev), ==, 0);
    return MUNIT_OK;
}

/* ---------- json_str ---------- */

static MunitResult test_json_str_basic(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"MESSAGE\":\"hello world\",\"OTHER\":\"x\"}", "MESSAGE", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "hello world");
    return MUNIT_OK;
}

static MunitResult test_json_str_escape_quote(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"KEY\":\"say \\\"hi\\\"\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "say \"hi\"");
    return MUNIT_OK;
}

static MunitResult test_json_str_escape_backslash(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"KEY\":\"a\\\\b\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "a\\b");
    return MUNIT_OK;
}

static MunitResult test_json_str_escape_newline(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"KEY\":\"line1\\nline2\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "line1\nline2");
    return MUNIT_OK;
}

static MunitResult test_json_str_missing_key(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"OTHER\":\"val\"}", "MISSING", buf, sizeof(buf));
    munit_assert_int(ok, ==, 0);
    return MUNIT_OK;
}

static MunitResult test_json_str_buffer_too_small(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[4]; /* too small for "hello" */
    int ok = json_str("{\"KEY\":\"hello\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 0);
    return MUNIT_OK;
}

/* ---------- format_ts ---------- */

static MunitResult test_format_ts_known(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[32];
    /* 2026-03-03T12:01:45Z = 1772539305 seconds = 1772539305000000 microseconds */
    format_ts("1772539305000000", buf, sizeof(buf));
    munit_assert_string_equal(buf, "2026-03-03T12:01:45Z");
    return MUNIT_OK;
}

static MunitResult test_format_ts_epoch(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[32];
    format_ts("0", buf, sizeof(buf));
    munit_assert_string_equal(buf, "1970-01-01T00:00:00Z");
    return MUNIT_OK;
}

/* ---------- test suite ---------- */

static MunitTest tests[] = {
    /* parse_message */
    {"/parse_message/invalid_user", test_parse_invalid_user, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/connection_closed_auth", test_parse_connection_closed_auth, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/accepted_publickey", test_parse_accepted_publickey, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/accepted_password", test_parse_accepted_password, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/connection_closed_invalid_skip", test_parse_connection_closed_invalid_skip, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/unrelated", test_parse_unrelated_message, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* json_str */
    {"/json_str/basic", test_json_str_basic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_quote", test_json_str_escape_quote, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_backslash", test_json_str_escape_backslash, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_newline", test_json_str_escape_newline, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/missing_key", test_json_str_missing_key, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/buffer_too_small", test_json_str_buffer_too_small, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* format_ts */
    {"/format_ts/known", test_format_ts_known, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_ts/epoch", test_format_ts_epoch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

static const MunitSuite suite = {
    "/ssh_journal", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};

int main(int argc, char *argv[]) {
    return munit_suite_main(&suite, NULL, argc, argv);
}
