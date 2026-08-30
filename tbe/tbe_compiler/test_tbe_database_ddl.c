#include "compiler_core.h"
#include "tinytest.h"

#include <sqlite3.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sqlite_ddl_test_state_s {
  sqlite3 *db;
  sqlite3_stmt *statement;
  char *sql_output_path;
  char *sql_text;
  char *sqlite_error;
} sqlite_ddl_test_state_t;

typedef struct expected_column_s {
  const char *name;
  const char *type;
} expected_column_t;

static char *sqlite_ddl_test_strdup(const char *text) {
  size_t size;
  char *copy;

  if (!text) return NULL;
  size = strlen(text) + 1u;
  copy = (char *)malloc(size);
  if (!copy) return NULL;
  memcpy(copy, text, size);
  return copy;
}

static size_t sqlite_ddl_test_live_resource_count(const sqlite_ddl_test_state_t *state) {
  size_t count = 0;

  if (!state) return 0;
  if (state->db) ++count;
  if (state->statement) ++count;
  if (state->sql_output_path) ++count;
  if (state->sql_text) ++count;
  if (state->sqlite_error) ++count;
  return count;
}

static void sqlite_ddl_test_reset_statement(sqlite_ddl_test_state_t *state) {
  if (state && state->statement) {
    sqlite3_finalize(state->statement);
    state->statement = NULL;
  }
}

static void sqlite_ddl_test_reset_error(sqlite_ddl_test_state_t *state) {
  if (state && state->sqlite_error) {
    sqlite3_free(state->sqlite_error);
    state->sqlite_error = NULL;
  }
}

static void sqlite_ddl_test_cleanup(sqlite_ddl_test_state_t *state) {
  if (!state) return;

  sqlite_ddl_test_reset_statement(state);
  sqlite_ddl_test_reset_error(state);

  if (state->db) {
    sqlite3_close(state->db);
    state->db = NULL;
  }

  free(state->sql_text);
  state->sql_text = NULL;

  if (state->sql_output_path) {
    (void)tt_remove_file(state->sql_output_path);
    free(state->sql_output_path);
    state->sql_output_path = NULL;
  }
}

static int sqlite_ddl_test_exec(sqlite_ddl_test_state_t *state, const char *sql) {
  sqlite_ddl_test_reset_error(state);
  return sqlite3_exec(state->db, sql, NULL, NULL, &state->sqlite_error);
}

static int sqlite_ddl_test_prepare(sqlite_ddl_test_state_t *state, const char *sql) {
  int rc;

  sqlite_ddl_test_reset_statement(state);
  rc = sqlite3_prepare_v2(state->db, sql, -1, &state->statement, NULL);
  if (rc != SQLITE_OK) {
    info("prepare failed: sql=%s error=%s", sql, sqlite3_errmsg(state->db));
    return 0;
  }
  return 1;
}

static void sqlite_ddl_test_expect_exec_ok(sqlite_ddl_test_state_t *state, const char *sql) {
  int rc = sqlite_ddl_test_exec(state, sql);

  info("sql=%s error=%s", sql,
       state->sqlite_error ? state->sqlite_error : sqlite3_errmsg(state->db));
  check_equal(rc, SQLITE_OK);
}

static void sqlite_ddl_test_expect_constraint(sqlite_ddl_test_state_t *state, const char *sql,
                                              int expected_extended_code) {
  int rc = sqlite_ddl_test_exec(state, sql);
  int extended_code = sqlite3_extended_errcode(state->db);

  info("sql=%s error=%s", sql,
       state->sqlite_error ? state->sqlite_error : sqlite3_errmsg(state->db));
  check_equal(extended_code & 0xff, SQLITE_CONSTRAINT);
  check_equal(rc, expected_extended_code);
  check_equal(extended_code, expected_extended_code);
}

static void sqlite_ddl_test_expect_bound_u64_ok(sqlite_ddl_test_state_t *state,
                                                const unsigned char *value, int value_size) {
  int rc;

  check(sqlite_ddl_test_prepare(
      state, "INSERT INTO integer_limits (exact_u64, nullable_u32) VALUES (?1, NULL);"));
  if (!state->statement) return;

  rc = sqlite3_bind_text(state->statement, 1, (const char *)value, value_size, SQLITE_STATIC);
  check_equal(rc, SQLITE_OK);
  if (rc != SQLITE_OK) return;

  rc = sqlite3_step(state->statement);
  info("bound u64 size=%d step=%d error=%s", value_size, rc, sqlite3_errmsg(state->db));
  check_equal(rc, SQLITE_DONE);
}

static void sqlite_ddl_test_expect_bound_u64_constraint(sqlite_ddl_test_state_t *state,
                                                        const unsigned char *value,
                                                        int value_size) {
  int rc;

  check(sqlite_ddl_test_prepare(
      state, "INSERT INTO integer_limits (exact_u64, nullable_u32) VALUES (?1, NULL);"));
  if (!state->statement) return;

  rc = sqlite3_bind_text(state->statement, 1, (const char *)value, value_size, SQLITE_STATIC);
  check_equal(rc, SQLITE_OK);
  if (rc != SQLITE_OK) return;

  rc = sqlite3_step(state->statement);
  info("bound u64 bytes=%02x %02x %02x size=%d step=%d error=%s", value[0], value[1], value[2],
       value_size, rc, sqlite3_errmsg(state->db));
  check_equal(rc, SQLITE_CONSTRAINT_CHECK);
  check_equal(sqlite3_extended_errcode(state->db), SQLITE_CONSTRAINT_CHECK);
}

static void sqlite_ddl_test_check_table_exists(sqlite_ddl_test_state_t *state,
                                               const char *table_name) {
  char sql[256];
  int rc;

  snprintf(sql, sizeof(sql),
           "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='%s';", table_name);
  check(sqlite_ddl_test_prepare(state, sql));
  if (!state->statement) return;

  rc = sqlite3_step(state->statement);
  info("table=%s step=%d", table_name, rc);
  check_equal(rc, SQLITE_ROW);
  if (rc == SQLITE_ROW) {
    check_equal(sqlite3_column_int(state->statement, 0), 1);
  }
}

static void sqlite_ddl_test_check_index_exists(sqlite_ddl_test_state_t *state,
                                               const char *index_name) {
  char sql[256];
  int rc;

  snprintf(sql, sizeof(sql),
           "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='%s';", index_name);
  check(sqlite_ddl_test_prepare(state, sql));
  if (!state->statement) return;

  rc = sqlite3_step(state->statement);
  info("index=%s step=%d", index_name, rc);
  check_equal(rc, SQLITE_ROW);
  if (rc == SQLITE_ROW) {
    check_equal(sqlite3_column_int(state->statement, 0), 1);
  }
}

static void sqlite_ddl_test_check_table_sql_contains(sqlite_ddl_test_state_t *state,
                                                     const char *table_name,
                                                     const char *needle) {
  char sql[256];
  const unsigned char *text;
  int rc;

  snprintf(sql, sizeof(sql),
           "SELECT sql FROM sqlite_master WHERE type='table' AND name='%s';", table_name);
  check(sqlite_ddl_test_prepare(state, sql));
  if (!state->statement) return;

  rc = sqlite3_step(state->statement);
  info("table=%s step=%d", table_name, rc);
  check_equal(rc, SQLITE_ROW);
  if (rc != SQLITE_ROW) return;

  text = sqlite3_column_text(state->statement, 0);
  check_not_null(text);
  if (text) {
    check_contains((const char *)text, needle);
  }
}

static void sqlite_ddl_test_check_table_columns(sqlite_ddl_test_state_t *state,
                                                const char *table_name,
                                                const expected_column_t *expected_columns,
                                                size_t expected_count) {
  char sql[256];

  snprintf(sql, sizeof(sql), "PRAGMA table_info(\"%s\");", table_name);
  check(sqlite_ddl_test_prepare(state, sql));
  if (!state->statement) return;

  for (size_t index = 0; index < expected_count; ++index) {
    const unsigned char *name;
    const unsigned char *type;
    int rc = sqlite3_step(state->statement);

    info("table=%s column_index=%zu step=%d", table_name, index, rc);
    check_equal(rc, SQLITE_ROW);
    if (rc != SQLITE_ROW) return;

    name = sqlite3_column_text(state->statement, 1);
    type = sqlite3_column_text(state->statement, 2);
    check_not_null(name);
    check_not_null(type);
    if (name) check_equal((const char *)name, expected_columns[index].name);
    if (type) check_equal((const char *)type, expected_columns[index].type);
  }

  check_equal(sqlite3_step(state->statement), SQLITE_DONE);
}

static void sqlite_ddl_test_populate_cleanup_probe(sqlite_ddl_test_state_t *state) {
  int rc;

  check_not_null(state);
  if (!state) return;

  state->sql_output_path = tt_make_temp_file("tbe_database_probe", ".sql");
  check_not_null(state->sql_output_path);
  if (state->sql_output_path) {
    check_equal(tt_write_file(state->sql_output_path, "probe", 5), 0);
  }

  state->sql_text = sqlite_ddl_test_strdup("SELECT 1;");
  check_not_null(state->sql_text);

  rc = sqlite3_open(":memory:", &state->db);
  info("probe sqlite3_open rc=%d error=%s", rc,
       state->db ? sqlite3_errmsg(state->db) : "no-handle");
  check_equal(rc, SQLITE_OK);

  check(sqlite_ddl_test_prepare(state, "SELECT 1;"));
  rc = sqlite_ddl_test_exec(state, "SELECT * FROM missing_cleanup_probe;");
  info("probe exec rc=%d error=%s", rc,
       state->sqlite_error ? state->sqlite_error : sqlite3_errmsg(state->db));
  check_equal(sqlite3_errcode(state->db), SQLITE_ERROR);
  check_not_null(state->sqlite_error);
}

spec("tbe_compiler SQLite DDL integration") {
  static sqlite_ddl_test_state_t state = {0};
  static size_t cleanup_probe_after_each_runs = 0;
  static size_t cleanup_probe_resource_count_before = 0;
  static size_t cleanup_probe_resource_count_after = 0;
  static int cleanup_probe_armed = 0;

  before_each() { memset(&state, 0, sizeof(state)); }

  after_each() {
    if (cleanup_probe_armed) {
      ++cleanup_probe_after_each_runs;
      cleanup_probe_resource_count_before = sqlite_ddl_test_live_resource_count(&state);
    }

    sqlite_ddl_test_cleanup(&state);

    if (cleanup_probe_armed) {
      cleanup_probe_resource_count_after = sqlite_ddl_test_live_resource_count(&state);
      cleanup_probe_armed = 0;
    }
  }

  it_should_fail("runs after_each cleanup after assertion longjmp") {
    sqlite_ddl_test_populate_cleanup_probe(&state);
    cleanup_probe_armed = 1;
    check(false);
  }

  it("observes fixture cleanup after the expected failure") {
    check_equal(cleanup_probe_after_each_runs, (size_t)1);
    check_equal(cleanup_probe_resource_count_before, (size_t)5);
    check_equal(cleanup_probe_resource_count_after, (size_t)0);
  }

  it("executes generated SQLite schema and enforces live constraints") {
    static const unsigned char canonical_one[] = {'1'};
    static const unsigned char embedded_nul_x[] = {'1', 0x00, 'x'};
    static const unsigned char embedded_nul_two[] = {'1', 0x00, '2'};
    static const unsigned char non_ascii_digit_suffix[] = {'1', 0xc3, 0xa9};
    static const expected_column_t user_columns[] = {
        {"user_id", "INTEGER"},
        {"email", "TEXT"},
        {"display_name", "TEXT"},
        {"active", "INTEGER"},
        {"rank", "INTEGER"},
    };
    static const expected_column_t session_columns[] = {
        {"session_id", "INTEGER"},
        {"user_id", "INTEGER"},
        {"token", "TEXT"},
    };
    static const expected_column_t integer_limit_columns[] = {
        {"exact_u64", "TEXT"},
        {"nullable_u32", "INTEGER"},
    };
    sqlite3_int64 generated_user_id = 0;
    int rc;
    size_t sql_size = 0;
    char session_insert_sql[256];
    tbe_compiler_options_t options = {
        .schema_path = TEST_DATABASE_SCHEMA_FILE,
        .output_path = NULL,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_SQLITE,
    };

    state.sql_output_path = tt_make_temp_file("tbe_database_schema", ".sql");
    check_not_null(state.sql_output_path);
    check_equal(tt_remove_file(state.sql_output_path), 0);

    options.output_path = state.sql_output_path;
    check_equal(tbe_compiler_run(&options), 0);

    state.sql_text = tt_read_file(state.sql_output_path, &sql_size);
    check_not_null(state.sql_text);
    check_greater(sql_size, (size_t)0);
    check_contains(state.sql_text, "CREATE TABLE \"users\"");
    check_contains(state.sql_text, "CREATE TABLE \"sessions\"");
    check_contains(state.sql_text, "CREATE TABLE \"integer_limits\"");
    check_contains(state.sql_text, "CONSTRAINT \"fk_sessions_user\" FOREIGN KEY");
    check_contains(state.sql_text, "CONSTRAINT \"ck_sessions_token\" CHECK");
    check_contains(state.sql_text, "CREATE INDEX \"idx_sessions_token_user\"");
    check_contains(state.sql_text, "CREATE UNIQUE INDEX \"uidx_sessions_user_token\"");
    check_contains(state.sql_text, "INSERT INTO users");

    rc = sqlite3_open(":memory:", &state.db);
    info("sqlite3_open rc=%d error=%s", rc,
         state.db ? sqlite3_errmsg(state.db) : "no-handle");
    check_equal(rc, SQLITE_OK);

    check_equal(sqlite3_extended_result_codes(state.db, 1), SQLITE_OK);
    sqlite_ddl_test_expect_exec_ok(&state, "PRAGMA foreign_keys=ON;");
    sqlite_ddl_test_expect_exec_ok(&state, state.sql_text);

    sqlite_ddl_test_check_table_exists(&state, "users");
    sqlite_ddl_test_check_table_exists(&state, "sessions");
    sqlite_ddl_test_check_table_exists(&state, "integer_limits");
    sqlite_ddl_test_check_index_exists(&state, "idx_sessions_token_user");
    sqlite_ddl_test_check_index_exists(&state, "uidx_sessions_user_token");
    sqlite_ddl_test_check_table_sql_contains(&state, "users", "AUTOINCREMENT");
    sqlite_ddl_test_check_table_columns(&state, "users", user_columns,
                                        sizeof(user_columns) / sizeof(user_columns[0]));
    sqlite_ddl_test_check_table_columns(&state, "sessions", session_columns,
                                        sizeof(session_columns) / sizeof(session_columns[0]));
    sqlite_ddl_test_check_table_columns(
        &state, "integer_limits", integer_limit_columns,
        sizeof(integer_limit_columns) / sizeof(integer_limit_columns[0]));

    check(sqlite_ddl_test_prepare(
        &state, "SELECT display_name, rank FROM users WHERE email='seed@example.com';"));
    rc = sqlite3_step(state.statement);
    check_equal(rc, SQLITE_ROW);
    if (rc == SQLITE_ROW) {
      check_equal((const char *)sqlite3_column_text(state.statement, 0), "Seed");
      check_equal(sqlite3_column_int(state.statement, 1), 4);
    }
    check_equal(sqlite3_step(state.statement), SQLITE_DONE);

    sqlite_ddl_test_expect_exec_ok(
        &state,
        "INSERT INTO users (email, display_name, active, rank) "
        "VALUES ('alpha@example.com', 'Alpha', 1, 7);");

    check(sqlite_ddl_test_prepare(
        &state,
        "SELECT user_id, email, display_name, active, rank "
        "FROM users WHERE email='alpha@example.com';"));

    rc = sqlite3_step(state.statement);
    info("select generated user row step=%d", rc);
    check_equal(rc, SQLITE_ROW);

    generated_user_id = sqlite3_column_int64(state.statement, 0);
    check_greater(generated_user_id, (sqlite3_int64)0);
    check_equal((const char *)sqlite3_column_text(state.statement, 1), "alpha@example.com");
    check_equal((const char *)sqlite3_column_text(state.statement, 2), "Alpha");
    check_equal(sqlite3_column_int(state.statement, 3), 1);
    check_equal(sqlite3_column_int(state.statement, 4), 7);
    check_equal(sqlite3_step(state.statement), SQLITE_DONE);

    snprintf(session_insert_sql, sizeof(session_insert_sql),
             "INSERT INTO sessions (session_id, user_id, token) "
             "VALUES (100, %lld, 'tok-100');",
             (long long)generated_user_id);
    sqlite_ddl_test_expect_exec_ok(&state, session_insert_sql);

    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO sessions (session_id, user_id, token) "
        "VALUES (100, 999, 'tok-duplicate');",
        SQLITE_CONSTRAINT_PRIMARYKEY);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO sessions (session_id, user_id, token) "
        "VALUES (101, 999999, 'orphan-token');",
        SQLITE_CONSTRAINT_FOREIGNKEY);
    snprintf(session_insert_sql, sizeof(session_insert_sql),
             "INSERT INTO sessions (session_id, user_id, token) "
             "VALUES (102, %lld, 'bad');",
             (long long)generated_user_id);
    sqlite_ddl_test_expect_constraint(&state, session_insert_sql, SQLITE_CONSTRAINT_CHECK);
    snprintf(session_insert_sql, sizeof(session_insert_sql),
             "INSERT INTO sessions (session_id, user_id, token) "
             "VALUES (103, %lld, 'tok-100');",
             (long long)generated_user_id);
    sqlite_ddl_test_expect_constraint(&state, session_insert_sql, SQLITE_CONSTRAINT_UNIQUE);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO users (email, display_name, active, rank) "
        "VALUES ('alpha@example.com', 'Another', 1, 9);",
        SQLITE_CONSTRAINT_UNIQUE);

    snprintf(session_insert_sql, sizeof(session_insert_sql),
             "INSERT INTO users (user_id, email, display_name, active, rank) "
             "VALUES (%lld, 'explicit-id@example.com', 'Explicit', 1, 5);",
             (long long)generated_user_id);
    sqlite_ddl_test_expect_constraint(&state, session_insert_sql,
                                      SQLITE_CONSTRAINT_PRIMARYKEY);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO users (email, active, rank) "
        "VALUES ('missing-name@example.com', 1, 3);",
        SQLITE_CONSTRAINT_NOTNULL);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO users (email, display_name, active, rank) "
        "VALUES ('bad-bool@example.com', 'Bad Bool', 2, 3);",
        SQLITE_CONSTRAINT_CHECK);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO users (email, display_name, active, rank) "
        "VALUES ('bad-rank@example.com', 'Bad Rank', 1, 256);",
        SQLITE_CONSTRAINT_CHECK);

    sqlite_ddl_test_expect_exec_ok(
        &state,
        "INSERT INTO integer_limits (exact_u64, nullable_u32) "
        "VALUES ('18446744073709551615', NULL);");
    check(sqlite_ddl_test_prepare(
        &state,
        "SELECT exact_u64, typeof(exact_u64), nullable_u32 "
        "FROM integer_limits WHERE exact_u64='18446744073709551615';"));
    rc = sqlite3_step(state.statement);
    check_equal(rc, SQLITE_ROW);
    if (rc == SQLITE_ROW) {
      check_equal((const char *)sqlite3_column_text(state.statement, 0),
                  "18446744073709551615");
      check_equal((const char *)sqlite3_column_text(state.statement, 1), "text");
      check_equal(sqlite3_column_type(state.statement, 2), SQLITE_NULL);
    }
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO integer_limits (exact_u64, nullable_u32) "
        "VALUES ('18446744073709551616', NULL);",
        SQLITE_CONSTRAINT_CHECK);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO integer_limits (exact_u64, nullable_u32) VALUES (1.5, NULL);",
        SQLITE_CONSTRAINT_CHECK);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO integer_limits (exact_u64, nullable_u32) "
        "VALUES (18446744073709551615, NULL);",
        SQLITE_CONSTRAINT_CHECK);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO integer_limits (exact_u64, nullable_u32) VALUES ('00', NULL);",
        SQLITE_CONSTRAINT_CHECK);
    sqlite_ddl_test_expect_constraint(
        &state,
        "INSERT INTO integer_limits (exact_u64, nullable_u32) VALUES ('1', 1.5);",
        SQLITE_CONSTRAINT_CHECK);
    sqlite_ddl_test_expect_bound_u64_ok(&state, canonical_one, (int)sizeof(canonical_one));
    sqlite_ddl_test_expect_bound_u64_constraint(&state, embedded_nul_x,
                                                (int)sizeof(embedded_nul_x));
    sqlite_ddl_test_expect_bound_u64_constraint(&state, embedded_nul_two,
                                                (int)sizeof(embedded_nul_two));
    sqlite_ddl_test_expect_bound_u64_constraint(&state, non_ascii_digit_suffix,
                                                (int)sizeof(non_ascii_digit_suffix));
  }
}
