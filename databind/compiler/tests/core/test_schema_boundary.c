#include "compiler_core.h"
#include "tinytest.h"
#include "binary_contract_overlay.h"

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

spec("tbe_compiler_schema_boundary") {
  it("rejects varint before every language target can publish output") {
    static const int64_t languages[] = {
        TBE_COMPILER_LANG_C,
        TBE_COMPILER_LANG_PYTHON,
        TBE_COMPILER_LANG_RUST,
        TBE_COMPILER_LANG_CPP,
        TBE_COMPILER_LANG_GO,
        TBE_COMPILER_LANG_TS,
        TBE_COMPILER_LANG_SQLITE,
        TBE_COMPILER_LANG_POSTGRESQL,
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
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_paths[i],
          .resource_dir = TBE_COMPILER_RESOURCE_DIR,
          .lang_enum = languages[i],
      };
      int status;

      remove(output_paths[i]);
      status = tbe_compiler_run(&options);
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

spec("tbe_compiler_default_type_identity") {
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
        status = databind_binary_contract_parse(schema, (size_t)count, root, NULL);
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
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
    };
    char *output;
    int status, qualified = 0;
    size_t definitions = 0;
    remove(schema_path);
    remove(output_path);
    check_equal(write_text_file(schema_path, schema), 0);
    status = tbe_compiler_run(&options);
    output = tbe_compiler_read_file(output_path);
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

spec("idl_contract_data_name_admission") {
  it("rejects duplicate fields before publishing an immutable contract") {
    static const char idl[] =
        "schema Sample; message Packet { int32 id; uint32 id; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_false(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_null(contract);
    check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
    check_true(strstr(diagnostic.message,
                      "Duplicate field 'id' in Data declaration 'Packet'") != NULL);
  }

  it("rejects colliding Data names across declaration kinds") {
    static const char idl[] =
        "schema Sample; message Packet { int32 id; } "
        "composite Packet { uint32 id; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_false(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_null(contract);
    check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
    check_true(strstr(diagnostic.message,
                      "Duplicate Data declaration 'Packet'") != NULL);
  }

  it("does not publish a generated artifact with colliding field names") {
    static const char schema_path[] = "test_idl_duplicate_fields.schema";
    static const char output_path[] = "test_idl_duplicate_fields.h";
    static const char idl[] =
        "schema Sample; message Packet { int32 id; uint32 id; }";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
    };
    remove(schema_path);
    remove(output_path);
    check_equal(write_text_file(schema_path, idl), 0);
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(output_path));
    remove(schema_path);
    remove(output_path);
  }

  it("allows the same field name in unrelated Data declarations") {
    static const char idl[] =
        "schema Sample; message Request { int32 id; } "
        "composite Response { uint32 id; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_true(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_not_null(contract);
    check_equal(diagnostic.status, IDL_OK);
    idl_contract_destroy(contract);
  }
}


spec("idl_contract_logical_type_admission") {
  it("rejects unresolved scalar fields at the canonical IDL boundary") {
    static const char idl[] =
        "schema Contract; message Packet { Missing field; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_false(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_null(contract);
    check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
    check_true(strstr(diagnostic.message, "unknown logical type 'Missing'") != NULL);
  }

  it("rejects missing nested generic map value types") {
    static const char idl[] =
        "schema Contract; message Packet { list<map<string,Missing>> values; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_false(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_null(contract);
    check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
    check_true(strstr(diagnostic.message, "unknown logical type 'Missing'") != NULL);
  }

  it("rejects missing map key and array element declarations") {
    static const char key_idl[] =
        "message Packet { map<Missing,uint32> values; }";
    static const char array_idl[] =
        "message Packet { Missing[4] values; }";
    const char *inputs[] = {key_idl, array_idl};
    for (size_t i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
      IdlContract *contract = NULL;
      IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
      check_false(idl_contract_parse(inputs[i], strlen(inputs[i]),
                                     &contract, &diagnostic));
      check_null(contract);
      check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
      check_true(strstr(diagnostic.message, "unknown logical type 'Missing'") != NULL);
    }
  }

  it("requires group collections to reference group declarations") {
    static const char idl[] =
        "message Item { int32 code; } "
        "message Packet { group<Item> entries; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_false(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_null(contract);
    check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
    check_true(strstr(diagnostic.message,
                      "group element must name a declared group") != NULL);
  }

  it("accepts forward references and recursive nested generic contracts") {
    static const char idl[] =
        "schema Contract; "
        "message Request { list<map<string,Response>> payloads; } "
        "message Response { Request backlink; uint32 value; } "
        "group Chunk { uint32 size; } "
        "message Package { group<Chunk> parts; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_true(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_not_null(contract);
    check_equal(diagnostic.status, IDL_OK);
    idl_contract_destroy(contract);
  }

  it("accepts valid logical varint independently of Binary admission") {
    static const char idl[] = "message Packet { varint value; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_true(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_not_null(contract);
    check_equal(diagnostic.status, IDL_OK);
    idl_contract_destroy(contract);
  }

  it("rejects user declarations shadowing scalar vocabulary") {
    static const char idl[] =
        "message int32 { uint32 field; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    check_false(idl_contract_parse(idl, sizeof(idl) - 1u, &contract, &diagnostic));
    check_null(contract);
    check_equal(diagnostic.status, IDL_SEMANTIC_ERROR);
    check_true(strstr(diagnostic.message, "shadows a logical builtin") != NULL);
  }

  it("prevents output publication on unresolved type errors") {
    static const char schema_path[] = "test_idl_missing_type.schema";
    static const char output_path[] = "test_idl_missing_type.h";
    static const char idl[] =
        "schema Contract; message Packet { Missing data; }";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
    };
    remove(schema_path);
    remove(output_path);
    check_equal(write_text_file(schema_path, idl), 0);
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(output_path));
    remove(schema_path);
    remove(output_path);
  }
}
