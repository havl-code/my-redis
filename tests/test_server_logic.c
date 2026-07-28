// Unit tests for the pure logic inside server.c: request parsing, integer
// parsing, hash table operations, and command dispatch. None of this needs a
// live socket or root, unlike accept_new_conn/try_fill_buffer/connection_io,
// which are exercised instead by actually running the server and client
// together (see the example session in the README).
//
// This file includes server.c directly so the tests can reach its static
// functions without changing server.c's structure or adding a build system.
// main is renamed via macro first so there's no clash with this file's own
// main below; server_main_unused is never actually called.
#define main server_main_unused
#include "../server.c"
#undef main

#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, desc)                                              \
    do {                                                               \
        tests_run++;                                                   \
        if (!(cond)) {                                                 \
            tests_failed++;                                            \
            printf("FAIL: %s (%s:%d)\n", desc, __FILE__, __LINE__);    \
        }                                                               \
    } while (0)

// wipe every bucket of the hash table so tests don't leak state into each other
static void clear_htable(void) {
    for (int i = 0; i < HTABLE_SIZE; i++) {
        Entry *e = htable[i];
        while (e) {
            Entry *next = e->next;
            free(e->key);
            free(e->val);
            free(e);
            e = next;
        }
        htable[i] = NULL;
    }
}

// build an Arg pointing at a C string literal, for convenience in tests
static Arg mkarg(const char *s) {
    Arg a;
    a.len = (uint32_t)strlen(s);
    a.data = (const uint8_t *)s;
    return a;
}

// read the 1-byte type tag out of a response buffer built by do_request/out_*
static uint8_t resp_type(const uint8_t *buf) {
    return buf[0];
}

// ---- parse_req ----

static void test_parse_req_single_string(void) {
    // wire body for a single-string request: [nstr=1][len=3]"get"
    uint8_t body[4 + 4 + 3];
    uint32_t nstr = 1, slen = 3;
    memcpy(body, &nstr, 4);
    memcpy(body + 4, &slen, 4);
    memcpy(body + 8, "get", 3);

    Arg args[MAX_ARGS];
    uint32_t out_nstr = 0;
    int32_t rv = parse_req(body, sizeof(body), &out_nstr, args, MAX_ARGS);

    CHECK(rv == 0, "parse_req accepts a well-formed single-string body");
    CHECK(out_nstr == 1, "parse_req reports the correct string count");
    CHECK(args[0].len == 3 && memcmp(args[0].data, "get", 3) == 0, "parse_req extracts the string correctly");
}

static void test_parse_req_multi_string(void) {
    // wire body for "set key1 hello"
    const char *strs[3] = {"set", "key1", "hello"};
    uint8_t body[128];
    uint32_t nstr = 3;
    memcpy(body, &nstr, 4);
    size_t pos = 4;
    for (int i = 0; i < 3; i++) {
        uint32_t slen = (uint32_t)strlen(strs[i]);
        memcpy(body + pos, &slen, 4);
        pos += 4;
        memcpy(body + pos, strs[i], slen);
        pos += slen;
    }

    Arg args[MAX_ARGS];
    uint32_t out_nstr = 0;
    int32_t rv = parse_req(body, pos, &out_nstr, args, MAX_ARGS);

    CHECK(rv == 0, "parse_req accepts a well-formed multi-string body");
    CHECK(out_nstr == 3, "parse_req reports 3 strings for 'set key1 hello'");
    CHECK(args[0].len == 3 && memcmp(args[0].data, "set", 3) == 0, "parse_req extracts arg 0 correctly");
    CHECK(args[1].len == 4 && memcmp(args[1].data, "key1", 4) == 0, "parse_req extracts arg 1 correctly");
    CHECK(args[2].len == 5 && memcmp(args[2].data, "hello", 5) == 0, "parse_req extracts arg 2 correctly");
}

static void test_parse_req_rejects_short_header(void) {
    uint8_t body[2] = {0, 0};   // shorter than the 4 bytes needed just for nstr
    Arg args[MAX_ARGS];
    uint32_t out_nstr = 0;
    int32_t rv = parse_req(body, sizeof(body), &out_nstr, args, MAX_ARGS);
    CHECK(rv == -1, "parse_req rejects a body too short to even hold nstr");
}

static void test_parse_req_rejects_string_overrunning_body(void) {
    // nstr=1, claims the string is 50 bytes, but the body only has 3 bytes of string data
    uint8_t body[4 + 4 + 3];
    uint32_t nstr = 1, slen = 50;
    memcpy(body, &nstr, 4);
    memcpy(body + 4, &slen, 4);
    memcpy(body + 8, "get", 3);

    Arg args[MAX_ARGS];
    uint32_t out_nstr = 0;
    int32_t rv = parse_req(body, sizeof(body), &out_nstr, args, MAX_ARGS);
    CHECK(rv == -1, "parse_req rejects a string whose length would run past the body");
}

static void test_parse_req_rejects_too_many_args(void) {
    uint8_t body[4];
    uint32_t nstr = 999;   // above MAX_ARGS
    memcpy(body, &nstr, 4);

    Arg args[MAX_ARGS];
    uint32_t out_nstr = 0;
    int32_t rv = parse_req(body, sizeof(body), &out_nstr, args, MAX_ARGS);
    CHECK(rv == -1, "parse_req rejects nstr above max_args");
}

static void test_parse_req_rejects_trailing_garbage(void) {
    // a valid single string, plus 3 extra bytes that don't belong to anything
    uint8_t body[4 + 4 + 3 + 3];
    uint32_t nstr = 1, slen = 3;
    memcpy(body, &nstr, 4);
    memcpy(body + 4, &slen, 4);
    memcpy(body + 8, "get", 3);
    memcpy(body + 11, "xyz", 3);

    Arg args[MAX_ARGS];
    uint32_t out_nstr = 0;
    int32_t rv = parse_req(body, sizeof(body), &out_nstr, args, MAX_ARGS);
    CHECK(rv == -1, "parse_req rejects trailing bytes that don't belong to any string");
}

// ---- arg_to_i64 ----

static void test_arg_to_i64_positive(void) {
    Arg a = mkarg("42");
    int64_t out = 0;
    CHECK(arg_to_i64(&a, &out) && out == 42, "arg_to_i64 parses a positive integer");
}

static void test_arg_to_i64_negative(void) {
    Arg a = mkarg("-7");
    int64_t out = 0;
    CHECK(arg_to_i64(&a, &out) && out == -7, "arg_to_i64 parses a negative integer");
}

static void test_arg_to_i64_rejects_non_numeric(void) {
    Arg a = mkarg("abc");
    int64_t out = 0;
    CHECK(!arg_to_i64(&a, &out), "arg_to_i64 rejects non-numeric input");
}

static void test_arg_to_i64_rejects_trailing_garbage(void) {
    Arg a = mkarg("42abc");
    int64_t out = 0;
    CHECK(!arg_to_i64(&a, &out), "arg_to_i64 rejects a number with trailing non-numeric characters");
}

static void test_arg_to_i64_rejects_empty(void) {
    Arg a = mkarg("");
    int64_t out = 0;
    CHECK(!arg_to_i64(&a, &out), "arg_to_i64 rejects an empty argument");
}

// ---- arg_is ----

static void test_arg_is_case_insensitive(void) {
    Arg a = mkarg("GeT");
    CHECK(arg_is(&a, "get"), "arg_is matches regardless of case");
}

static void test_arg_is_rejects_wrong_command(void) {
    Arg a = mkarg("get");
    CHECK(!arg_is(&a, "set"), "arg_is rejects a non-matching command");
}

static void test_arg_is_rejects_prefix_match(void) {
    Arg a = mkarg("gets");   // longer than "get", must not match
    CHECK(!arg_is(&a, "get"), "arg_is does not match on a partial/prefix string");
}

// ---- hash table ----

static void test_hashtable_set_and_get(void) {
    clear_htable();
    h_set((const uint8_t *)"key1", 4, (const uint8_t *)"hello", 5);
    Entry *e = h_lookup((const uint8_t *)"key1", 4);
    CHECK(e != NULL, "h_lookup finds a key that was just set");
    CHECK(e && e->vlen == 5 && memcmp(e->val, "hello", 5) == 0, "h_lookup returns the correct value");
}

static void test_hashtable_overwrite_resets_ttl(void) {
    clear_htable();
    h_set((const uint8_t *)"key1", 4, (const uint8_t *)"hello", 5);
    Entry *e = h_lookup((const uint8_t *)"key1", 4);
    e->expire_at = time(NULL) + 100;   // give it a TTL directly
    h_set((const uint8_t *)"key1", 4, (const uint8_t *)"world", 5);   // overwrite
    e = h_lookup((const uint8_t *)"key1", 4);
    CHECK(e && e->expire_at == 0, "h_set clears any existing TTL on overwrite (matches Redis semantics)");
    CHECK(e && e->vlen == 5 && memcmp(e->val, "world", 5) == 0, "h_set actually updates the stored value");
}

static void test_hashtable_delete(void) {
    clear_htable();
    h_set((const uint8_t *)"key1", 4, (const uint8_t *)"hello", 5);
    CHECK(h_del((const uint8_t *)"key1", 4), "h_del reports success when the key exists");
    CHECK(h_lookup((const uint8_t *)"key1", 4) == NULL, "the key is actually gone after h_del");
    CHECK(!h_del((const uint8_t *)"key1", 4), "h_del reports failure on a second delete of the same key");
}

static void test_hashtable_lazy_expiry(void) {
    clear_htable();
    h_set((const uint8_t *)"key1", 4, (const uint8_t *)"hello", 5);
    Entry *e = h_lookup((const uint8_t *)"key1", 4);
    e->expire_at = time(NULL) - 5;   // force it into the past
    CHECK(h_lookup((const uint8_t *)"key1", 4) == NULL, "h_lookup treats a key past its expire_at as missing");
}

// ---- do_request (command dispatch) ----

static void test_do_request_set_get_del(void) {
    clear_htable();
    uint8_t out[MAX_MSG_SIZE];

    Arg set_args[3] = {mkarg("set"), mkarg("key1"), mkarg("hello")};
    do_request(set_args, 3, out);
    CHECK(resp_type(out) == RES_STR && memcmp(out + 1, "OK", 2) == 0, "SET returns string OK");

    Arg get_args[2] = {mkarg("get"), mkarg("key1")};
    do_request(get_args, 2, out);
    CHECK(resp_type(out) == RES_STR && memcmp(out + 1, "hello", 5) == 0, "GET returns the stored value");

    Arg del_args[2] = {mkarg("del"), mkarg("key1")};
    do_request(del_args, 2, out);
    int64_t delval = 0;
    memcpy(&delval, out + 1, 8);
    CHECK(resp_type(out) == RES_INT && delval == 1, "DEL returns integer 1 when a key was actually deleted");

    do_request(get_args, 2, out);
    CHECK(resp_type(out) == RES_NIL, "GET returns nil after the key has been deleted");
}

static void test_do_request_unknown_command(void) {
    clear_htable();
    uint8_t out[MAX_MSG_SIZE];
    Arg args[1] = {mkarg("bogus")};
    do_request(args, 1, out);
    uint32_t code = 0;
    memcpy(&code, out + 1, 4);
    CHECK(resp_type(out) == RES_ERR && code == ERR_UNKNOWN_CMD, "an unrecognised command returns ERR_UNKNOWN_CMD");
}

static void test_do_request_wrong_arg_count(void) {
    clear_htable();
    uint8_t out[MAX_MSG_SIZE];
    Arg args[1] = {mkarg("get")};   // GET needs a key argument, this has none
    do_request(args, 1, out);
    uint32_t code = 0;
    memcpy(&code, out + 1, 4);
    CHECK(resp_type(out) == RES_ERR && code == ERR_BAD_ARGS, "GET with the wrong number of arguments returns ERR_BAD_ARGS");
}

static void test_do_request_expire_and_ttl(void) {
    clear_htable();
    uint8_t out[MAX_MSG_SIZE];

    Arg set_args[3] = {mkarg("set"), mkarg("key1"), mkarg("hello")};
    do_request(set_args, 3, out);

    Arg ttl_args[2] = {mkarg("ttl"), mkarg("key1")};
    do_request(ttl_args, 2, out);
    int64_t ttl_val = 0;
    memcpy(&ttl_val, out + 1, 8);
    CHECK(resp_type(out) == RES_INT && ttl_val == -1, "TTL on a key with no expiry returns -1");

    Arg expire_args[3] = {mkarg("expire"), mkarg("key1"), mkarg("60")};
    do_request(expire_args, 3, out);
    int64_t expire_val = 0;
    memcpy(&expire_val, out + 1, 8);
    CHECK(resp_type(out) == RES_INT && expire_val == 1, "EXPIRE on an existing key returns 1");

    do_request(ttl_args, 2, out);
    memcpy(&ttl_val, out + 1, 8);
    CHECK(resp_type(out) == RES_INT && ttl_val > 0 && ttl_val <= 60, "TTL reports a sensible remaining time after EXPIRE");

    Arg ttl_missing[2] = {mkarg("ttl"), mkarg("nosuchkey")};
    do_request(ttl_missing, 2, out);
    memcpy(&ttl_val, out + 1, 8);
    CHECK(resp_type(out) == RES_INT && ttl_val == -2, "TTL on a nonexistent key returns -2");
}

int main(void) {
    test_parse_req_single_string();
    test_parse_req_multi_string();
    test_parse_req_rejects_short_header();
    test_parse_req_rejects_string_overrunning_body();
    test_parse_req_rejects_too_many_args();
    test_parse_req_rejects_trailing_garbage();

    test_arg_to_i64_positive();
    test_arg_to_i64_negative();
    test_arg_to_i64_rejects_non_numeric();
    test_arg_to_i64_rejects_trailing_garbage();
    test_arg_to_i64_rejects_empty();

    test_arg_is_case_insensitive();
    test_arg_is_rejects_wrong_command();
    test_arg_is_rejects_prefix_match();

    test_hashtable_set_and_get();
    test_hashtable_overwrite_resets_ttl();
    test_hashtable_delete();
    test_hashtable_lazy_expiry();

    test_do_request_set_get_del();
    test_do_request_unknown_command();
    test_do_request_wrong_arg_count();
    test_do_request_expire_and_ttl();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}