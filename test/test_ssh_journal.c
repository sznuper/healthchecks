/*
 * Unit tests for ssh_journal.c internal functions.
 *
 * Uses the #include-the-source pattern to access static functions.
 */

#include <unistd.h>

#define main ssh_journal_main
#include "../src/ssh_journal.c"
#undef main

#include "munit.h"

/* --- stdout capture helper --- */

static char captured[8192];
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

/* ========== parse_message: failure patterns ========== */

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

static MunitResult test_parse_invalid_user_no_port(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    /* Some sshd versions don't include "port N" */
    int matched = parse_message("Invalid user test from 10.0.0.1", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_FAILURE);
    munit_assert_string_equal(ev.user, "test");
    munit_assert_string_equal(ev.host, "10.0.0.1");
    return MUNIT_OK;
}

static MunitResult test_parse_invalid_user_ipv6(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    /* IPv6 — host is just the next token after "from " */
    int matched = parse_message("Invalid user root from ::1 port 22", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_FAILURE);
    munit_assert_string_equal(ev.user, "root");
    munit_assert_string_equal(ev.host, "::1");
    return MUNIT_OK;
}

static MunitResult test_parse_invalid_user_no_from(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    /* Malformed: "Invalid user" present but no " from " */
    int matched = parse_message("Invalid user admin", &ev);
    munit_assert_int(matched, ==, 0);
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

/* Without [preauth] suffix — should NOT match */
static MunitResult test_parse_connection_closed_auth_no_preauth(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Connection closed by authenticating user root 10.0.0.1 port 22", &ev);
    munit_assert_int(matched, ==, 0);
    return MUNIT_OK;
}

/* ========== parse_message: login patterns ========== */

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

static MunitResult test_parse_accepted_publickey_no_port(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message("Accepted publickey for bob from 10.0.0.5", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_LOGIN);
    munit_assert_string_equal(ev.user, "bob");
    munit_assert_string_equal(ev.host, "10.0.0.5");
    return MUNIT_OK;
}

/* ========== parse_message: logout patterns ========== */

static MunitResult test_parse_disconnected_from_user(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Disconnected from user deploy 192.168.1.100 port 44222", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_LOGOUT);
    munit_assert_string_equal(ev.user, "deploy");
    munit_assert_string_equal(ev.host, "192.168.1.100");
    return MUNIT_OK;
}

static MunitResult test_parse_disconnected_from_user_ipv6(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Disconnected from user root ::1 port 22", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_LOGOUT);
    munit_assert_string_equal(ev.user, "root");
    munit_assert_string_equal(ev.host, "::1");
    return MUNIT_OK;
}

static MunitResult test_parse_disconnected_from_user_no_port(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message("Disconnected from user alice 10.0.0.1", &ev);
    munit_assert_int(matched, ==, 1);
    munit_assert_int(ev.type, ==, EV_LOGOUT);
    munit_assert_string_equal(ev.user, "alice");
    munit_assert_string_equal(ev.host, "10.0.0.1");
    return MUNIT_OK;
}

/* "Disconnected from user" with no host — should NOT match */
static MunitResult test_parse_disconnected_no_host(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message("Disconnected from user alice", &ev);
    munit_assert_int(matched, ==, 0);
    return MUNIT_OK;
}

/* ========== parse_message: skip/no-match patterns ========== */

/* "Connection closed by invalid user" is skipped to avoid double-counting */
static MunitResult test_parse_connection_closed_invalid_skip(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    int matched = parse_message(
        "Connection closed by invalid user test 1.2.3.4 port 55000 [preauth]", &ev);
    munit_assert_int(matched, ==, 0);
    return MUNIT_OK;
}

static MunitResult test_parse_unrelated_messages(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    struct event ev;
    munit_assert_int(parse_message("Server listening on 0.0.0.0 port 22", &ev), ==, 0);
    munit_assert_int(parse_message("pam_unix(sshd:session): session opened", &ev), ==, 0);
    munit_assert_int(parse_message("Received disconnect from 1.2.3.4 port 55000", &ev), ==, 0);
    munit_assert_int(parse_message("", &ev), ==, 0);
    return MUNIT_OK;
}

/* ========== json_str ========== */

static MunitResult test_json_str_basic(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"MESSAGE\":\"hello world\",\"OTHER\":\"x\"}", "MESSAGE", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "hello world");
    return MUNIT_OK;
}

static MunitResult test_json_str_second_key(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"A\":\"first\",\"B\":\"second\"}", "B", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "second");
    return MUNIT_OK;
}

static MunitResult test_json_str_empty_value(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"KEY\":\"\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "");
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

static MunitResult test_json_str_escape_tab(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"KEY\":\"a\\tb\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "a\tb");
    return MUNIT_OK;
}

static MunitResult test_json_str_escape_slash(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    int ok = json_str("{\"KEY\":\"a\\/b\"}", "KEY", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "a/b");
    return MUNIT_OK;
}

static MunitResult test_json_str_missing_key(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    munit_assert_int(json_str("{\"OTHER\":\"val\"}", "MISSING", buf, sizeof(buf)), ==, 0);
    return MUNIT_OK;
}

static MunitResult test_json_str_empty_json(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    munit_assert_int(json_str("{}", "KEY", buf, sizeof(buf)), ==, 0);
    return MUNIT_OK;
}

static MunitResult test_json_str_buffer_too_small(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[4]; /* too small for "hello" (5 chars + null) */
    munit_assert_int(json_str("{\"KEY\":\"hello\"}", "KEY", buf, sizeof(buf)), ==, 0);
    return MUNIT_OK;
}

static MunitResult test_json_str_buffer_exact(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[4]; /* exactly fits "hi" (2 chars + null, bufsz=4 is fine) */
    int ok = json_str("{\"K\":\"hi\"}", "K", buf, sizeof(buf));
    munit_assert_int(ok, ==, 1);
    munit_assert_string_equal(buf, "hi");
    return MUNIT_OK;
}

/* Partial key match should not match (e.g. "MSG" should not match "MESSAGE") */
static MunitResult test_json_str_partial_key(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[256];
    munit_assert_int(json_str("{\"MESSAGE\":\"val\"}", "MSG", buf, sizeof(buf)), ==, 0);
    return MUNIT_OK;
}

/* ========== format_ts ========== */

static MunitResult test_format_ts_known(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[32];
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

static MunitResult test_format_ts_one_second(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[32];
    /* 1 second = 1000000 microseconds */
    format_ts("1000000", buf, sizeof(buf));
    munit_assert_string_equal(buf, "1970-01-01T00:00:01Z");
    return MUNIT_OK;
}

/* Subsecond precision is truncated to seconds */
static MunitResult test_format_ts_subsecond(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    char buf[32];
    format_ts("1772539305500000", buf, sizeof(buf)); /* +0.5s */
    munit_assert_string_equal(buf, "2026-03-03T12:01:45Z");
    return MUNIT_OK;
}

/* ========== emit_extra_fields ========== */

static MunitResult test_emit_extra_basic(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    const char *skip[] = {"MESSAGE", "__REALTIME_TIMESTAMP"};
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_extra_fields(
        "{\"MESSAGE\":\"hello\",\"__REALTIME_TIMESTAMP\":\"123\",\"_HOSTNAME\":\"vps\"}",
        skip, 2);
    capture_end(saved, pipefd);
    /* MESSAGE and __REALTIME_TIMESTAMP should be skipped */
    munit_assert_null(strstr(captured, "MESSAGE="));
    munit_assert_null(strstr(captured, "__REALTIME_TIMESTAMP="));
    munit_assert_not_null(strstr(captured, "_HOSTNAME=vps\n"));
    return MUNIT_OK;
}

static MunitResult test_emit_extra_skips_non_string(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_extra_fields("{\"num\":42,\"str\":\"val\"}", NULL, 0);
    capture_end(saved, pipefd);
    /* num is non-string, should be skipped */
    munit_assert_null(strstr(captured, "num="));
    munit_assert_not_null(strstr(captured, "str=val\n"));
    return MUNIT_OK;
}

static MunitResult test_emit_extra_empty(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_extra_fields("{}", NULL, 0);
    capture_end(saved, pipefd);
    munit_assert_int(captured_len, ==, 0);
    return MUNIT_OK;
}

static MunitResult test_emit_extra_escaped_value(const MunitParameter params[], void *data) {
    (void)params; (void)data;
    int saved, pipefd[2];
    capture_start(&saved, pipefd);
    emit_extra_fields("{\"key\":\"a\\nb\"}", NULL, 0);
    capture_end(saved, pipefd);
    munit_assert_not_null(strstr(captured, "key=a\nb\n"));
    return MUNIT_OK;
}

/* ========== test suite ========== */

static MunitTest tests[] = {
    /* parse_message: failure */
    {"/parse_message/invalid_user", test_parse_invalid_user, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/invalid_user_no_port", test_parse_invalid_user_no_port, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/invalid_user_ipv6", test_parse_invalid_user_ipv6, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/invalid_user_no_from", test_parse_invalid_user_no_from, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/connection_closed_auth", test_parse_connection_closed_auth, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/connection_closed_no_preauth", test_parse_connection_closed_auth_no_preauth, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_message: login */
    {"/parse_message/accepted_publickey", test_parse_accepted_publickey, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/accepted_password", test_parse_accepted_password, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/accepted_publickey_no_port", test_parse_accepted_publickey_no_port, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_message: logout */
    {"/parse_message/disconnected_from_user", test_parse_disconnected_from_user, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/disconnected_from_user_ipv6", test_parse_disconnected_from_user_ipv6, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/disconnected_from_user_no_port", test_parse_disconnected_from_user_no_port, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/disconnected_no_host", test_parse_disconnected_no_host, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* parse_message: skip/no-match */
    {"/parse_message/connection_closed_invalid_skip", test_parse_connection_closed_invalid_skip, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_message/unrelated", test_parse_unrelated_messages, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* json_str */
    {"/json_str/basic", test_json_str_basic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/second_key", test_json_str_second_key, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/empty_value", test_json_str_empty_value, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_quote", test_json_str_escape_quote, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_backslash", test_json_str_escape_backslash, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_newline", test_json_str_escape_newline, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_tab", test_json_str_escape_tab, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/escape_slash", test_json_str_escape_slash, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/missing_key", test_json_str_missing_key, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/empty_json", test_json_str_empty_json, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/buffer_too_small", test_json_str_buffer_too_small, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/buffer_exact", test_json_str_buffer_exact, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/json_str/partial_key", test_json_str_partial_key, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* format_ts */
    {"/format_ts/known", test_format_ts_known, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_ts/epoch", test_format_ts_epoch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_ts/one_second", test_format_ts_one_second, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/format_ts/subsecond", test_format_ts_subsecond, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* emit_extra_fields */
    {"/emit_extra/basic", test_emit_extra_basic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/emit_extra/skips_non_string", test_emit_extra_skips_non_string, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/emit_extra/empty", test_emit_extra_empty, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/emit_extra/escaped_value", test_emit_extra_escaped_value, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

static const MunitSuite suite = {
    "/ssh_journal", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};

int main(int argc, char *argv[]) {
    return munit_suite_main(&suite, NULL, argc, argv);
}
