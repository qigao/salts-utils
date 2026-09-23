#include "compiler_core.h"
#include "tinytest.h"
#include "schema_parser_dsl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_text_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  size_t length;

  if (!file) return -1;
  length = strlen(text);
  if (fwrite(text, 1, length, file) != length) {
    fclose(file);
    return -1;
  }
  return fclose(file);
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  fclose(file);
  return 1;
}

spec("data_bind_compiler_schema_boundary") {
  it("rejects varint before every language target can publish output") {
    static const int64_t languages[] = {
        DATABIND_COMPILER_LANG_C,
        DATABIND_COMPILER_LANG_PYTHON,
        DATABIND_COMPILER_LANG_RUST,
        DATABIND_COMPILER_LANG_CPP,
        DATABIND_COMPILER_LANG_GO,
        DATABIND_COMPILER_LANG_TS,
        DATABIND_COMPILER_LANG_SQLITE,
        DATABIND_COMPILER_LANG_POSTGRESQL,
    };
    static const char *const output_paths[] = {
        "test_varint_boundary_c.out",
        "test_varint_boundary_python.out",
        "test_varint_boundary_rust.out",
        "test_varint_boundary_cpp.out",
        "test_varint_boundary_go.out",
        "test_varint_boundary_ts.out",
        "test_varint_boundary_sqlite.out",
        "test_varint_boundary_postgresql.out",
    };
    static const char schema_path[] = "test_varint_boundary.schema";
    static const char schema[] = "message Bad { varint value; }";

    check_equal(sizeof(languages) / sizeof(languages[0]),
                sizeof(output_paths) / sizeof(output_paths[0]));
    remove(schema_path);
    check_equal(write_text_file(schema_path, schema), 0);

    for (size_t i = 0; i < sizeof(languages) / sizeof(languages[0]); ++i) {
      data_bind_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_paths[i],
          .resource_dir = DATABIND_COMPILER_RESOURCE_DIR,
          .lang_enum = languages[i],
      };
      int status;

      remove(output_paths[i]);
      status = data_bind_compiler_run(&options);
      info("language=%lld output=%s", (long long)languages[i], output_paths[i]);
      check_not_equal(status, 0);
      check_false(file_exists(output_paths[i]));
      remove(output_paths[i]);
    }

    remove(schema_path);
  }
}


/* #45: these exercise the production parser and default C template, not a
 * second type classifier implemented by the test. */
static Node *default_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP) return NULL;
  for (i = 0; i < parent->data.map.count; ++i) {
    Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL && strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static int default_text_is(Node *parent, const char *name, const char *expected) {
  Node *child = default_child(parent, name);
  return child != NULL && child->type == NODE_STRING &&
         child->data.string_val != NULL && strcmp(child->data.string_val, expected) == 0;
}

static size_t default_occurrences(const char *text, const char *needle) {
  size_t count = 0;
  size_t length = strlen(needle);
  while ((text = strstr(text, needle)) != NULL) {
    ++count;
    text += length;
  }
  return count;
}

spec("data_bind_compiler_default_type_identity") {
  it("does not classify enum or flags defaults by substrings in their declared names") {
    static const char *const names[] = {"Paint", "uintMode", "floatMode", "doubleMode"};
    static const char *const declarations[] = {"enum", "flags"};
    enum { SCHEMA_CAPACITY = 256 };
    size_t i, j;
    for (j = 0; j < sizeof(declarations) / sizeof(declarations[0]); ++j) {
      for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char schema[SCHEMA_CAPACITY];
        Node *root = create_node_map("root");
        Node *messages, *defaults, *field = NULL;
        int matched = 0;
        int count = snprintf(schema, sizeof(schema),
            "%s %s <uint8> { On=1; } message Settings { optional %s mode default On; }",
            declarations[j], names[i], names[i]);
        int status;
        check_not_null(root);
        check_true(count > 0 && (size_t)count < sizeof(schema));
        status = parse_schema(schema, (size_t)count, root, NULL);
        messages = default_child(root, "messages");
        if (messages != NULL && messages->type == NODE_LIST && messages->data.list.count == 1u) {
          defaults = default_child(messages->data.list.items[0], "default_value_fields");
          if (defaults != NULL && defaults->type == NODE_LIST && defaults->data.list.count == 1u)
            field = defaults->data.list.items[0];
        }
        if (field != NULL) {
          matched = default_text_is(field, "type", names[i]) &&
                    default_text_is(field, "enum_name", names[i]) &&
                    default_text_is(field, "default_value", "On") &&
                    default_child(field, "is_enum_ref") != NULL &&
                    default_child(field, "is_numeric") == NULL &&
                    default_child(field, "is_boolean") == NULL &&
                    default_child(field, "is_string") == NULL;
        }
        node_free(root);
        info("declaration=%s name=%s", declarations[j], names[i]);
        check_equal(status, 0);
        check_true(matched);
      }
    }
  }

  it("emits exactly one qualified enum default macro from the real C template") {
    static const char schema_path[] = "test_default_identity.schema";
    static const char output_path[] = "test_default_identity.h";
    static const char schema[] =
        "enum Paint <uint8> { Red=1; } "
        "message Settings { optional Paint color default Red; }";
    data_bind_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = DATABIND_COMPILER_RESOURCE_DIR,
        .lang_enum = DATABIND_COMPILER_LANG_C,
    };
    char *output;
    int status, qualified = 0;
    size_t definitions = 0;
    remove(schema_path);
    remove(output_path);
    check_equal(write_text_file(schema_path, schema), 0);
    status = data_bind_compiler_run(&options);
    output = data_bind_compiler_read_file(output_path);
    if (output != NULL) {
      definitions = default_occurrences(output, "#define Settings_color_DEFAULT ");
      qualified = strstr(output, "#define Settings_color_DEFAULT Paint_Red") != NULL;
    }
    free(output);
    remove(schema_path);
    remove(output_path);
    check_equal(status, 0);
    check_true(qualified);
    check_equal(definitions, (size_t)1u);
  }
}
