Warning: truncated output (original token count: 40244)
Total output lines: 3604

#include "mustache.h"
#include "mustache_helpers.h"
#include "compiler_core.h"
#include "database_schema.h"
#include "node_tree.h"
#include "tbe_wire.h"
#include "schema_parser_dsl.h"
#include "tinytest.h"
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define tt_close _close
#define tt_dup _dup
#define tt_dup2 _dup2
#define tt_fileno _fileno
#define TT_NULL_DEVICE "NUL"
#else
#define tt_close close
#define tt_dup dup
#define tt_dup2 dup2
#define tt_fileno fileno
#define TT_NULL_DEVICE "/dev/null"
#endif

static void cleanup_test_file(const char *path);
static Node *build_database_ir_from_schema_with_diagnostic(
    const char *schema, tbe_database_dialect_t dialect, tbe_database_schema_status_t *status,
    tbe_database_schema_diagnostic_t *diagnostic);

static Node *find_child(Node *parent, const char *name) {
  if (!parent || parent->type != NODE_MAP) return NULL;

  for (size_t i = 0; i < parent->data.map.count; ++i) {
    Node *child = parent->data.map.items[i];
    if (child->name && strcmp(child->name, name) == 0) return child;
  }

  return NULL;
}

static int replace_test_string(Node *map, const char *name, const char *value) {
  Node *node = find_child(map, name);
  char *replacement;
  size_t length;

  if (!node || node->type != NODE_STRING || !value) return -1;
  length = strlen(value);
  replacement = (char *)malloc(length + 1u);
  if (!replacement) return -1;
  memcpy(replacement, value, length + 1u);
  free(node->data.string_val);
  node->data.string_val = replacement;
  return 0;
}

static Node *build_malformed_database_message_schema(int fields_as_string) {
  Node *root = create_node_map("root");
  Node *messages = create_node_list("messages");
  Node *message = create_node_map(NULL);
  Node *name = create_node_string("name", "Malformed");
  Node *fields = fields_as_string ? create_node_string("fields", "not-a-list") : NULL;

  if (!root || !messages || !message || !name || (fields_as_string && !fields)) goto cleanup;
  if (map_add(message, name) != 0) goto cleanup;
  name = NULL;
  if (fields && map_add(message, fields) != 0) goto cleanup;
  fields = NULL;
  if (list_add(messages, message) != 0) goto cleanup;
  message = NULL;
  if (map_add(root, messages) != 0) goto cleanup;
  messages = NULL;
  return root;

cleanup:
  node_free(fields);
  node_free(name);
  node_free(message);
  node_free(messages);
  node_free(root);
  return NULL;
}

static Node *build_schema_with_null_database_annotation_value(void) {
  static const char schema[] =
      "[db_table(records)] message Record { [db_unique(1)] int32 value; }";
  Node *root = create_node_map("root");
  Node *messages;
  Node *fields;
  Node *attributes;
  Node *value;

  if (!root || parse_schema(schema, sizeof(schema) - 1u, root, NULL) != 0) {
    node_free(root);
    return NULL;
  }
  messages = find_child(root, "messages");
  fields = messages && messages->type == NODE_LIST && messages->data.list.count == 1u
               ? find_child(messages->data.list.items[0], "fields")
               : NULL;
  attributes = fields && fields->type == NODE_LIST && fields->data.list.count == 1u
                   ? find_child(fields->data.list.items[0], "attributes")
                   : NULL;
  value = attributes && attributes->type == NODE_LIST && attributes->data.list.count == 1u
              ? find_child(attributes->data.list.items[0], "value")
              : NULL;
  if (!value || value->type != NODE_STRING) {
    node_free(root);
    return NULL;
  }
  free(value->data.string_val);
  value->data.string_val = NULL;
  return root;
}

static Node *database_ir_table(Node *database_ir, size_t index) {
  Node *tables = find_child(database_ir, "db_tables");
  if (!tables || tables->type != NODE_LIST || index >= tables->data.list.count) return NULL;
  return tables->data.list.items[index];
}

static Node *database_ir_column(Node *table, size_t index) {
  Node *columns = find_child(table, "db_columns");
  if (!columns || columns->type != NODE_LIST || index >= columns->data.list.count) return NULL;
  return columns->data.list.items[index];
}

static Node *database_ir_list_item(Node *owner, const char *list_name, size_t index) {
  Node *list = find_child(owner, list_name);
  if (!list || list->type != NODE_LIST || index >= list->data.list.count) return NULL;
  return list->data.list.items[index];
}

static Node *build_database_ir_from_schema(const char *schema,
                                           tbe_database_dialect_t dialect,
                                           tbe_database_schema_status_t *status) {
  return build_database_ir_from_schema_with_diagnostic(schema, dialect, status, NULL);
}

static Node *build_database_ir_from_schema_with_diagnostic(
    const char *schema, tbe_database_dialect_t dialect, tbe_database_schema_status_t *status,
    tbe_database_schema_diagnostic_t *diagnostic) {
  Node *schema_root = create_node_map("root");
  Node *database_ir = NULL;

  if (!schema_root) return NULL;
  if (parse_schema(schema, strlen(schema), schema_root, NULL) != 0) {
    node_free(schema_root);
    return NULL;
  }

  *status = tbe_database_schema_build(schema_root, dialect, &database_ir, diagnostic);
  node_free(schema_root);
  return database_ir;
}

static Node *build_database_ir_from_mutated_default(
    const char *schema, const char *default_value, tbe_database_dialect_t dialect,
    tbe_database_schema_status_t *status, tbe_database_schema_diagnostic_t *diagnostic) {
  Node *schema_root = create_node_map("root");
  Node *database_ir = NULL;
  Node *messages;
  Node *fields;
  Node *field;

  if (!schema_root) return NULL;
  if (parse_schema(schema, strlen(schema), schema_root, NULL) != 0) goto cleanup;
  messages = find_child(schema_root, "messages");
  fields = messages && messages->type == NODE_LIST && messages->data.list.count == 1u ?
               find_child(messages->data.list.items[0], "fields") : NULL;
  field = fields && fields->type == NODE_LIST && fields->data.list.count == 1u ?
              fields->data.list.items[0] : NULL;
  if (!field || replace_test_string(field, "default_value", default_value) != 0) goto cleanup;

  *status = tbe_database_schema_build(schema_root, dialect, &database_ir, diagnostic);

cleanup:
  node_free(schema_root);
  return database_ir;
}

typedef struct database_failure_case_s {
  const char *name;
  const char *schema;
  const char *message_name;
  const char *field_name;
  const char *context;
} database_failure_case_t;

static void check_database_schema_failure(const database_failure_case_t *test_case) {
  tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
  tbe_database_schema_diagnostic_t diagnostic;
  Node *database_ir;

  check_not_null(test_case);
  if (!test_case) return;
  database_ir = build_database_ir_from_schema_with_diagnostic(
      test_case->schema, TBE_DATABASE_DIALECT_SQLITE, &status, &diagnostic);
  info("case=%s", test_case->name);
  check_equal(status, TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA);
  check_null(database_ir);
  check_equal(diagnostic.dialect, "sqlite");
  check_equal(diagnostic.message_name, test_case->message_name);
  check_equal(diagnostic.field_name, test_case->field_name);
  check_contains(diagnostic.context, test_case->context);
}

static char *render_c_template(const char *schema) {
  size_t template_size = 0;
  char *template_text = tt_read_file(C_STRUCT_TEMPLATE_FILE, &template_size);
  Node *root = NULL;
  MUSTACHE_TEMPLATE *templ = NULL;
  MUSTACHE_STRING_RENDERER renderer;
  MUSTACHE_DATAPROVIDER provider = mustache_helpers_provider();
  char *output = NULL;
  int renderer_ready = 0;

  if (!template_text) return NULL;

  root = create_node_map(NULL);
  if (!root) goto cleanup;

  if (parse_schema(schema, strlen(schema), root, NULL) != 0) goto cleanup;
  tbe_compiler_annotate_language_types(root);

  templ = mustache_compile(template_text, template_size, NULL, NULL, 0);
  if (!templ) goto cleanup;

  if (mustache_string_renderer_init(&renderer) != 0) goto cleanup;
  renderer_ready = 1;

  if (mustache_process(templ, (MUSTACHE_RENDERER *)&renderer, &renderer, &provider, root) !=
      MUSTACHE_ERR_SUCCESS) {
    goto cleanup;
  }

  output = mustache_string_renderer_get(&renderer);

cleanup:
  if (renderer_ready) {
    mustache_string_renderer_free(&renderer);
  }
  mustache_release(templ);
  node_free(root);
  free(template_text);
  return output;
}

static int write_test_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  if (!file) return -1;
  if (fwrite(text, 1, strlen(text), file) != strlen(text)) {
    fclose(file);
    return -1;
  }
  return fclose(file);
}

static char *render_compiler_template_from_schema(const char *schema,
                                                  const char *schema_path,
                                                  const char *template_file,
                                                  const char *output_path) {
  Node *root = NULL;
  char *schema_data = NULL;
  char *output = NULL;
  size_t output_size = 0;

  cleanup_test_file(schema_path);
  cleanup_test_file(output_path);
  if (write_test_file(schema_path, schema) != 0) goto cleanup;
  if (tbe_compiler_parse_schema_file(schema_path, &root, &schema_data) != 0) goto cleanup;
  if (tbe_compiler_render_file(root, template_file, output_path) != 0) goto cleanup;
  output = tt_read_file(output_path, &output_size);
  if (!output || output_size == 0) {
    free(output);
    output = NULL;
    goto cleanup;
  }

cleanup:
  free(schema_data);
  node_free(root);
  cleanup_test_file(schema_path);
  cleanup_test_file(output_path);
  return output;
}

static void cleanup_test_file(const char *path) {
  if (path) {
    remove(path);
  }
}

static int parse_schema_quietly(const char *schema, size_t size, Node *root) {
  int saved_stdout = -1;
  int saved_stderr = -1;
  FILE *null_file = NULL;
  int result;

  fflush(stdout);
  fflush(stderr);
  saved_stdout = tt_dup(tt_fileno(stdout));
  saved_stderr = tt_dup(tt_fileno(stderr));
  if (saved_stdout < 0 || saved_stderr < 0) {
    if (saved_stdout >= 0) tt_close(saved_stdout);
    if (saved_stderr >= 0) tt_close(saved_stderr);
    return parse_schema(schema, size, root, NULL);
  }

  null_file = freopen(TT_NULL_DEVICE, "w", stdout);
  if (!null_file) {
    tt_close(saved_stdout);
    tt_close(saved_stderr);
    return parse_schema(schema, size, root, NULL);
  }

  null_file = freopen(TT_NULL_DEVICE, "w", stderr);
  if (!null_file) {
    tt_dup2(saved_stdout, tt_fileno(stdout));
    tt_close(saved_stdout);
    tt_close(saved_stderr);
    return parse_schema(schema, size, root, NULL);
  }

  result = parse_schema(schema, size, root, NULL);
  fflush(stdout);
  fflush(stderr);
  tt_dup2(saved_stdout, tt_fileno(stdout));
  tt_dup2(saved_stderr, tt_fileno(stderr));
  tt_close(saved_stdout);
  tt_close(saved_stderr);
  return result;
}

static char *run_compiler_capture_stderr(const tbe_compiler_options_t *options,
                                         int *out_status) {
  int saved_stderr = -1;
  FILE *capture_file = NULL;
  char *stderr_path = NULL;
  char *captured = NULL;
  size_t captured_size = 0;

  if (out_status) *out_status = -1;

  stderr_path = tt_make_temp_file("tbe_compiler_stderr", ".log");
  if (!stderr_path) return NULL;

  fflush(stderr);
  saved_stderr = tt_dup(tt_fileno(stderr));
  if (saved_stderr < 0) goto cleanup;

  capture_file = freopen(stderr_path, "w", stderr);
  if (!capture_file) goto cleanup;

  if (out_status) {
    *out_status = tbe_compiler_run(options);
  } else {
    (void)tbe_compiler_run(options);
  }
  fflush(stderr);

cleanup:
  if (saved_stderr >= 0) {
    tt_dup2(saved_stderr, tt_fileno(stderr));
    tt_close(saved_stderr);
  }

  captured = tt_read_file(stderr_path, &captured_size);
  if (captured != NULL && captured_size == 0) {
    free(captured);
    captured = NULL;
  }

  if (stderr_path != NULL) {
    tt_remove_file(stderr_path);
    free(stderr_path);
  }

  return captured;
}

typedef enum compiler_conflicting_output_kind_e {
  COMPILER_CONFLICTING_OUTPUT_SOURCE,
  COMPILER_CONFLICTING_OUTPUT_GUEST,
  COMPILER_CONFLICTING_OUTPUT_LUA,
  COMPILER_CONFLICTING_OUTPUT_DSL
} compiler_conflicting_output_kind_t;

static const char *compiler_conflicting_output_option_name(
    compiler_conflicting_output_kind_t kind) {
  switch (kind) {
    case COMPILER_CONFLICTING_OUTPUT_SOURCE:
      return "--source-output";
    case COMPILER_CONFLICTING_OUTPUT_GUEST:
      return "--guest-output";
    case COMPILER_CONFLICTING_OUTPUT_LUA:
      return "--lua-output";
    case COMPILER_CONFLICTING_OUTPUT_DSL:
      return "--dsl-output";
    default:
      return "--unknown-output";
  }
}

static const char *compiler_conflicting_output_suffix(
    compiler_conflicting_output_kind_t kind) {
  switch (kind) {
    case COMPILER_CONFLICTING_OUTPUT_SOURCE:
      return "source";
    case COMPILER_CONFLICTING_OUTPUT_GUEST:
      return "guest";
    case COMPILER_CONFLICTING_OUTPUT_LUA:
      return "lua";
    case COMPILER_CONFLICTING_OUTPUT_DSL:
      return "dsl";
    default:
      return "unknown";
  }
}

static void compiler_assign_conflicting_output(tbe_compiler_options_t *options,
                                               compiler_conflicting_output_kind_t kind,
                                               const char *path) {
  if (!options) return;
  switch (kind) {
    case COMPILER_CONFLICTING_OUTPUT_SOURCE:
      options->source_output_path = path;
      break;
    case COMPILER_CONFLICTING_OUTPUT_GUEST:
      options->guest_output_path = path;
      break;
    case COMPILER_CONFLICTING_OUTPUT_LUA:
      options->lua_output_path = path;
      break;
    case COMPILER_CONFLICTING_OUTPUT_DSL:
      options->dsl_output_path = path;
      break;
    default:
      break;
  }
}

static void check_database_language_requires_explicit_output(const char *language_name,
                                                             int64_t lang_enum) {
  tbe_compiler_options_t options = {
      .schema_path = "missing_database_output_contract.schema",
      .resource_dir = TBE_COMPILER_RESOURCE_DIR,
      .lang_enum = lang_enum,
  };
  char *stderr_output = NULL;
  int status = -1;

  stderr_output = run_compiler_capture_stderr(&options, &status);
  info("language=%s stderr=%s", language_name, stderr_output ? stderr_output : "(null)");
  check_not_null(stderr_output);
  check_not_equal(status, 0);
  if (!stderr_output) return;

  check_contains(stderr_output, "--output");
  check_contains(stderr_output, language_name);
  check(strstr(stderr_output, "Failed to read schema file") == NULL);
  check(strstr(stderr_output, "Failed to render mustache template") == NULL);

  free(stderr_output);
}

static void check_database_language_conflicting_output_fails_fast(
    const char *language_name, int64_t lang_enum,
    compiler_conflicting_output_kind_t conflicting_output_kind) {
  char output_path[128];
  char conflicting_output_path[128];
  tbe_compiler_options_t options = {
      .schema_path = "missing_database_conflict_contract.schema",
      .resource_dir = TBE_COMPILER_RESOURCE_DIR,
      .lang_enum = lang_enum,
  };
  char *stderr_output = NULL;
  char *generated_output = NULL;
  char *generated_conflicting_output = NULL;
  size_t generated_output_size = 0;
  size_t generated_conflicting_output_size = 0;
  int status = -1;

  snprintf(output_path, sizeof(output_path), "test_tbe_compiler_%s_conflict_output.sql",
           language_name);
  snprintf(conflicting_output_path, sizeof(conflicting_output_path),
           "test_tbe_compiler_%s_conflict_%s.out", language_name,
           compiler_conflicting_output_suffix(conflicting_output_kind));
  cleanup_test_file(output_path);
  cleanup_test_file(conflicting_output_path);

  options.output_path = output_path;
  compiler_assign_conflicting_output(&options, conflicting_output_kind, conflicting_output_path);

  stderr_output = run_compiler_capture_stderr(&options, &status);
  info("language=%s option=%s stderr=%s", language_name,
       compiler_conflicting_output_option_name(conflicting_output_kind),
       stderr_output ? stderr_output : "(null)");
  check_not_null(stderr_output);
  check_not_equal(status, 0);
  if (stderr_output) {
    check_contains(stderr_output,
                   compiler_conflicting_output_option_name(conflicting_output_kind));
    check_contains(stderr_output, language_name);
    check(strstr(stderr_output, "Failed to read schema file") == NULL);
    check(strstr(stderr_output, "Failed to render mustache template") == NULL);
  }

  generated_output = tt_read_file(output_path, &generated_output_size);
  generated_conflicting_output =
      tt_read_file(conflicting_output_path, &generated_conflicting_output_size);
  check_null(generated_output);
  check_null(generated_conflicting_output);

  free(generated_output);
  free(generated_conflicting_output);
  free(stderr_output);
  cleanup_test_file(output_path);
  cleanup_test_file(conflicting_output_path);
}

static void check_database_language_empty_conflicting_output_fails_fast(
    const char *language_name, int64_t lang_enum,
    compiler_conflicting_output_kind_t conflicting_output_kind) {
  char output_path[128];
  tbe_compiler_options_t options = {
      .schema_path = "missing_database_empty_conflict_contract.schema",
      .resource_dir = TBE_COMPILER_RESOURCE_DIR,
      .lang_enum = lang_enum,
  };
  char *stderr_output = NULL;
  char *generated_output = NULL;
  size_t generated_output_size = 0;
  int status = -1;

  snprintf(output_path, sizeof(output_path),
           "test_tbe_compiler_%s_empty_conflict_output.sql", language_name);
  cleanup_test_file(output_path);

  options.output_path = output_path;
  compiler_assign_conflicting_output(&options, conflicting_output_kind, "");

  stderr_output = run_compiler_capture_stderr(&options, &status);
  info("language=%s option=%s stderr=%s", language_name,
       compiler_conflicting_output_option_name(conflicting_output_kind),
       stderr_output ? stderr_output : "(null)");
  check_not_null(stderr_output);
  check_not_equal(status, 0);
  if (stderr_output) {
    check_contains(stderr_output,
                   compiler_conflicting_output_option_name(conflicting_output_kind));
    check_contains(stderr_output, language_name);
    check(strstr(stderr_output, "Failed to read schema file") == NULL);
    check(strstr(stderr_output, "Failed to render mustache template") == NULL);
  }

  generated_output = tt_read_file(output_path, &generated_output_size);
  check_null(generated_output);

  free(generated_output);
  free(stderr_output);
  cleanup_test_file(output_path);
}

spec("tbe_compiler") {
  describe("database schema IR") {
    it("normalizes annotated tables into owned SQLite IR") {
      const char *schema =
          "schema Accounts [id(1), version(1), byte_order(little)];"
          "enum Role <uint16> { User = 0; Admin = 1; }"
          "[db_table(user_account)] message User {"
          "  [db_column(user_id), db_primary_key(1), db_generated(identity)] int64 id;"
          "  optional uint32 login_count default 0;"
          "  Role role;"
          "  [db_unique(1)] string email;"
          "  optional string display_name;"
          "}"
          "[db_table(membership)] message Membership {"
          "  [db_primary_key(1)] int32 user_id;"
          "  [db_primary_key(2)] string tenant;"
          "}";
      tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
      Node *database_ir = build_database_ir_from_schema(
          schema, TBE_DATABASE_DIALECT_SQLITE, &status);
      Node *user_table;
      Node *membership_table;
      Node *primary_keys;

      check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
      check_not_null(database_ir);
      if (!database_ir) return;

      user_table = database_ir_table(database_ir, 0);
      membership_table = database_ir_table(database_ir, 1);
      check_not_null(user_table);
      check_not_null(membership_table);
      if (!user_table || !membership_table) {
        tbe_database_schema_destroy(database_ir);
        return;
      }

      check_equal(find_child(user_table, "sql_table_name")->data.string_val,
                  "\"user_account\"");
      check_equal(find_child(user_table, "db_columns")->data.list.count, (size_t)5);
      check_equal(find_child(database_ir_column(user_table, 0), "sql_column_name")->data.string_val,
                  "\"user_id\"");
      check_equal(find_child(database_ir_column(user_table, 0), "sql_type")->data.string_val,
                  "INTEGER");
      check_equal(find_child(database_ir_column(user_table, 0), "sql_constraints")->data.string_val,
                  "NOT NULL PRIMARY KEY AUTOINCREMENT");
      check_equal(find_child(database_ir_column(user_table, 3), "sql_constraints")->data.string_val,
                  "NOT NULL UNIQUE");
      check_equal(find_child(database_ir_column(user_table, 4), "sql_constraints")->data.string_val,
                  "");
      check_contains(find_child(database_ir_column(user_table, 1), "sql_constraints")->data.string_val,
                     "DEFAULT 0");
      check_equal(find_child(database_ir_column(user_table, 2), "sql_type")->data.string_val,
                  "INTEGER");

      primary_keys = find_child(membership_table, "db_primary_key_columns");
      check_equal(primary_keys->data.list.count, (size_t)2);
      check_equal(find_child(primary_keys->data.list.items[0], "sql_column_name")->data.string_val,
                  "\"user_id\"");
      check_equal(find_child(primary_keys->data.list.items[1], "sql_column_name")->data.string_val,
                  "\"tenant\"");
      check_not_null(find_child(membership_table, "has_composite_primary_key"));
      check_null(find_child(user_table, "is_last"));
      check_null(find_child(membership_table, "is_last"));
      check_null(find_child(database_ir_column(user_table, 4), "is_last"));
      check_null(find_child(database_ir_column(membership_table, 1), "is_last"));
      check_null(find_child(primary_keys->data.list.items[1], "is_last"));

      tbe_database_schema_destroy(database_ir);
      tbe_database_schema_destroy(NULL);
      tbe_database_schema_destroy(NULL);
    }

    it("normalizes foreign keys indexes checks and initializers for each dialect") {
      const char *schema =
          "schema Shop ["
          " db_init(sqlite, \"INSERT INTO users(id, tenant) VALUES (1, 7)\"),"
          " db_init(postgresql, \"INSERT INTO users(id, tenant) VALUES (2, 8)\"),"
          " db_init(sqlite, \"INSERT INTO orders(user_id, tenant, order_number, total_cents) "
          "VALUES (1, 7, 'seed', 100)\")"
          "];"
          "[db_table(users)] message User {"
          " [db_primary_key(1)] int64 id;"
          " [db_primary_key(2)] int32 tenant;"
          "}"
          "[db_table(orders),"
          " db_foreign_key(fk_orders_user, User, user_id, id, tenant, tenant),"
          " db_foreign_key_on_delete(fk_orders_user, cascade),"
          " db_index(idx_orders_lookup, tenant, user_id),"
          " db_unique_index(uidx_orders_number, tenant, order_number),"
          " db_check(ck_total_sqlite, sqlite, \"total_cents >= 0\"),"
          " db_check(ck_total_postgresql, postgresql, \"total_cents >= 0\")"
          "] message Order {"
          " int64 user_id; int32 tenant; int64 total_cents; string order_number;"
          "}";
      const tbe_database_dialect_t dialects[] = {
          TBE_DATABASE_DIALECT_SQLITE, TBE_DATABASE_DIALECT_POSTGRESQL};
      const char *expected_check_names[] = {
          "\"ck_total_sqlite\"", "\"ck_total_postgresql\""};
      const size_t expected_initializer_counts[] = {2u, 1u};

      for (size_t dialect_index = 0; dialect_index < 2u; ++dialect_index) {
        tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
        Node *database_ir = build_database_ir_from_schema(schema, dialects[dialect_index], &status);
        Node *order_table;
        Node *foreign_key;
        Node *foreign_key_columns;
        Node *check_constraint;
        Node *index;
        Node *index_columns;
        Node *initializers;

        info("dialect_index=%zu", dialect_index);
        check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
        check_not_null(database_ir);
        if (!database_ir) continue;

        check_equal(find_child(database_ir, "db_tables")->data.list.count, (size_t)2);
        check_equal(find_child(database_ir, "db_indexes")->data.list.count, (size_t)2);
        initializers = find_child(database_ir, "db_initializers");
        check_not_null(initializers);
        if (initializers)
          check_equal(initializers->data.list.count, expected_initializer_counts[dialect_index]);

        order_table = database_ir_table(database_ir, 1);
        foreign_key = database_ir_list_item(order_table, "db_foreign_keys", 0);
        check_constraint = database_ir_list_item(order_table, "db_checks", 0);
        check_not_null(foreign_key);
        check_not_null(check_constraint);
        if (foreign_key) {
          check_equal(find_child(foreign_key, "sql_constraint_name")->data.string_val,
                      "\"fk_orders_user\"");
          check_equal(find_child(foreign_key, "sql_referenced_table_name")->data.string_val,
                      "\"users\"");
          check_equal(find_child(foreign_key, "sql_on_delete_action")->data.string_val,
                      "CASCADE");
          foreign_key_columns = find_child(foreign_key, "db_foreign_key_columns");
          check_equal(foreign_key_columns->data.list.count, (size_t)2);
          check_equal(find_child(foreign_key_columns->data.list.items[0], "sql_column_name")
                          ->data.string_val,
                      "\"user_id\"");
          check_equal(find_child(foreign_key_columns->data.list.items[0],
                                 "sql_referenced_column_name")
                          ->data.string_val,
                      "\"id\"");
          check_equal(find_child(foreign_key_columns->data.list.items[1], "sql_column_name")
                          ->data.string_val,
                      "\"tenant\"");
          check_equal(find_child(foreign_key_columns->data.list.items[1],
                                 "sql_referenced_column_name")
                          ->data.string_val,
                      "\"tenant\"");
        }
        if (check_constraint) {
          check_equal(find_child(check_constraint, "sql_constraint_name")->data.string_val,
                      expected_check_names[dialect_index]);
          check_equal(find_child(check_constraint, "sql_check_expression")->data.string_val,
                      "total_cents >= 0");
        }

        index = database_ir_list_item(database_ir, "db_indexes", 0);
        check_not_null(index);
        if (index) {
          check_equal(find_child(index, "sql_index_name")->data.string_val,
                      "\"idx_orders_lookup\"");
          check_equal(find_child(index, "sql_table_name")->data.string_val, "\"orders\"");
          check_null(find_child(index, "is_unique_index"));
          index_columns = find_child(index, "db_index_columns");
          check_equal(index_columns->data.list.count, (size_t)2);
          check_equal(find_child(index_columns->data.list.items[0], "sql_column_name")
                          ->data.string_val,
                      "\"tenant\"");
          check_equal(find_child(index_columns->data.list.items[1], "sql_column_name")
                          ->data.string_val,
                      "\"user_id\"");
        }
        index = database_ir_list_item(database_ir, "db_indexes", 1);
        check_not_null(index);
        if (index) {
          check_equal(find_child(index, "sql_index_name")->data.string_val,
                      "\"uidx_orders_number\"");
          check_not_null(find_child(index, "is_unique_index"));
        }

        if (initializers && initializers->data.list.count > 0u) {
          check_equal(find_child(initializers->data.list.items[0], "sql_statement")
                          ->data.string_val,
                      dialect_index == 0u
                          ? "INSERT INTO users(id, tenant) VALUES (1, 7)"
                          : "INSERT INTO users(id, tenant) VALUES (2, 8)");
        }
        tbe_database_schema_destroy(database_ir);
      }
    }

    it("rejects malformed or unbound foreign key delete actions") {
      const struct {
        const char *schema;
        const char *context;
      } cases[] = {
          {"[db_table(parent)] message Parent { [db_primary_key(1)] int64 id; }"
           "[db_table(child), db_foreign_key(fk_parent, Parent, parent_id, id),"
           " db_foreign_key_on_delete(fk_parent)] message Child { int64 parent_id; }",
           "requires constraint name and action"},
          {"[db_table(parent)] message Parent { [db_primary_key(1)] int64 id; }"
           "[db_table(child), db_foreign_key(fk_parent, Parent, parent_id, id),"
           " db_foreign_key_on_delete(missing, cascade)] message Child { int64 parent_id; }",
           "references unknown foreign key missing"},
          {"[db_table(parent)] message Parent { [db_primary_key(1)] int64 id; }"
           "[db_table(child), db_foreign_key(fk_parent, Parent, parent_id, id),"
           " db_foreign_key_on_delete(fk_parent, cascade),"
           " db_foreign_key_on_delete(fk_parent, restrict)] message Child { int64 parent_id; }",
           "duplicates action for fk_parent"},
          {"[db_table(parent)] message Parent { [db_primary_key(1)] int64 id; }"
           "[db_table(child), db_foreign_key(fk_parent, Parent, parent_id, id),"
           " db_foreign_key_on_delete(fk_parent, set_null)] message Child { int64 parent_id; }",
           "unsupported action set_null"},
      };

      for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_OK;
        tbe_database_schema_diagnostic_t diagnostic;
        Node *database_ir = build_database_ir_from_schema_with_diagnostic(
            cases[index].schema, TBE_DATABASE_DIALECT_SQLITE, &status, &diagnostic);

        info("case=%zu context=%s", index, diagnostic.context);
        check_null(database_ir);
        check_equal(status, TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA);
        check_contains(diagnostic.context, cases[index].context);
        tbe_database_schema_destroy(database_ir);
      }
    }

    it("maps every supported scalar type for SQLite and PostgreSQL") {
      const char *schema =
          "enum Kind <uint16> { First = 0; Second = 1; }"
          "[db_table(types)] message Types {"
          " bool bool_value; int8 i8_value; uint8 u8_value; int16 i16_value;"
          " uint16 u16_value; int32 i32_value; uint32 u32_value; int64 i64_value;"
          " uint64 u64_value; float f32_value; double f64_value;"
          " bytes(16) digest_value; uuid uuid_value; Kind kind_value;"
          " u8 alias_u8_value; uint16_t alias_u16_value; u32 alias_u32_value;"
          " uint64_t alias_u64_value; string text_value; bytes payload_value;"
          "}";
      const char *sqlite_types[] = {
          "INTEGER", "INTEGER", "INTEGER", "INTEGER", "INTEGER", "INTEGER", "INTEGER",
          "INTEGER", "TEXT", "REAL", "REAL", "BLOB", "TEXT", "INTEGER", "INTEGER",
          "INTEGER", "INTEGER", "TEXT", "TEXT", "BLOB"};
      const char *postgresql_types[] = {
          "boolean", "smallint", "smallint", "smallint", "integer", "integer", "bigint",
          "bigint", "numeric(20,0)", "real", "double precision", "bytea", "uuid", "integer",
          "smallint", "integer", "bigint", "numeric(20,0)", "text", "bytea"};
      const char *sqlite_constraints[] = {
          "NOT NULL CHECK (\"bool_value\" IS NULL OR (typeof(\"bool_value\") = 'integer' AND \"bool_value\" IN (0, 1)))",
          "NOT NULL CHECK (\"u8_value\" IS NULL OR (typeof(\"u8_value\") = 'integer' AND \"u8_value\" BETWEEN 0 AND 255))",
          "NOT NULL CHECK (\"u16_value\" IS NULL OR (typeof(\"u16_value\") = 'integer' AND \"u16_value\" BETWEEN 0 AND 65535))",
          "NOT NULL CHECK (\"u32_value\" IS NULL OR (typeof(\"u32_value\") = 'integer' AND \"u32_value\" BETWEEN 0 AND 4294967295))",
          "NOT NULL CHECK (\"u64_value\" IS NULL OR (typeof(\"u64_value\") = 'text' AND length(CAST(\"u64_value\" AS BLOB)) = length(\"u64_value\") AND length(\"u64_value\") BETWEEN 1 AND 20 AND \"u64_value\" NOT GLOB '*[^0-9]*' AND (\"u64_value\" = '0' OR substr(\"u64_value\", 1, 1) <> '0') AND (length(\"u64_value\") < 20 OR \"u64_value\" <= '18446744073709551615')))",
          "NOT NULL CHECK (\"kind_value\" IS NULL OR (typeof(\"kind_value\") = 'integer' AND \"kind_value\" BETWEEN 0 AND 65535))",
          "NOT NULL CHECK (\"alias_u8_value\" IS NULL OR (typeof(\"alias_u8_value\") = 'integer' AND \"alias_u8_value\" BETWEEN 0 AND 255))",
          "NOT NULL CHECK (\"alias_u16_value\" IS NULL OR (typeof(\"alias_u16_value\") = 'integer' AND \"alias_u16_value\" BETWEEN 0 AND 65535))",
          "NOT NULL CHECK (\"alias_u32_value\" IS NULL OR (typeof(\"alias_u32_value\") = 'integer' AND \"alias_u32_value\" BETWEEN 0 AND 4294967295))",
          "NOT NULL CHECK (\"alias_u64_value\" IS NULL OR (typeof(\"alias_u64_value\") = 'text' AND length(CAST(\"alias_u64_value\" AS BLOB)) = length(\"alias_u64_value\") AND length(\"alias_u64_value\") BETWEEN 1 AND 20 AND \"alias_u64_value\" NOT GLOB '*[^0-9]*' AND (\"alias_u64_value\" = '0' OR substr(\"alias_u64_value\", 1, 1) <> '0') AND (length(\"alias_u64_value\") < 20 OR \"alias_u64_value\" <= '18446744073709551615')))"};
      const char *postgresql_constraints[] = {
          "NOT NULL", "NOT NULL CHECK (\"u8_value\" BETWEEN 0 AND 255)",
          "NOT NULL CHECK (\"u16_value\" BETWEEN 0 AND 65535)",
          "NOT NULL CHECK (\"u32_value\" BETWEEN 0 AND 4294967295)",
          "NOT NULL CHECK (\"u64_value\" BETWEEN 0 AND 18446744073709551615)",
          "NOT NULL CHECK (\"kind_value\" BETWEEN 0 AND 65535)",
          "NOT NULL CHECK (\"alias_u8_value\" BETWEEN 0 AND 255)",
          "NOT NULL CHECK (\"alias_u16_value\" BETWEEN 0 AND 65535)",
          "NOT NULL CHECK (\"alias_u32_value\" BETWEEN 0 AND 4294967295)",
          "NOT NULL CHECK (\"alias_u64_value\" BETWEEN 0 AND 18446744073709551615)"};
      const tbe_database_dialect_t dialects[] = {
          TBE_DATABASE_DIALECT_SQLITE, TBE_DATABASE_DIALECT_POSTGRESQL};
      const char *const *expected_types[] = {sqlite_types, postgresql_types};
      const char *const *expected_constraints[] = {sqlite_constraints, postgresql_constraints};
      const size_t constraint_columns[] = {0, 2, 4, 6, 8, 13, 14, 15, 16, 17};

      for (size_t dialect_index = 0; dialect_index < 2; ++dialect_index) {
        tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
        Node *database_ir = build_database_ir_from_schema(
            schema, dialects[dialect_index], &status);
        Node *table;

        check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
        check_not_null(database_ir);
        if (!database_ir) continue;
        table = database_ir_table(database_ir, 0);
        check_not_null(table);
        if (table) {
          for (size_t column_index = 0; column_index < 20; ++column_index) {
            Node *column = database_ir_column(table, column_index);
            check_not_null(column);
            if (column) {
              check_equal(find_child(column, "sql_type")->data.string_val,
                          expected_types[dialect_index][column_index]);
            }
          }
          for (size_t constraint_index = 0; constraint_index < 10; ++constraint_index) {
            Node *column = database_ir_column(table, constraint_columns[constraint_index]);
            check_not_null(column);
            if (column) {
              check_equal(find_child(column, "sql_constraints")->data.string_val,
                          expected_constraints[dialect_index][constraint_index]);
            }
          }
        }
        tbe_database_schema_destroy(database_ir);
      }
    }

    it("supports identity only for integer types available to each dialect") {
      const char *postgresql_schema =
          "[db_table(ids_u8)] message IdU8 {"
          " [db_primary_key(1), db_generated(identity)] uint8 id; }"
          "[db_table(ids_u16)] message IdU16 {"
          " [db_primary_key(1), db_generated(identity)] uint16 id; }"
          "[db_table(ids_u32)] message IdU32 {"
          " [db_primary_key(1), db_generated(identity)] uint32 id; }";
      const char *postgresql_constraints[] = {
          "NOT NULL PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY CHECK (\"id\" BETWEEN 0 AND 255)",
          "NOT NULL PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY CHECK (\"id\" BETWEEN 0 AND 65535)",
          "NOT NULL PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY CHECK (\"id\" BETWEEN 0 AND 4294967295)",
      };
      tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
      tbe_database_schema_diagnostic_t diagnostic;
      Node *database_ir = build_database_ir_from_schema_with_diagnostic(
          postgresql_schema, TBE_DATABASE_DIALECT_POSTGRESQL, &status, &diagnostic);

      check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
      check_not_null(database_ir);
      if (database_ir) {
        for (size_t index = 0; index < 3u; ++index) {
          Node *column = database_ir_column(database_ir_table(database_ir, index), 0);
          check_not_null(column);
          if (column) {
            check_equal(find_child(column, "sql_constraints")->data.string_val,
                        postgresql_constraints[index]);
          }
        }
        tbe_database_schema_destroy(database_ir);
      }

      database_ir = build_database_ir_from_schema_with_diagnostic(
          "[db_table(ids)] message Ids {"
          " [db_primary_key(1), db_generated(identity)] uint64 id; }",
          TBE_DATABASE_DIALECT_POSTGRESQL, &status, &diagnostic);
      check_equal(status, TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA);
      check_null(database_ir);
      check_equal(diagnostic.field_name, "id");
      check_contains(diagnostic.context, "identity");
      check_contains(diagnostic.context, "uint64");

      database_ir = build_database_ir_from_schema_with_diagnostic(
          "[db_table(ids)] message Ids {"
          " [db_primary_key(1), db_generated(identity)] uint8 id; }",
          TBE_DATABASE_DIALECT_SQLITE, &status, &diagnostic);
      check_equal(status, TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA);
      check_null(database_ir);
      check_equal(diagnostic.field_name, "id");
      check_contains(diagnostic.context, "signed");
    }

    it("quotes PostgreSQL string defaults independently of server settings") {
      const char *schema =
          "[db_table(strings)] message Strings {"
          " string payload default \"safe\\'; DROP TABLE protected; --\"; }";
      tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
      Node *database_ir = build_database_ir_from_schema(
          schema, TBE_DATABASE_DIALECT_POSTGRESQL, &status);
      Node *column;

      check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
      check_not_null(database_ir);
      if (!database_ir) return;
      column = database_ir_column(database_ir_table(database_ir, 0), 0);
      check_not_null(column);
      if (column) {
        check_equal(find_child(column, "sql_constraints")->data.string_val,
                    "NOT NULL DEFAULT E'safe\\\\''; DROP TABLE protected; --'");
      }
      tbe_database_schema_destroy(database_ir);
    }

    it("normalizes only type-valid defaults") {
      const char *valid_schema =
          "enum State <uint8> { Idle = 0; Active = 7; }"
          "[db_table(defaults), custom(kept)] message Defaults {"
          " bool enabled default true; int8 signed_value default 127;"
          " uint8 unsigned_value default 255; float ratio default 1;"
          " State state default Active; uint8 hexadecimal_value default 0xFF;"
          " uint64 exact_value default 18446744073709551615;"
          " string label default \"O'Reilly\";"
          "}";
      const char *invalid_schemas[] = {
          "[db_table(invalid)] message Invalid { int32 accepted; bool value default 1; }",
          "[db_table(invalid)] message Invalid { int8 value default true; }",
          "[db_table(invalid)] message Invalid { uint8 value default 256; }",
          "[db_table(invalid)] message Invalid { float value default NaN; }",
          "enum State <uint8> { Idle = 0; } [db_table(invalid)] message Invalid { State value default Missing; }",
          "[db_table(invalid)] message Invalid { bytes value default 1; }",
          "[db_table(invalid)] message Invalid { uuid value default \"not-a-uuid\"; }"};
      tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
      Node *database_ir = build_database_ir_from_schema(
          valid_schema, TBE_DATABASE_DIALECT_SQLITE, &status);
      Node *table;

      check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
      check_not_null(database_ir);
      if (database_ir) {
        table = database_ir_table(database_ir, 0);
        check_not_null(table);
        if (table) {
          check_equal(find_child(database_ir_column(table, 0), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 1 CHECK (\"enabled\" IS NULL OR (typeof(\"enabled\") = 'integer' AND \"enabled\" IN (0, 1)))");
          check_equal(find_child(database_ir_column(table, 1), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 127");
          check_equal(find_child(database_ir_column(table, 2), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 255 CHECK (\"unsigned_value\" IS NULL OR (typeof(\"unsigned_value\") = 'integer' AND \"unsigned_value\" BETWEEN 0 AND 255))");
          check_equal(find_child(database_ir_column(table, 3), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 1");
          check_equal(find_child(database_ir_column(table, 7), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 'O''Reilly'");
          check_equal(find_child(database_ir_column(table, 4), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 7 CHECK (\"state\" IS NULL OR (typeof(\"state\") = 'integer' AND \"state\" BETWEEN 0 AND 255))");
          check_equal(find_child(database_ir_column(table, 5), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT 255 CHECK (\"hexadecimal_value\" IS NULL OR (typeof(\"hexadecimal_value\") = 'integer' AND \"hexadecimal_value\" BETWEEN 0 AND 255))");
          check_equal(find_child(database_ir_column(table, 6), "sql_constraints")->data.string_val,
                      "NOT NULL DEFAULT '18446744073709551615' CHECK (\"exact_value\" IS NULL OR (typeof(\"exact_value\") = 'text' AND length(CAST(\"exact_value\" AS BLOB)) = length(\"exact_value\") AND length(\"exact_value\") BETWEEN 1 AND 20 AND \"exact_value\" NOT GLOB '*[^0-9]*' AND (\"exact_value\" = '0' OR substr(\"exact_value\", 1, 1) <> '0') AND (length(\"exact_value\") < 20 OR \"exact_value\" <= '18446744073709551615')))");
        }
        tbe_database_schema_destroy(database_ir);
      }

      for (size_t schema_index = 0;
           schema_index < sizeof(invalid_schemas) / sizeof(invalid_schemas[0]); ++schema_index) {
        database_ir = build_database_ir_from_schema(
            invalid_schemas[schema_index], TBE_DATABASE_DIALECT_SQLITE, &status);
        check_equal(status, TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA);
        check_null(database_ir);
      }
    }

    it("reports the closest field and annotation for invalid field annotations") {
      static const database_failure_case_t cases[] = {
          {"rejects unknown field annotation",
           "[db_table(annotation_scope)] message FieldUnknown { [db_unknown(1)] int32 id; }",
           "FieldUnknown", "id", "annotation=db_unknown"},
          {"rejects db_table on a field",
           "[db_table(annotation_scope)] message FieldTable { [db_table(wrong_location)] int32 id; }",
           "FieldTable", "id", "annotation=db_table"},
      };

      for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        check_database_schema_failure(&cases[index]);
    }

    it("rejects every table, column, primary-key, identity, and db_ignore contract break") {
      static const database_failure_case_t cases[] = {
          {"requires a database table", "message NotTable { int32 id; }", "<schema>",
           "<schema>", "annotation=db_table"},
          {"rejects duplicate table names",
           "[db_table(shared)] message First { int32 id; }"
           "[db_table(shared)] message Second { int32 id; }",
           "Second", "<message>", "annotation=db_table"},
          {"rejects duplicate column names",
           "[db_table(columns)] message ColumnNames { [db_column(id)] int32 first; "
           "[db_column(id)] int32 second; }",
           "ColumnNames", "second", "annotation=db_column"},
          {"rejects a table without persisted columns",
           "[db_table(empty)] message Empty { [db_ignore(1)] int32 id; }", "Empty", "<table>",
           "annotation=db_ignore"},
          {"rejects duplicate primary-key order",
           "[db_table(keys)] message DuplicateOrder { [db_primary_key(1)] int32 first; "
           "[db_primary_key(1)] int32 second; }",
           "DuplicateOrder", "second", "annotation=db_primary_key"},
          {"rejects a primary-key order gap",
           "[db_table(keys)] message MissingOrder { [db_primary_key(1)] int32 first; "
           "[db_primary_key(3)] int32 third; }",
           "MissingOrder", "third", "annotation=db_primary_key"},
          {"rejects zero primary-key order",
           "[db_table(keys)] message ZeroOrder { [db_primary_key(0)] int32 id; }", "ZeroOrder", "id",
           "annotation=db_primary_key"},
          {"rejects an optional primary key",
           "[db_table(keys)] message OptionalKey { optional [db_primary_key(1)] int32 id; }",
           "OptionalKey", "id", "annotation=db_primary_key"},
          {"rejects identity on a non-integer primary key",
           "[db_table(identity)] message TextIdentity { [db_primary_key(1), db_generated(identity)] string id; }",
           "TextIdentity", "id", "annotation=db_generated"},
          {"rejects identity in a composite primary key",
           "[db_table(identity)] message CompositeIdentity { [db_primary_key(1), db_generated(identity)] int64 id; "
           "[db_primary_key(2)] int32 tenant; }",
           "CompositeIdentity", "id", "annotation=db_generated"},
          {"rejects identity with a TBE default",
           "[db_table(identity)] message DefaultIdentity { [db_primary_key(1), db_generated(identity)] int64 id default 1; }",
           "DefaultIdentity", "id", "annotation=db_generated"},
          {"rejects identity with unique",
           "[db_table(identity)] message UniqueIdentity { [db_primary_key(1), db_generated(identity), db_unique(1)] int64 id; }",
           "UniqueIdentity", "id", "annotation=db_generated"},
      };

      for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        check_database_schema_failure(&cases[index]);
    }

    it("rejects invalid db boolean values and db_ignore annotation conflicts") {
      static const database_failure_case_t cases[] = {
          {"rejects a non-one db_unique value",
           "[db_table(annotation_values)] message AnnotationValues { [db_unique(2)] int32 id; }",
           "AnnotationValues", "id", "annotation=db_unique"},
          {"rejects a non-one db_ignore value",
           "[db_table(annotation_values)] message AnnotationValues { [db_ignore(2)] int32 id; }",
           "AnnotationValues", "id", "annotation=db_ignore"},
          {"rejects db_ignore combined with another database annotation",
           "[db_table(annotation_values)] message AnnotationValues { [db_ignore(1), db_unique(1)] int32 id; }",
           "AnnotationValues", "id", "annotation=db_ignore"},
      };

      for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        check_database_schema_failure(&cases[index]);
    }

    it("rejects unsafe custom checks and non-insert initialization statements") {
      static const database_failure_case_t cases[] = {
          {"rejects multiple statements in a check",
           "[db_table(records), db_check(ck_value, sqlite, \"value > 0; DROP TABLE records\")] "
           "message Records { int32 value; }",
           "Records", "<message>", "annotation=db_check"},
          {"rejects a line comment in a check",
           "[db_table(records), db_check(ck_value, sqlite, \"value > 0 -- bypass\")] "
           "message Records { int32 value; }",
           "Records", "<message>", "annotation=db_check"},
          {"rejects a block comment in a check",
           "[db_table(records), db_check(ck_value, sqlite, \"value > 0 /* bypass */\")] "
           "message Records { int32 value; }",
           "Records", "<message>", "annotation=db_check"},
          {"rejects an unterminated string in a check",
           "[db_table(records), db_check(ck_value, sqlite, \"value <> 'blocked\")] "
           "message Records { string value; }",
           "Records", "<message>", "annotation=db_check"},
          {"rejects unbalanced parentheses in a check",
           "[db_table(records), db_check(ck_value, sqlite, \"value >= (0\")] "
           "message Records { int32 value; }",
           "Records", "<message>", "annotation=db_check"},
          {"rejects a non-insert initializer",
           "schema Records [db_init(sqlite, \"DELETE FROM records\")];"
           "[db_table(records)] message Record { int32 value; }",
           "<schema>", "<schema>", "annotation=db_init"},
          {"rejects multiple initializer statements",
           "schema Records [db_init(sqlite, \"INSERT INTO records(value) VALUES (1); "
           "DELETE FROM records\")];"
           "[db_table(records)] message Record { int32 value; }",
           "<schema>", "<schema>", "annotation=db_init"},
          {"rejects an initializer comment",
           "schema Records [db_init(sqlite, \"INSERT INTO records(value) VALUES (1) -- trailing\")];"
           "[db_table(records)] message Record { int32 value; }",
           "<schema>", "<schema>", "annotation=db_init"},
      };

      for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        check_database_schema_failure(&cases[index]);
    }

    it("rejects malformed relation index and legacy database annotations") {
      static const database_failure_case_t cases[] = {
          {"rejects extra db_table arguments",
           "[db_table(records, extra)] message Records { int32 value; }",
           "Records", "<message>", "annotation=db_table"},
          {"rejects extra db_column arguments",
           "[db_table(records)] message Records { [db_column(value, extra)] int32 value; }",
           "Records", "value", "annotation=db_column"},
          {"requires db_table for table-level database annotations",
           "[db_index(idx_records, value)] message Records { int32 value; }"
           "[db_table(other)] message Other { int32 value; }",
           "Records", "<message>", "annotation=db_index"},
          {"requires an indexed field",
           "[db_table(records), db_index(idx_records)] message Records { int32 value; }",
           "Records", "<message>", "annotation=db_index"},
          {"rejects duplicate index names across tables",
           "[db_table(first), db_index(idx_shared, value)] message First { int32 value; }"
           "[db_table(second), db_index(idx_shared, value)] message Second { int32 value; }",
           "Second", "<message>", "annotation=db_index"},
          {"rejects an index name that collides with a table",
           "[db_table(records), db_index(other, value)] message Records { int32 value; }"
           "[db_table(other)] message Other { int32 value; }",
           "Records", "<message>", "annotation=db_index"},
          {"rejects an unknown indexed field",
           "[db_table(records), db_index(idx_records, missing)] message Records { int32 value; }",
           "Records", "missing", "annotation=db_index"},
          {"rejects a repeated indexed field",
           "[db_table(records), db_index(idx_records, value, value)] "
           "message Records { int32 value; }",
           "Records", "value", "annotation=db_index"},
          {"rejects incomplete foreign-key pairs",
           "[db_table(users)] message User { [db_primary_key(1)] int64 id; }"
           "[db_table(orders), db_foreign_key(fk_user, User, user_id)] "
           "message Order { int64 user_id; }",
           "Order", "<message>", "annotation=db_foreign_key"},
          {"rejects an unknown foreign-key target message",
           "[db_table(orders), db_foreign_key(fk_user, Missing, user_id, id)] "
           "message Order { int64 user_id; }",
           "Order", "<message>", "annotation=db_foreign_key"},
          {"rejects incompatible foreign-key field types",
           "[db_table(users)] message User { [db_primary_key(1)] int64 id; }"
           "[db_table(orders), db_foreign_key(fk_user, User, user_id, id)] "
           "message Order { string user_id; }",
           "Order", "user_id", "annotation=db_foreign_key"},
          {"requires a unique foreign-key target",
           "[db_table(users)] message User { int64 id; }"
           "[db_table(orders), db_foreign_key(fk_user, User, user_id, id)] "
           "message Order { int64 user_id; }",
           "Order", "<message>", "annotation=db_foreign_key"},
          {"rejects repeated local foreign-key fields",
           "[db_table(users)] message User { [db_primary_key(1)] int64 id; "
           "[db_primary_key(2)] int64 tenant; }"
           "[db_table(orders), db_foreign_key(fk_user, User, user_id, id, user_id, tenant)] "
           "message Order { int64 user_id; }",
           "Order", "user_id", "annotation=db_foreign_key"},
          {"rejects duplicate table constraint names",
           "[db_table(users)] message User { [db_primary_key(1)] int64 id; }"
           "[db_table(orders), db_foreign_key(shared_name, User, user_id, id),"
           " db_check(shared_name, sqlite, \"user_id > 0\")] "
           "message Order { int64 user_id; }",
           "Order", "<message>", "constraint"},
          {"rejects an unknown check dialect",
           "[db_table(records), db_check(ck_value, mysql, \"value > 0\")] "
           "message Records { int32 value; }",
           "Records", "<message>", "annotation=db_check"},
          {"rejects an unknown initializer dialect",
           "schema Records [db_init(mysql, \"INSERT INTO records(value) VALUES (1)\")];"
           "[db_table(records)] message Record { int32 value; }",
           "<schema>", "<schema>", "annotation=db_init"},
      };

      for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        check_database_schema_failure(&cases[index]);
    }

    it("rejects unsupported persisted field shapes while db_ignore skips them") {
      static const database_failure_case_t cases[] = {
          {"rejects a collection", "[db_table(unsupported)] message Collection { list<int32> values; }",
           "Collection", "values", "type=list"},
          {"rejects a map", "[db_table(unsupported)] message Map { map<string,int32> values; }", "Map",
           "values", "type=map"},
          {"rejects a group", "group Level { int32 price; } "
                              "[db_table(unsupported)] message Group { group<Level> levels; }",
           "Group", "levels", "type=group"},
          {"rejects a composite reference", "composite Header { int32 sequence; } "
                                            "[db_table(unsupported)] message Composite { Header header; }",
           "Composite", "header", "type=Header"},
          {"rejects a union reference", "union Choice { int32 value; } "
                                        "[db_table(unsupported)] message Union { Choice choice; }",
           "Union", "choice", "type=Choice"},
      };
      static const char *const ignored_schemas[] = {
          "[db_table(ignored_collection)] message IgnoredCollection { int32 id; [db_ignore(1)] list<int32> values; }",
          "[db_table(ignored_map)] message IgnoredMap { int32 id; [db_ignore(1)] map<string,int32> values; }",
          "group Level { int32 price; } [db_table(ignored_group)] message IgnoredGroup { int32 id; [db_ignore(1)] group<Level> levels; }",
          "composite Header { int32 sequence; } [db_table(ignored_composite)] message IgnoredComposite { int32 id; [db_ignore(1)] Header header; }",
          "union Choice { int32 value; } [db_table(ignored_union)] message IgnoredUnion { int32 id; [db_ignore(1)] Choice choice; }",
      };

      for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        check_database_schema_failure(&cases[index]);
      for (size_t index = 0; index < sizeof(ignored_schemas) / sizeof(ignored_schemas[0]); ++index) {
        tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
        Node *database_ir = build_database_ir_from_schema(
            ignored_schemas[index], TBE_DATABASE_DIALECT_SQLITE, &status);
        check_equal(status, TBE_DATABASE_SCHEMA_STATUS_OK);
        check_not_null(database_ir);
        tbe_database_schema_destroy(database_ir);
      }
    }

    it("normalizes grammar-expressible default literals and rejects type mismatch") {
      const char *valid_schema =
          "enum State <uint8> { Idle = 0; Active = 255; }"
          "[db_table(default_limits)] message DefaultLimits {"
          " int8 signed_min default 0; int8 signed_max default 127; int16 signed_max16 default 32767;"
          " int32 signed_max32 default 2147483647; int64 signed_max64 default 9223372036854775807;"
          " uint8 unsigned_min default 0; uint64 unsigned_max default 18446744073709551615;"
          " float decimal default 125; State state default Active; string quoted default \"D'Arcy\";"
          "}";
      static const database_failure_case_t invalid_cases[] = {
          {"rejects signed default above its maximum",
           "[db_table(defaults)] message SignedOverflow { int8 value default 128; }",
           "SignedOverflow", "value", "type=int8 default=128"},
          {"rejects unsigned default above its maximum",
           "[db_table(defaults)] message UnsignedOverflow { uint64 value default 18446744073709551616; }",
           "UnsignedOverflow", "value", "type=uint64 default=18446744073709551616"},
          {"rejects int64 default above its maximum",
           "[db_table(defaults)] message Int64Overflow { int64 value default 9223372036854775808; }",
           "Int64Overflow", "value", "type=int64 default=9223372036854775808"},
          {"rejects a boolean default written as an integer",
           "[db_table(defaults)] message BooleanMismatch { bool value default 1; }",
           "BooleanMismatch", "value", "type=bool default=1"},
          {"rejects a non-finite float default",
           "[db_table(defaults)] message FloatInvalid { float value default NaN; }",
           "FloatInvalid", "value", "type=float default=NaN"},
          {"rejects an unknown enum default constant",
           "enum State <uint8> { Idle = 0; } "
           "[db_table(defaults)] message EnumMismatch { State value default Missing; }",
           "EnumMismatch", "valu…10244 tokens truncated… IS NULL OR "
          "(typeof(\"record_id\") = 'text' AND length(CAST(\"record_id\" AS BLOB)) = "
          "length(\"record_id\") AND length(\"record_id\") BETWEEN 1 AND 20 AND "
          "\"record_id\" NOT GLOB '*[^0-9]*' AND (\"record_id\" = '0' OR "
          "substr(\"record_id\", 1, 1) <> '0') AND (length(\"record_id\") < 20 OR "
          "\"record_id\" <= '18446744073709551615'))) PRIMARY KEY;\"note\" TEXT";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = template_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };
      char *output = NULL;
      size_t output_size = 0;

      cleanup_test_file(schema_path);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(write_test_file(template_path, template_text), 0);
      check_equal(tbe_compiler_run(&options), 0);
      output = tt_read_file(output_path, &output_size);
      check_not_null(output);
      if (output) {
        check_equal(output, expected);
        free(output);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
    }

    it("should expose relationship index check and initializer IR to custom templates") {
      const char *schema_path = "test_tbe_compiler_database_v2_custom.schema";
      const char *template_path = "test_tbe_compiler_database_v2_custom.mustache";
      const char *output_path = "test_tbe_compiler_database_v2_custom.out";
      const char *schema =
          "schema Custom [db_init(sqlite, \"INSERT INTO users(id) VALUES (1)\")];"
          "[db_table(users)] message User { [db_primary_key(1)] int64 id; }"
          "[db_table(orders), db_foreign_key(fk_user, User, user_id, id),"
          " db_unique_index(uidx_order_user, order_id, user_id),"
          " db_check(ck_order_id, sqlite, \"order_id > 0\")]"
          " message Order { int64 order_id; int64 user_id; }";
      const char *template_text =
          "{{#db_tables}}T{{sql_table_name}}"
          "{{#db_checks}} C{{sql_constraint_name}}={{sql_check_expression}}{{/db_checks}}"
          "{{#db_foreign_keys}} F{{sql_constraint_name}}>{{sql_referenced_table_name}}("
          "{{#db_foreign_key_columns}}{{sql_column_name}}={{sql_referenced_column_name}}"
          "{{#has_next_foreign_key_column}},{{/has_next_foreign_key_column}}"
          "{{/db_foreign_key_columns}}){{/db_foreign_keys}};{{/db_tables}}"
          "{{#db_indexes}} I{{#is_unique_index}}U{{/is_unique_index}}{{sql_index_name}}@"
          "{{sql_table_name}}({{#db_index_columns}}{{sql_column_name}}"
          "{{#has_next_index_column}},{{/has_next_index_column}}{{/db_index_columns}});"
          "{{/db_indexes}}{{#db_initializers}} S{{sql_statement}};{{/db_initializers}}";
      const char *expected =
          "T\"users\";T\"orders\" C\"ck_order_id\"=order_id > 0"
          " F\"fk_user\">\"users\"(\"user_id\"=\"id\");"
          " IU\"uidx_order_user\"@\"orders\"(\"order_id\",\"user_id\");"
          " SINSERT INTO users(id) VALUES (1);";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = template_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };
      char *output = NULL;
      size_t output_size = 0;

      cleanup_test_file(schema_path);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(write_test_file(template_path, template_text), 0);
      check_equal(tbe_compiler_run(&options), 0);
      output = tt_read_file(output_path, &output_size);
      check_not_null(output);
      if (output) {
        check_equal(output, expected);
        free(output);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
    }

    it("should expose scoped database template markers without is_last") {
      const char *schema_path = "test_tbe_compiler_database_markers.schema";
      const char *template_path = "test_tbe_compiler_database_markers.mustache";
      const char *output_path = "test_tbe_compiler_database_markers.out";
      const char *schema =
          "[db_table(alpha)] message Alpha {"
          " [db_primary_key(1)] int64 a1;"
          " [db_primary_key(2)] uint16 a3;"
          " optional string a2;"
          "}"
          "[db_table(beta)] message Beta {"
          " [db_primary_key(1)] string b1;"
          "}";
      const char *template_text =
          "{{#db_tables}}T={{sql_table_name}}[{{#db_columns}}{{sql_column_name}}"
          "{{#has_sql_constraints}}C{{/has_sql_constraints}}"
          "{{#has_next_column}},{{/has_next_column}}{{/db_columns}}]"
          "{{#has_composite_primary_key}}PK={{#db_primary_key_columns}}{{sql_column_name}}"
          "{{#has_next_primary_key}}+{{/has_next_primary_key}}"
          "{{/db_primary_key_columns}};{{/has_composite_primary_key}}"
          "{{#has_next_table}}|{{/has_next_table}}{{/db_tables}}";
      const char *expected =
          "T=\"alpha\"[\"a1\"C,\"a3\"C,\"a2\"]PK=\"a1\"+\"a3\";|"
          "T=\"beta\"[\"b1\"C]";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = template_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };
      char *output = NULL;
      size_t output_size = 0;

      cleanup_test_file(schema_path);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(write_test_file(template_path, template_text), 0);
      check_equal(tbe_compiler_run(&options), 0);
      output = tt_read_file(output_path, &output_size);
      check_not_null(output);
      if (output) {
        check_equal(output, expected);
        free(output);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
    }

    it("should create POSIX output with fopen-compatible permissions") {
#ifndef _WIN32
      const char *schema_path = "test_tbe_compiler_posix_mode.schema";
      const char *output_path = "test_tbe_compiler_posix_mode.sql";
      const char *schema =
          "[db_table(records)] message Record { [db_primary_key(1)] int64 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };
      struct stat output_status;
      mode_t saved_umask;
      mode_t expected_mode;

      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
      saved_umask = umask(0);
      umask(saved_umask);
      expected_mode = (mode_t)(0666 & ~saved_umask);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      check_equal(stat(output_path, &output_status), 0);
      check_equal((mode_t)(output_status.st_mode & 0777), expected_mode);
      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
#else
      check_true(1);
#endif
    }

    it("should preserve existing POSIX output permissions when overwriting") {
#ifndef _WIN32
      static const mode_t modes[] = {0600, 0640};
      const char *schema_path = "test_tbe_compiler_posix_overwrite_mode.schema";
      const char *output_path = "test_tbe_compiler_posix_overwrite_mode.sql";
      const char *schema =
          "[db_table(records)] message Record { [db_primary_key(1)] int64 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
      check_equal(write_test_file(schema_path, schema), 0);
      for (size_t index = 0; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        struct stat output_status;

        check_equal(write_test_file(output_path, "existing-output"), 0);
        check_equal(chmod(output_path, modes[index]), 0);
        check_equal(tbe_compiler_run(&options), 0);
        check_equal(stat(output_path, &output_status), 0);
        check_equal((mode_t)(output_status.st_mode & 0777), modes[index]);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
#else
      check_true(1);
#endif
    }

    it("should preserve a similarly named pre-existing temporary file") {
      const char *schema_path = "test_tbe_compiler_temp_owner.schema";
      const char *output_path = "test_tbe_compiler_temp_owner.sql";
      const char *old_temp_path = "test_tbe_compiler_temp_owner.sql.tbe.tmp";
      const char *schema =
          "[db_table(records)] message Record { [db_primary_key(1)] int64 id; }";
      const char *expected =
          "BEGIN;\n"
          "CREATE TABLE \"records\" (\n"
          "  \"id\" INTEGER NOT NULL PRIMARY KEY\n"
          ");\n\n"
          "COMMIT;\n";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };
      char *output = NULL;
      char *old_temp = NULL;
      size_t output_size = 0;
      size_t old_temp_size = 0;

      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
      cleanup_test_file(old_temp_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(write_test_file(output_path, "known-good-output"), 0);
      check_equal(write_test_file(old_temp_path, "unrelated-temporary-file"), 0);
      check_equal(tbe_compiler_run(&options), 0);
      output = tt_read_file(output_path, &output_size);
      old_temp = tt_read_file(old_temp_path, &old_temp_size);
      check_not_null(output);
      check_not_null(old_temp);
      if (output) {
        check_equal(output, expected);
        free(output);
      }
      if (old_temp) {
        check_equal(old_temp, "unrelated-temporary-file");
        free(old_temp);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
      cleanup_test_file(old_temp_path);
    }

    it("should preserve an existing output when template compilation fails") {
      const char *template_path = "test_tbe_compiler_invalid_template.mustache";
      const char *output_path = "test_tbe_compiler_preserved_output.out";
      Node *root = create_node_map(NULL);
      char *output = NULL;
      size_t output_size = 0;

      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
      check_not_null(root);
      check_equal(write_test_file(template_path, "{{/unterminated}}"), 0);
      check_equal(write_test_file(output_path, "known-good-output"), 0);
      if (root) check_not_equal(tbe_compiler_render_file(root, template_path, output_path), 0);
      output = tt_read_file(output_path, &output_size);
      check_not_null(output);
      if (output) {
        check_equal(output, "known-good-output");
        free(output);
      }
      node_free(root);
      cleanup_test_file(template_path);
      cleanup_test_file(output_path);
    }

    it("should require explicit output for SQLite and PostgreSQL DDL") {
      check_database_language_requires_explicit_output("sqlite", TBE_COMPILER_LANG_SQLITE);
      check_database_language_requires_explicit_output("postgresql",
                                                       TBE_COMPILER_LANG_POSTGRESQL);
    }

    it("should reject source output with database languages before parsing") {
      check_database_language_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_SOURCE);
      check_database_language_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL,
          COMPILER_CONFLICTING_OUTPUT_SOURCE);
    }

    it("should reject guest output with database languages before parsing") {
      check_database_language_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_GUEST);
      check_database_language_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL,
          COMPILER_CONFLICTING_OUTPUT_GUEST);
    }

    it("should reject Lua output with database languages before parsing") {
      check_database_language_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_LUA);
      check_database_language_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL, COMPILER_CONFLICTING_OUTPUT_LUA);
    }

    it("should reject DSL output with database languages before parsing") {
      check_database_language_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_DSL);
      check_database_language_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL, COMPILER_CONFLICTING_OUTPUT_DSL);
    }

    it("should reject empty source output with database languages before parsing") {
      check_database_language_empty_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_SOURCE);
      check_database_language_empty_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL,
          COMPILER_CONFLICTING_OUTPUT_SOURCE);
    }

    it("should reject empty guest output with database languages before parsing") {
      check_database_language_empty_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_GUEST);
      check_database_language_empty_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL,
          COMPILER_CONFLICTING_OUTPUT_GUEST);
    }

    it("should reject empty Lua output with database languages before parsing") {
      check_database_language_empty_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_LUA);
      check_database_language_empty_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL, COMPILER_CONFLICTING_OUTPUT_LUA);
    }

    it("should reject empty DSL output with database languages before parsing") {
      check_database_language_empty_conflicting_output_fails_fast(
          "sqlite", TBE_COMPILER_LANG_SQLITE, COMPILER_CONFLICTING_OUTPUT_DSL);
      check_database_language_empty_conflicting_output_fails_fast(
          "postgresql", TBE_COMPILER_LANG_POSTGRESQL, COMPILER_CONFLICTING_OUTPUT_DSL);
    }

    it("should preserve an existing output when replacement fails") {
#ifdef _WIN32
      const char *schema_path = "test_tbe_compiler_rename_failure.schema";
      const char *output_path = "test_tbe_compiler_rename_failure.sql";
      const char *schema =
          "[db_table(records)] message Record { [db_primary_key(1)] int64 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_path,
          .lang_enum = TBE_COMPILER_LANG_SQLITE,
      };
      HANDLE output_lock;
      char *output = NULL;
      size_t output_size = 0;

      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(write_test_file(output_path, "known-good-output"), 0);
      output_lock = CreateFileA(output_path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
      check_not_equal(output_lock, INVALID_HANDLE_VALUE);
      if (output_lock != INVALID_HANDLE_VALUE) {
        check_not_equal(tbe_compiler_run(&options), 0);
        CloseHandle(output_lock);
      }
      output = tt_read_file(output_path, &output_size);
      check_not_null(output);
      if (output) {
        check_equal(output, "known-good-output");
        free(output);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(output_path);
#else
      check_true(1);
#endif
    }

    it("should generate RulesForge type declarations with the built-in DSL template") {
      const char *schema_path = "test_tbe_compiler_rfl.schema";
      const char *header_path = "test_tbe_compiler_rfl.h";
      const char *dsl_path = "test_tbe_compiler_rfl.rfl";
      const char *schema =
          "schema Market;"
          "enum Side <uint8> { Buy = 0; Sell = 1; }"
          "composite Header { uint32 seq; }"
          "group Level { uint64 price; uint32 qty; }"
          "message Book { Header header; bytes(16) digest; uuid request_id; "
          "group<Level> bids; string symbol; }";
      size_t dsl_size = 0;
      char *dsl = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = NULL,
          .output_path = header_path,
          .dsl_output_path = dsl_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(dsl_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      dsl = tt_read_file(dsl_path, &dsl_size);
      check_not_null(dsl);
      check(dsl_size > 0);
      if (dsl != NULL) {
        check_contains(dsl, "package Market");
        check_contains(dsl, "enum Side <int>");
        check_contains(dsl, "declare Header");
        check_contains(dsl, "seq: int");
        check_contains(dsl, "declare Level");
        check_contains(dsl, "price: uint64");
        check_contains(dsl, "declare Book");
        check_contains(dsl, "header: Header");
        check_contains(dsl, "digest: Bytes");
        check_contains(dsl, "request_id: UUID");
        check_contains(dsl, "bids: List<Level>");
        check_contains(dsl, "symbol: String");
      }

      free(dsl);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(dsl_path);
    }

    it("should generate a Wasm guest bridge adapter with the built-in C generator") {
      const char *header_path = "test_tbe_compiler_guest.h";
      const char *guest_path = "test_tbe_compiler_guest.c";
      size_t header_size = 0;
      size_t guest_size = 0;
      char *header = NULL;
      char *guest = NULL;
      tbe_compiler_options_t options = {
          .schema_path = SCHEMA_EXAMPLE_FILE,
          .template_path = NULL,
          .output_path = header_path,
          .guest_output_path = guest_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(header_path);
      cleanup_test_file(guest_path);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      guest = tt_read_file(guest_path, &guest_size);
      check_not_null(header);
      check_not_null(guest);
      check(header_size > 0);
      check(guest_size > 0);
      if (header != NULL) {
        check_contains(header, "typedef struct tbe_guest_bridge_s");
        check_contains(header, "uint32_t abi_version;");
        check_contains(header, "record##_guest_from_json");
        check_contains(header, "record##_guest_to_xml");
        check_contains(header, "TBE_GUEST_DECLARE_RECORD(LoginMessage);");
      }
      if (guest != NULL) {
        check_contains(guest, "TBE_GUEST_SCHEMA_ID[] = \"Session\"");
        check_contains(guest, "TBE_GUEST_DEFINE_RECORD(LoginMessage)");
        check_contains(guest, "TBE_GUEST_FORMAT_CSV");
      }
      free(header);
      free(guest);
      cleanup_test_file(header_path);
      cleanup_test_file(guest_path);
    }

    it("should honor c field annotations in typed C output") {
      const char *schema_path = "test_tbe_compiler_c_name.tbe";
      const char *header_path = "test_tbe_compiler_c_name.h";
      const char *source_path = "test_tbe_compiler_c_name.c";
      const char *schema =
          "schema Annotated;"
          "message Order { [c(order_id)] uint32 id; string symbol; }";
      size_t header_size = 0;
      size_t source_size = 0;
      char *header = NULL;
      char *source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = NULL,
          .output_path = header_path,
          .source_output_path = source_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      source = tt_read_file(source_path, &source_size);
      check_not_null(header);
      check_not_null(source);
      if (header != NULL) check_contains(header, "uint32_t order_id;");
      if (source != NULL) {
        check_contains(source, ".name = \"id\"");
        check_contains(source, "offsetof(Order_t, order_id)");
      }

      free(header);
      free(source);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
    }

    it("should partition CMeta descriptor wrappers at generation time") {
      const char *schema_path = "test_tbe_compiler_cmeta_descriptor.tbe";
      const char *header_path = "test_tbe_compiler_cmeta_descriptor.h";
      const char *source_path = "test_tbe_compiler_cmeta_descriptor.c";
      const char *schema =
          "schema DescriptorGraph;"
          "enum State <uint8> { Idle = 0; Ready = 7; }"
          "flags Permission <uint8> { Read = 1; Write = 2; }"
          "enum WideDomain <uint64> { Zero = 0; Maximum = 18446744073709551615; }"
          "composite Point { int32 x; double y; }"
          "message Sample { Point point; State state; uint32 count; }"
          "message FlagStorage { Permission value; }"
          "message WideEnumStorage { WideDomain value; }"
          "message LoginMessage { string user; }";
      size_t header_size = 0;
      size_t source_size = 0;
      char *header = NULL;
      char *source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = NULL,
          .output_path = header_path,
          .source_output_path = source_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      source = tt_read_file(source_path, &source_size);
      check_not_null(header);
      check_not_null(source);
      if (header != NULL) {
        check_contains(header,
                       "const TbeTypedDescriptor *Sample_typed_descriptor(void)");
        check_contains(header,
                       "const TbeTypedDescriptor *FlagStorage_typed_descriptor(void)");
        check_contains(header,
                       "const TbeTypedDescriptor *WideEnumStorage_typed_descriptor(void)");
        check(strstr(header,
                     "const TbeTypedDescriptor *LoginMessage_typed_descriptor(void)") == NULL);
      }
      if (source != NULL) {
        check_contains(source,
                       "TBE_TYPED_DESCRIPTOR_INIT(&Sample_TYPED_TYPE, &Sample_CMETA_DATA)");
        check_contains(source, "const TbeTypedDescriptor *Sample_typed_descriptor(void)");
        check_contains(source, "static const cmeta_data_enum_ops State_CMETA_ENUM_OPS");
        check_contains(source, "State_cmeta_read, State_cmeta_assign");
        check_contains(source, "&State_CMETA_ENUM_OPS");
        check_contains(source, "static const cmeta_data_enum_ops Permission_CMETA_ENUM_OPS");
        check_contains(source, "static const cmeta_data_enum_ops WideDomain_CMETA_ENUM_OPS");
        check_contains(source,
                       "TBE_TYPED_DESCRIPTOR_INIT(&FlagStorage_TYPED_TYPE, &FlagStorage_CMETA_DATA)");
        check_contains(source,
                       "TBE_TYPED_DESCRIPTOR_INIT(&WideEnumStorage_TYPED_TYPE, &WideEnumStorage_CMETA_DATA)");
        check(strstr(source, "LoginMessage_TYPED_DESCRIPTOR") == NULL);
        check_contains(source, "TBE_TYPED_DEFINE_DESCRIPTOR_RECORD(Sample)");
        check_contains(source, "TBE_TYPED_DEFINE_DESCRIPTOR_RECORD(FlagStorage)");
        check_contains(source, "TBE_TYPED_DEFINE_DESCRIPTOR_RECORD(WideEnumStorage)");
        check_contains(source, "TBE_TYPED_DEFINE_RAW_RECORD(LoginMessage)");
        check_contains(source, "tbe_typed_descriptor_init(&name##_TYPED_DESCRIPTOR");
        check_contains(source, "tbe_typed_descriptor_clear(&name##_TYPED_DESCRIPTOR");
        check_contains(source, "tbe_typed_descriptor_parse(codec, #name, &name##_TYPED_DESCRIPTOR");
        check_contains(source, "tbe_typed_descriptor_serialize(codec, #name, &name##_TYPED_DESCRIPTOR");
        check_contains(source, "tbe_typed_descriptor_serialize_binary(&name##_TYPED_DESCRIPTOR");
        check_contains(source,
                       "tbe_typed_descriptor_serialize_binary_into(&name##_TYPED_DESCRIPTOR");
        check_contains(source, "tbe_typed_init(&name##_TYPED_TYPE");
        check_contains(source, "tbe_typed_parse_ex(codec, #name, &name##_TYPED_TYPE");
        check_contains(source, "tbe_typed_serialize_ex(codec, #name, &name##_TYPED_TYPE");
      }

      free(header);
      free(source);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
    }

    it("should generate typed C to Lua adapters") {
      const char *header_path = "test_tbe_compiler_lua.h";
      const char *source_path = "test_tbe_compiler_lua_typed.c";
      const char *lua_path = "test_tbe_compiler_lua.c";
      size_t header_size = 0;
      size_t lua_size = 0;
      char *header = NULL;
      char *lua_source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = SCHEMA_EXAMPLE_FILE,
          .template_path = NULL,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      lua_source = tt_read_file(lua_path, &lua_size);
      check_not_null(header);
      check_not_null(lua_source);
      check(header_size > 0);
      check(lua_size > 0);
      if (header != NULL) {
        check_contains(header, "LoginMessage_typed_type(void)");
        check_contains(header, "LoginMessage_push_lua(struct lua_State *L");
        check_contains(header, "LoginMessage_from_lua(struct lua_State *L");
      }
      if (lua_source != NULL) {
        check_contains(lua_source, "#include \"salts_lua.h\"");
        check_contains(lua_source, "TBE_LUA_DEFINE_RAW_RECORD(LoginMessage)");
        check_contains(lua_source, "c11_lua_read_tbe_typed");
      }

      free(header);
      free(lua_source);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should route supported Lua records through their public descriptor") {
      const char *schema_path = "test_tbe_compiler_lua_descriptor.tbe";
      const char *header_path = "test_tbe_compiler_lua_descriptor.h";
      const char *source_path = "test_tbe_compiler_lua_descriptor_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_descriptor.c";
      const char *schema =
          "schema LuaDescriptor;"
          "enum State <uint8> { Idle = 0; Ready = 7; }"
          "composite Point { int32 x; double y; }"
          "message Sample { Point point; State state; uint32 count; }";
      size_t header_size = 0;
      size_t lua_size = 0;
      char *header = NULL;
      char *lua_source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = NULL,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      lua_source = tt_read_file(lua_path, &lua_size);
      check_not_null(header);
      check_not_null(lua_source);
      if (header != NULL) {
        check_contains(header, "Sample_typed_descriptor(void)");
        check(strstr(header, "Sample_typed_type(void)") == NULL);
      }
      if (lua_source != NULL) {
        check_contains(lua_source, "TBE_LUA_DEFINE_DESCRIPTOR_RECORD(Sample)");
        check_contains(lua_source, "c11_lua_push_tbe_typed_descriptor");
        check_contains(lua_source, "c11_lua_read_tbe_typed_descriptor");
        check_contains(lua_source, "name##_typed_descriptor()");
      }

      free(header);
      free(lua_source);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should require typed metadata for Lua adapter output") {
      const char *header_path = "test_tbe_compiler_lua_invalid.h";
      const char *lua_path = "test_tbe_compiler_lua_invalid.c";
      tbe_compiler_options_t options = {
          .schema_path = SCHEMA_EXAMPLE_FILE,
          .template_path = NULL,
          .output_path = header_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(header_path);
      cleanup_test_file(lua_path);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(header_path);
      cleanup_test_file(lua_path);
    }

    it("should generate schema-declared Lua operation callbacks") {
      const char *schema_path = "test_tbe_compiler_lua_operation.tbe";
      const char *header_path = "test_tbe_compiler_lua_operation.h";
      const char *source_path = "test_tbe_compiler_lua_operation_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_operation.c";
      const char *schema =
          "schema Orders;"
          "[lua_operation(create_order), lua_response(OrderResult)] "
          "message CreateOrder { uint32 id; }"
          "[lua_operation(fetch_order), lua_response(OrderResult), lua_async(future)] "
          "message FetchOrder { uint32 id; }"
          "message EnrichOrder { uint32 id; }"
          "message AsyncEnrichOrder { uint32 id; }"
          "message OrderResult { uint32 id; }";
      size_t header_size = 0;
      size_t lua_size = 0;
      char *header = NULL;
      char *lua_source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      lua_source = tt_read_file(lua_path, &lua_size);
      check_not_null(header);
      check_not_null(lua_source);
      if (header != NULL) {
        check_contains(header, "typedef struct Orders_lua_api_s");
        check_contains(header, "DataBindStatus (*create_order)");
        check_contains(header, "typedef struct Orders_lua_fetch_order_async_s");
        check_contains(header, "Orders_lua_fetch_order_async_t *operation");
        check_contains(header, "void (*destroy)(void *state, int canceled)");
        check_contains(header, "size_t max_pending_operations");
        check(strstr(header, "salts_coro") == NULL);
        check(strstr(header, "coroutine_stack_size") == NULL);
        check_contains(header, "Orders_lua_push_module");
      }
      if (lua_source != NULL) {
        check_contains(lua_source, "#include \"salts_lua.h\"");
        check_contains(lua_source, "Orders_lua_call_create_order");
        check_contains(lua_source, "Orders_lua_fetch_order_poll");
        check_contains(lua_source, "Orders_lua_fetch_order_await");
        check_contains(lua_source, "lua_yieldk");
        check_contains(lua_source, "future->operation.poll");
        check_contains(lua_source, "future->operation.destroy");
        check(strstr(lua_source, "salts_coro") == NULL);
        check_contains(lua_source, "CreateOrder_from_lua");
        check_contains(lua_source, "OrderResult_push_lua");
      }

      free(header);
      free(lua_source);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should reject removed synchronous Lua imports") {
      const char *schema_path = "test_tbe_compiler_lua_import.tbe";
      const char *header_path = "test_tbe_compiler_lua_import.h";
      const char *source_path = "test_tbe_compiler_lua_import_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_import.c";
      const char *schema =
          "schema Rules;"
          "[lua_import(evaluate), lua_response(Result)] "
          "message Request { uint32 id; }"
          "message Result { uint32 id; }";
      size_t header_size = 0;
      size_t lua_size = 0;
      char *header = NULL;
      char *lua_source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      return;
      header = tt_read_file(header_path, &header_size);
      lua_source = tt_read_file(lua_path, &lua_size);
      check_not_null(header);
      check_not_null(lua_source);
      if (header != NULL) {
        check_contains(header, "typedef struct Rules_lua_limits_s");
        check_contains(header, "typedef struct Rules_lua_client_s");
        check_contains(header, "Rules_lua_client_close");
        check_contains(header, "Rules_lua_client_evaluate_on_owner");
      }
      if (lua_source != NULL) {
        check_contains(lua_source, "struct Rules_lua_client_s");
        check_contains(lua_source, "Rules_lua_client_create");
        check_contains(lua_source, "Request_push_lua");
        check_contains(lua_source, "Result_from_lua");
      }

      free(header);
      free(lua_source);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should generate Future operations without C coroutine dependencies") {
      const char *schema_path = "test_tbe_compiler_lua_future.tbe";
      const char *header_path = "test_tbe_compiler_lua_future.h";
      const char *source_path = "test_tbe_compiler_lua_future_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_future.c";
      const char *schema =
          "schema Stateful;"
          "[lua_operation(fetch), lua_response(Result), lua_async(future)] "
          "message Request { uint32 id; }"
          "message Result { uint32 id; }";
      size_t header_size = 0;
      size_t lua_size = 0;
      char *header = NULL;
      char *lua_source = NULL;
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check_equal(tbe_compiler_run(&options), 0);
      header = tt_read_file(header_path, &header_size);
      lua_source = tt_read_file(lua_path, &lua_size);
      check_not_null(header);
      check_not_null(lua_source);
      if (header != NULL) {
        check_contains(header, "Stateful_lua_fetch_async_t");
        check_contains(header, "size_t max_pending_operations");
        check(strstr(header, "#include \"salts_coro.h\"") == NULL);
        check(strstr(header, "coroutine_stack_size") == NULL);
      }
      if (lua_source != NULL) {
        check_contains(lua_source, "future->operation.poll");
        check_contains(lua_source, "future->operation.destroy");
        check(strstr(lua_source, "salts_coro_pool") == NULL);
      }

      free(header);
      free(lua_source);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should reject the removed salts_coro async interface") {
      const char *schema_path = "test_tbe_compiler_lua_async_invalid.tbe";
      const char *header_path = "test_tbe_compiler_lua_async_invalid.h";
      const char *source_path = "test_tbe_compiler_lua_async_invalid_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_async_invalid.c";
      const char *schema =
          "schema Orders;"
          "[lua_operation(fetch_order), lua_response(OrderResult), lua_async(salts_coro)] "
          "message FetchOrder { uint32 id; }"
          "message OrderResult { uint32 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should reject removed asynchronous Lua imports") {
      const char *schema_path = "test_tbe_compiler_lua_import_async.tbe";
      const char *header_path = "test_tbe_compiler_lua_import_async.h";
      const char *source_path = "test_tbe_compiler_lua_import_async_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_import_async.c";
      const char *schema =
          "schema Orders;"
          "[lua_import(fetch_order), lua_response(OrderResult), lua_async(future)] "
          "message FetchOrder { uint32 id; }"
          "message OrderResult { uint32 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      return;
      {
        size_t header_size = 0;
        size_t lua_size = 0;
        char *header = tt_read_file(header_path, &header_size);
        char *lua_source = tt_read_file(lua_path, &lua_size);
        check_not_null(header);
        check_not_null(lua_source);
        if (header != NULL) {
          check_contains(header, "typedef struct salts_lua_executor salts_lua_executor_t");
          check_contains(header, "Orders_lua_client_close");
          check_contains(header, "Orders_lua_client_fetch_order_async");
          check_contains(header, "Orders_lua_fetch_order_future_poll");
          check_contains(header, "Orders_lua_fetch_order_future_cancel");
        }
        if (lua_source != NULL) {
          check_contains(lua_source, "tbe_typed_serialize_binary");
          check_contains(lua_source, "salts_lua_executor_try_post");
          check_contains(lua_source, "Orders_lua_fetch_order_dispatch");
        }
        free(header);
        free(lua_source);
      }
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should reject an unknown Lua operation response type") {
      const char *schema_path = "test_tbe_compiler_lua_operation_invalid.tbe";
      const char *header_path = "test_tbe_compiler_lua_operation_invalid.h";
      const char *source_path = "test_tbe_compiler_lua_operation_invalid_typed.c";
      const char *lua_path = "test_tbe_compiler_lua_operation_invalid.c";
      const char *schema =
          "schema Orders;"
          "[lua_operation(create_order), lua_response(Missing)] "
          "message CreateOrder { uint32 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = header_path,
          .source_output_path = source_path,
          .lua_output_path = lua_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      cleanup_test_file(lua_path);
    }

    it("should reject duplicate c field annotations in typed C output") {
      const char *schema_path = "test_tbe_compiler_c_collision.tbe";
      const char *header_path = "test_tbe_compiler_c_collision.h";
      const char *source_path = "test_tbe_compiler_c_collision.c";
      const char *schema =
          "schema Annotated;"
          "message Order { [c(value)] uint32 id; [c(value)] string symbol; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = NULL,
          .output_path = header_path,
          .source_output_path = source_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
    }

    it("should reject invalid c identifiers in typed C output") {
      const char *schema_path = "test_tbe_compiler_c_invalid.tbe";
      const char *header_path = "test_tbe_compiler_c_invalid.h";
      const char *source_path = "test_tbe_compiler_c_invalid.c";
      const char *schema =
          "schema Annotated;"
          "message Order { [c(\"order-id\")] uint32 id; }";
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .template_path = NULL,
          .output_path = header_path,
          .source_output_path = source_path,
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
      check_equal(write_test_file(schema_path, schema), 0);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(schema_path);
      cleanup_test_file(header_path);
      cleanup_test_file(source_path);
    }

    it("should reject guest adapter output outside the built-in C generator") {
      const char *output_path = "test_tbe_compiler_guest_invalid.out";
      tbe_compiler_options_t options = {
          .schema_path = SCHEMA_EXAMPLE_FILE,
          .template_path = C_STRUCT_TEMPLATE_FILE,
          .output_path = output_path,
          .guest_output_path = "test_tbe_compiler_guest_invalid.c",
          .lang_enum = TBE_COMPILER_LANG_C,
      };

      cleanup_test_file(output_path);
      cleanup_test_file(options.guest_output_path);
      check(tbe_compiler_run(&options) != 0);
      cleanup_test_file(output_path);
      cleanup_test_file(options.guest_output_path);
    }

    it("should render C++ Go Python Rust and TypeScript type outputs") {
      const char *schema =
          "schema Market [byte_order(little)];"
          "enum Side <uint8> { Buy = 1; Sell = 2; }"
          "composite Header { uint32_t seq; }"
          "group Level { uint64 price; uint32 qty; }"
          "message Book { Header header; bytes(16) digest; uuid request_id; group<Level> bids; "
          "string symbol; }";
      char *cpp_output = render_compiler_template_from_schema(
          schema, "test_tbe_compiler_lang.schema", CPP_TYPES_TEMPLATE_FILE,
          "test_tbe_compiler_lang.cpp.out");
      char *go_output = render_compiler_template_from_schema(
          schema, "test_tbe_compiler_lang.schema", GO_TYPES_TEMPLATE_FILE,
          "test_tbe_compiler_lang.go.out");
      char *py_output = render_compiler_template_from_schema(
          schema, "test_tbe_compiler_lang.schema", PYTHON_DATACLASS_TEMPLATE_FILE,
          "test_tbe_compiler_lang.py.out");
      char *rust_output = render_compiler_template_from_schema(
          schema, "test_tbe_compiler_lang.schema", RUST_STRUCTS_TEMPLATE_FILE,
          "test_tbe_compiler_lang.rs.out");
      char *ts_output = render_compiler_template_from_schema(
          schema, "test_tbe_compiler_lang.schema", TS_TYPES_TEMPLATE_FILE,
          "test_tbe_compiler_lang.ts.out");

      check_not_null(cpp_output);
      check_not_null(go_output);
      check_not_null(py_output);
      check_not_null(rust_output);
      check_not_null(ts_output);

      check_contains(cpp_output, "enum class Side : std::uint8_t");
      check_contains(cpp_output, "std::vector<Level> bids;");
      check_contains(cpp_output, "std::string symbol;");
      check_contains(cpp_output, "std::vector<std::uint8_t> digest;");
      check_contains(cpp_output, "salts_uuid_t request_id;");
      check_contains(cpp_output, "#include \"salts_uuid.h\"");

      check_contains(go_output, "package market");
      check_contains(go_output, "Bids []Level");
      check_contains(go_output, "Symbol string");
      check_contains(go_output, "Digest []byte");
      check_contains(go_output, "RequestId [16]byte");

      check_contains(py_output, "bids: list[Level]");
      check_contains(py_output, "symbol: str");
      check_contains(py_output, "digest: bytes");
      check_contains(py_output, "request_id: str");

      check_contains(rust_output, "#[repr(u8)]");
      check_contains(rust_output, "pub bids: Vec<Level>");
      check_contains(rust_output, "pub symbol: String");
      check_contains(rust_output, "pub digest: Vec<u8>");
      check_contains(rust_output, "pub request_id: [u8; 16]");

      check_contains(ts_output, "export enum Side");
      check_contains(ts_output, "bids: Array<Level>;");
      check_contains(ts_output, "symbol: string;");
      check_contains(ts_output, "digest: Uint8Array;");
      check_contains(ts_output, "request_id: string;");

      free(cpp_output);
      free(go_output);
      free(py_output);
      free(rust_output);
      free(ts_output);
    }

    it("should render implicit enum values and variable bytes safely") {
      const char *schema = "enum Color { Red; Green = 5; Blue; } "
                           "message Blob { bytes(16) digest; bytes payload; }";
      char *output = render_c_template(schema);

      check_not_null(output);
      check_contains(output, "#define Color_Red ((Color_t)UINT64_C(0))");
      check_contains(output, "#define Color_Green ((Color_t)UINT64_C(5))");
      check_contains(output, "#define Color_Blue ((Color_t)UINT64_C(6))");
      check_contains(output, "bytes payload;");
      check_contains(output, "uint8_t digest[16];");
      check_contains(output, "typedef struct Blob_builder_s {");
      check_contains(output, "static inline bool Blob_builder_bind");
      check_contains(output, "static inline bool Blob_payload_set(");
      check_contains(output, "return tbe_wire_write_var_data(view->data + payload_offset,");
      check_contains(output, "static inline bool Blob_payload(");
      check_contains(output, "tbe_var_data_t *value");
      check_contains(output, "return tbe_wire_read_var_data(view->data + payload_offset,");
      check_contains(output, "return tbe_wire_read_var_data(view->data + payload_offset,");
      check(strstr(output, "uint8_t payload[") == NULL);

      free(output);
    }

    it("should render UUID fields with the installed Core type") {
      const char *schema = "message Event { uuid request_id; }";
      char *output = render_c_template(schema);

      check_not_null(output);
      check_contains(output, "#include \"salts_uuid.h\"");
      check_contains(output, "salts_uuid_t request_id;");
      check_contains(output, "enum { Event_BLOCK_LENGTH = 16 };");
      check_contains(output, "static inline bool Event_request_id_set(");
      check_contains(output, "const salts_uuid_t *value");
      check_contains(output, "static inline bool Event_request_id_get(");
      check_contains(output, "salts_uuid_t *value");

      free(output);
    }

    it("should emit valid C underlying types for flags and no trailing enum commas") {
      const char *schema = "schema GuardTest [id(1), version(1)]; "
                           "enum Side <uint8> { Buy = 1; Sell = 2; } "
                           "enum Wide <uint64> { Max = 18446744073709551615; } "
                           "flags Perms <uint16> { Read = 1; Write = 2; Execute = 4; }";
      const char *guard_end = "#endif /* GuardTest_GENERATED_H */";
      char *output = render_c_template(schema);
      size_t output_len = output ? strlen(output) : 0;
      size_t guard_end_len = strlen(guard_end);

      while (output_len > 0 && (output[output_len - 1] == '\r' || output[output_len - 1] == '\n'))
        --output_len;

      check_not_null(output);
      check_contains(output, "#ifndef GuardTest_GENERATED_H");
      check_contains(output, "#define GuardTest_GENERATED_H");
      check_greater_equal(output_len, guard_end_len);
      if (output_len >= guard_end_len)
        check_equal(output + output_len - guard_end_len, guard_end, guard_end_len);
      check_contains(output, "typedef uint16_t Perms_t;");
      check_contains(output, "typedef uint8_t Side_t;");
      check_contains(output, "typedef uint64_t Wide_t;");
      check_contains(
          output,
          "#define Wide_Max ((Wide_t)UINT64_C(18446744073709551615))");
      check(strstr(output, "typedef uint16 Perms_t;") == NULL);
      check(strstr(output, "Side_Sell = 2,") == NULL);
      check(strstr(output, "Perms_Execute = 4,") == NULL);
      check_contains(output, "static inline bool Perms_has(");
      check_contains(output, "static inline bool Side_is_valid(");

      free(output);
    }

    it("should render typed nested composite view and builder accessors") {
      const char *schema = "composite Header { uint32 seq_num; uint64 timestamp; } "
                           "message Envelope { uint32 channel; Header header; }";
      char *output = render_c_template(schema);

      check_not_null(output);
      check_contains(output, "typedef struct Header_builder_s {");
      check_contains(output, "typedef struct Envelope_builder_s {");
      check_contains(output, "static inline bool Envelope_builder_bind");
      check_contains(output, "static inline bool Envelope_channel_set");
      check_contains(output, "enum { Envelope_header_OFFSET = 4 };");
      check_contains(output, "static inline bool Envelope_header(");
      check_contains(output, "Header_view_t *value");
      check_contains(output, "return Header_view_bind(value, view->data + 4, view->size - 4);");
      check_contains(output, "static inline bool Envelope_header_builder(");
      check_contains(output, "Header_builder_t *value");
      check_contains(output,
                         "return Header_builder_bind(value, view->data + 4, view->size - 4);");
      check_contains(output, "static inline const uint8_t *Envelope_header_ptr");

      free(output);
    }

    it("should keep enum fields fixed-size and generate enum writers") {
      const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; } "
                           "message Quote { Side side; uint32 qty; }";
      char *output = render_c_template(schema);

      check_not_null(output);
      check_contains(output, "typedef struct Quote_builder_s {");
      check_contains(output, "static inline bool Quote_builder_bind");
      check_contains(output, "enum { Quote_BLOCK_LENGTH = 5 };");
      check_contains(output, "enum { Quote_side_OFFSET = 0 };");
      check_contains(output, "enum { Quote_qty_OFFSET = 1 };");
      check_contains(output, "Side_t side;");
      check_contains(output, "static inline Side_t Quote_side_get");
      check_contains(output, "static inline bool Quote_side_set");
      check_contains(
          output,
          "tbe_wire_write_u8(view->data + 0, GeneratedSchema_WIRE_BIG_ENDIAN, (uint8_t)value);");
      check_contains(output, "static inline bool Quote_qty_set");
      check_contains(
          output, "tbe_wire_write_u32(view->data + 1, GeneratedSchema_WIRE_BIG_ENDIAN, value);");
      check_contains(output, "return (Side_t)tbe_wire_read_u8(view->data + 0,");
      check_contains(output, "GeneratedSchema_WIRE_BIG_ENDIAN");
      check(strstr(output, "Side_view_t") == NULL);

      free(output);
    }

    it("should render fixed bytes and fixed array writers safely") {
      const char *schema = "enum Side <uint8> { Buy = 1; Sell = 2; } "
                           "composite Point { int32 x; int32 y; } "
                           "message Payloads { bytes(16) digest; Point[2] points; uint32[4] "
                           "values; Side[2] sides; }";
      char *output = render_c_template(schema);

      check_not_null(output);
      check_contains(output, "enum { Payloads_digest_OFFSET = 0 };");
      check_contains(output, "enum { Payloads_points_OFFSET = 16 };");
      check_contains(output, "enum { Payloads_values_OFFSET = 32 };");
      check_contains(output, "enum { Payloads_sides_OFFSET = 48 };");
      check_contains(output, "static inline bool Payloads_digest_set(");
      check_contains(output, "size != 16");
      check_contains(output, "TBE_GENERATED_MEMCPY(view->data + 0, data, 16);");
      check_contains(output, "static inline bool Payloads_points_builder_at(");
      check_contains(output, "Point_builder_t *value");
      check_contains(output, "index >= 2");
      check_contains(output, "element_offset = 16 + ((size_t)index * 8);");
      check_contains(output, "return Point_builder_bind(value, view->data + element_offset, "
                                 "view->size - element_offset);");
      check_contains(output, "static inline bool Payloads_values_set_at(");
      check_contains(output, "index >= 4");
      check_contains(output, "tbe_wire_write_u32(");
      check_contains(output, "view->data + 32 + ((size_t)index * 4),");
      check_contains(output, "GeneratedSchema_WIRE_BIG_ENDIAN, value);");
      check_contains(output, "static inline bool Payloads_sides_set_at(");
      check_contains(output, "tbe_wire_write_u8(");
      check_contains(output, "view->data + 48 + ((size_t)index * 1),");
      check_contains(output, "(uint8_t)value);");

      free(output);
    }

    it("should render schema composites groups and messages") {
      const char *schema = "schema Market [id(7), version(2), byte_order(little)]; "
                           "composite Header { uint32 seq_num; uint64 timestamp; } "
                           "group Level { uint64 price; uint32 qty; } "
                           "[id(100), version(1)] message BookSnapshot { "
                           "Header header; "
                           "group<Level> bids; "
                           "string symbol; "
                           "bytes source; }";
      char *output = render_c_template(schema);

      check_not_null(output);
      check_contains(output, "typedef struct Header_s {");
      check_contains(output, "typedef struct Level_s {");
      check_contains(output, "typedef struct BookSnapshot_s {");
      check_contains(output, "#include \"tbe_wire.h\"");
      check_contains(output, "enum { Market_WIRE_BIG_ENDIAN = 0 };");
      check_contains(output, "Header_t header;");
      check_contains(output, "list<Level> bids;");
      check_contains(output, "enum { Header_BLOCK_LENGTH = 12 };");
      check_contains(output, "enum { BookSnapshot_BLOCK_LENGTH = 12 };");
      check_contains(output, "enum { BookSnapshot_header_OFFSET = 0 };");
      check_contains(output, "typedef struct Level_cursor_s {");
      check_contains(output, "static inline bool Level_cursor_bind");
      check_contains(
          output,
          "cursor->block_length = tbe_wire_read_u16(cursor->data, Market_WIRE_BIG_ENDIAN);");
      check_contains(output, "static inline bool Level_cursor_get");
      check_contains(output, "typedef struct BookSnapshot_view_s {");
      check_contains(output, "static inline bool BookSnapshot_view_bind");
      check_contains(output, "typedef struct BookSnapshot_builder_s {");
      check_contains(output, "static inline bool BookSnapshot_builder_bind");
      check_contains(output, "static inline const uint8_t *BookSnapshot_header_ptr");
      check_contains(output, "static inline bool BookSnapshot_bids_cursor");
      check_contains(output, "static inline bool BookSnapshot_symbol(");
      check_contains(output, "return tbe_wire_read_var_data(payload_data,");
      check_contains(output, "static inline bool BookSnapshot_symbol_set(");
      check_contains(output, "BookSnapshot_view_t read_view;");
      check_contains(output, "if (!BookSnapshot_bids_cursor(&read_view, &previous)) {");
      check_contains(output, "return tbe_wire_write_var_data(");
      check_contains(output, "static inline bool BookSnapshot_source(");
      check_contains(output, "tbe_wire_var_data_end(&previous);");
      check_contains(output, "static inline bool BookSnapshot_source_set(");
      check_contains(output, "if (!BookSnapshot_symbol(&read_view, &previous)) {");
      check_contains(output, "return Level_cursor_bind(cursor, view->data + group_offset, "
                                 "view->size - group_offset);");
      check_contains(output, "if (!BookSnapshot_symbol(view, &previous)) {");
      check_contains(output, "payload_data = tbe_wire_var_data_end(&previous);");
      check_contains(output, "static inline uint32_t Header_seq_num_get");
      check_contains(output,
                         "return tbe_wire_read_u32(view->data + 0, Market_WIRE_BIG_ENDIAN);");

      free(output);
    }
  }
}
