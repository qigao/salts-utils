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

static int test_render_file_contains(const char *path, const char *fragment) {
  FILE *file = fopen(path, "rb");
  char content[8192];
  size_t length;
  if (file == NULL || fragment == NULL) {
    if (file != NULL) fclose(file);
    return 0;
  }
  length = fread(content, 1u, sizeof(content) - 1u, file);
  if (ferror(file) || !feof(file)) {
    fclose(file);
    return 0;
  }
  content[length] = '\0';
  fclose(file);
  return strstr(content, fragment) != NULL;
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  fclose(file);
  return 1;
}

spec("tbe_compiler_schema_boundary") {
  it("rejects varint in format/native backends lacking a projection") {
    static const int64_t languages[] = {
        TBE_COMPILER_LANG_C,
        TBE_COMPILER_LANG_RUST,
        TBE_COMPILER_LANG_CPP,
        TBE_COMPILER_LANG_GO,
        TBE_COMPILER_LANG_SQLITE,
        TBE_COMPILER_LANG_POSTGRESQL,
    };
    static const char *const output_paths[] = {
        "test_varint_boundary_c.out",
        "test_varint_boundary_rust.out",
        "test_varint_boundary_cpp.out",
        "test_varint_boundary_go.out",
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

spec("typescript_contract_render_ir") {
  it("generates exact logical types when Binary field ordering is invalid") {
    static const char schema_path[] = "ts_contract_no_binary.schema";
    static const char output_path[] = "ts_contract_no_binary.ts";
    static const char binary_path[] = "ts_contract_no_binary.h";
    static const char schema[] =
        "schema Render; "
        "enum Status <uint8> { Idle = 0; Ready = 1; } "
        "message Record { uint32 id; } "
        "message Envelope { "
        "string description; uint32 count; "
        "optional nullable list<map<string,Record>> nested; "
        "varint counter; int64 long_total; uint64 big_total; "
        "bytes(16) digest; Status status; }";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
    };
    remove(schema_path);
    remove(output_path);
    remove(binary_path);
    check_equal(write_text_file(schema_path, schema), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(output_path, "export enum Status"));
    check_true(test_render_file_contains(output_path, "description: string;"));
    check_true(test_render_file_contains(output_path, "count: number;"));
    check_true(test_render_file_contains(output_path,
                "nested?: Array<Map<string, Record>> | null;"));
    check_true(test_render_file_contains(output_path, "counter: bigint;"));
    check_true(test_render_file_contains(output_path, "long_total: bigint;"));
    check_true(test_render_file_contains(output_path, "big_total: bigint;"));
    check_true(test_render_file_contains(output_path, "digest: Uint8Array;"));
    check_true(test_render_file_contains(output_path, "status: Status;"));

    options.lang_enum = TBE_COMPILER_LANG_C;
    options.output_path = binary_path;
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(binary_path));
    remove(schema_path);
    remove(output_path);
    remove(binary_path);
  }

  it("separates pure Binary field-order rules from source-language admission") {
    static const char schema_path[] = "ts_binary_field_order.schema";
    static const char ts_path[] = "ts_binary_field_order.ts";
    static const char c_path[] = "ts_binary_field_order.h";
    static const char schema[] =
        "message Event { string payload; uint32 sequence; }";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = ts_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
    };
    remove(schema_path);
    remove(ts_path);
    remove(c_path);
    check_equal(write_text_file(schema_path, schema), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(ts_path, "payload: string;"));
    check_true(test_render_file_contains(ts_path, "sequence: number;"));

    options.lang_enum = TBE_COMPILER_LANG_C;
    options.output_path = c_path;
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(c_path));
    remove(schema_path);
    remove(ts_path);
    remove(c_path);
  }

  it("honors an explicit Mustache template using only typed presentation data") {
    static const char schema_path[] = "ts_contract_custom.schema";
    static const char template_path[] = "ts_contract_custom.mustache";
    static const char output_path[] = "ts_contract_custom.ts";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .template_path = template_path,
        .output_path = output_path,
        .lang_enum = TBE_COMPILER_LANG_TS,
    };
    remove(schema_path);
    remove(template_path);
    remove(output_path);
    check_equal(write_text_file(schema_path,
        "schema Custom; message Event { varint id; optional bool enabled; }"), 0);
    check_equal(write_text_file(template_path,
        "{{schema.schema_name}}:{{#messages}}{{name}}:{{#fields}}"
        "{{name}}={{ts_type}};{{/fields}}{{/messages}}"), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(
        output_path, "Custom:Event:id=bigint;enabled=boolean;"));
    remove(schema_path);
    remove(template_path);
    remove(output_path);
  }

  it("fails closed when a logical builtin has no TS representation") {
    static const char schema_path[] = "ts_contract_domain.schema";
    static const char output_path[] = "ts_contract_domain.ts";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
    };
    remove(schema_path);
    remove(output_path);
    check_equal(write_text_file(schema_path,
        "message Event { datetime observed; }"), 0);
    check_equal(write_text_file(output_path, "unchanged-output"), 0);
    check_not_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(output_path, "unchanged-output"));
    remove(schema_path);
    remove(output_path);
  }

  it("rejects unsupported union declarations without silently omitting them") {
    static const char schema_path[] = "ts_contract_union.schema";
    static const char output_path[] = "ts_contract_union.ts";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
    };
    remove(schema_path);
    remove(output_path);
    check_equal(write_text_file(schema_path,
        "message Event { uint32 id; } "
        "union Message { Event payload; }"), 0);
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(output_path));
    remove(schema_path);
    remove(output_path);
  }
}


spec("python_contract_render_ir") {
  it("emits Binary-independent dataclasses with exact nested logical types") {
    static const char schema_path[] = "python_contract_no_binary.schema";
    static const char output_path[] = "python_contract_no_binary.py";
    static const char binary_path[] = "python_contract_no_binary.h";
    static const char schema[] =
        "schema Python; "
        "enum Status <uint16> { Idle = 0; Ready = 1; } "
        "flags Permission <uint8> { Read = 1; Write = 2; } "
        "message Envelope { "
        "string description; uint32 count; "
        "list<map<string,Item>> nested; "
        "set<uint32> ids; "
        "nullable string maybe_title; "
        "varint clicks; int64 signed_total; uint64 unsigned_total; "
        "bytes(16) digest; Status state; Permission rights; } "
        "message Item { uint32 code; }";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_PYTHON,
    };

    remove(schema_path);
    remove(output_path);
    remove(binary_path);
    check_equal(write_text_file(schema_path, schema), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(
        output_path, "from __future__ import annotations"));
    check_true(test_render_file_contains(output_path, "class Status(IntEnum):"));
    check_true(test_render_file_contains(output_path, "class Permission(IntFlag):"));
    check_true(test_render_file_contains(output_path, "description: str"));
    check_true(test_render_file_contains(output_path, "count: int"));
    check_true(test_render_file_contains(
        output_path, "nested: list[dict[str, Item]]"));
    check_true(test_render_file_contains(output_path, "ids: set[int]"));
    check_true(test_render_file_contains(
        output_path, "maybe_title: str | None"));
    check_true(test_render_file_contains(output_path, "clicks: int"));
    check_true(test_render_file_contains(output_path, "signed_total: int"));
    check_true(test_render_file_contains(output_path, "unsigned_total: int"));
    check_true(test_render_file_contains(output_path, "digest: bytes"));
    check_true(test_render_file_contains(output_path, "state: Status"));
    check_true(test_render_file_contains(output_path, "rights: Permission"));
    check_false(test_render_file_contains(output_path, "# size:"));

    options.lang_enum = TBE_COMPILER_LANG_C;
    options.output_path = binary_path;
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(binary_path));
    remove(schema_path);
    remove(output_path);
    remove(binary_path);
  }

  it("supports custom Python templates over typed presentation facts") {
    static const char schema_path[] = "python_contract_custom.schema";
    static const char template_path[] = "python_contract_custom.mustache";
    static const char output_path[] = "python_contract_custom.out";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .template_path = template_path,
        .output_path = output_path,
        .lang_enum = TBE_COMPILER_LANG_PYTHON,
    };
    remove(schema_path);
    remove(template_path);
    remove(output_path);
    check_equal(write_text_file(schema_path,
        "schema Customer; message Event { varint id; nullable string note; }"), 0);
    check_equal(write_text_file(template_path,
        "{{schema.schema_name}}:{{#messages}}{{name}}:{{#fields}}"
        "{{name}}={{python_type}};{{/fields}}{{/messages}}"), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(
        output_path, "Customer:Event:id=int;note=str | None;"));
    remove(schema_path);
    remove(template_path);
    remove(output_path);
  }

  it("rejects omitted-presence and unimplemented default semantics atomically") {
    static const char schema_path[] = "python_contract_presence.schema";
    static const char output_path[] = "python_contract_presence.py";
    static const char *const bad_schemas[] = {
        "message Event { optional uint32 value; }",
        "message Event { uint32 value default 7; }",
        "message Event { datetime stamp; }",
        "message Event { uint32 id; } union Choice { Event value; }",
    };
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_PYTHON,
    };
    for (size_t i = 0u; i < sizeof(bad_schemas) / sizeof(bad_schemas[0]); ++i) {
      remove(schema_path);
      remove(output_path);
      check_equal(write_text_file(schema_path, bad_schemas[i]), 0);
      check_equal(write_text_file(output_path, "unchanged-output"), 0);
      check_not_equal(tbe_compiler_run(&options), 0);
      check_true(test_render_file_contains(output_path, "unchanged-output"));
    }
    remove(schema_path);
    remove(output_path);
  }
}


spec("go_contract_render_ir") {
  it("admits Go source for a Binary-incompatible logical contract") {
    static const char schema_path[] = "go_contract_no_binary.schema";
    static const char go_path[] = "go_contract_no_binary.go";
    static const char c_path[] = "go_contract_no_binary.h";
    static const char source[] =
        "schema Ledger; "
        "enum Mode <uint8> { Inactive = 0; Active = 1; } "
        "message Row { uint32 id; } "
        "message Event { "
        "string payload; uint32 sequence; "
        "list<map<string,Row>> records; "
        "map<string,uint64> totals; set<uint32> tags; "
        "uint8[8] digest; bytes(16) token; "
        "nullable string note; int64 signed_total; uint64 unsigned_total; "
        "Mode mode; group<Parts> parts; } "
        "group Parts { uint32 count; }";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = go_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_GO,
    };
    remove(schema_path);
    remove(go_path);
    remove(c_path);
    check_equal(write_text_file(schema_path, source), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(go_path, "package ledger"));
    check_true(test_render_file_contains(go_path, "type Mode uint8"));
    check_true(test_render_file_contains(go_path, "Mode_Active Mode = 1"));
    check_true(test_render_file_contains(go_path, "Payload string"));
    check_true(test_render_file_contains(go_path, "Sequence uint32"));
    check_true(test_render_file_contains(go_path, "Records []map[string]Row"));
    check_true(test_render_file_contains(go_path, "Totals map[string]uint64"));
    check_true(test_render_file_contains(go_path, "Tags map[uint32]struct{}"));
    check_true(test_render_file_contains(go_path, "Digest [8]uint8"));
    check_true(test_render_file_contains(go_path, "Token [16]byte"));
    check_true(test_render_file_contains(go_path, "Note *string"));
    check_true(test_render_file_contains(go_path, "SignedTotal int64"));
    check_true(test_render_file_contains(go_path, "UnsignedTotal uint64"));
    check_true(test_render_file_contains(go_path, "Mode Mode"));
    check_true(test_render_file_contains(go_path, "Parts []Parts"));

    options.lang_enum = TBE_COMPILER_LANG_C;
    options.output_path = c_path;
    check_not_equal(tbe_compiler_run(&options), 0);
    check_false(file_exists(c_path));
    remove(schema_path);
    remove(go_path);
    remove(c_path);
  }

  it("renders Go custom Mustache templates from typed presentation data") {
    static const char schema_path[] = "go_contract_custom.schema";
    static const char template_path[] = "go_contract_custom.mustache";
    static const char output_path[] = "go_contract_custom.out";
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .template_path = template_path,
        .output_path = output_path,
        .lang_enum = TBE_COMPILER_LANG_GO,
    };
    remove(schema_path);
    remove(template_path);
    remove(output_path);
    check_equal(write_text_file(schema_path,
        "schema Ledger; message Log { nullable uint64 value; }"), 0);
    check_equal(write_text_file(template_path,
        "{{schema.go_package_name}}:{{#messages}}{{name}}:{{#fields}}"
        "{{go_name}}={{go_type}};{{/fields}}{{/messages}}"), 0);
    check_equal(tbe_compiler_run(&options), 0);
    check_true(test_render_file_contains(output_path,
                                         "ledger:Log:Value=*uint64;"));
    remove(schema_path);
    remove(template_path);
    remove(output_path);
  }

  it("rejects unsupported map/set keys and ambiguous exported field names") {
    static const char schema_path[] = "go_contract_bad_keys.schema";
    static const char output_path[] = "go_contract_bad_keys.go";
    static const char *const rejected[] = {
        "message Bad { map<bytes,uint32> value; }",
        "message Bad { set<list<uint32>> value; }",
        "message Bad { list<map<bytes,uint32>> value; }",
        "message Bad { uint32 foo_bar; uint64 fooBar; }",
        "message Bad { varint value; }",
        "message Bad { datetime observed; }",
    };
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_GO,
    };
    for (size_t i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      remove(schema_path);
      remove(output_path);
      check_equal(write_text_file(schema_path, rejected[i]), 0);
      check_equal(write_text_file(output_path, "caller-output-sentinel"), 0);
      check_not_equal(tbe_compiler_run(&options), 0);
      check_true(test_render_file_contains(output_path, "caller-output-sentinel"));
    }
    remove(schema_path);
    remove(output_path);
  }

  it("rejects unknown omission/default semantics and symbolic fixed widths") {
    static const char schema_path[] = "go_contract_presence.schema";
    static const char output_path[] = "go_contract_presence.go";
    static const char *const rejected[] = {
        "message Bad { optional uint32 value; }",
        "message Bad { uint32 value default 3; }",
        "message Bad { uint16 n; uint8[n] digest; }",
        "message Bad { uint32 id; } union Choice { Bad value; }",
    };
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output_path,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_GO,
    };
    for (size_t i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      remove(schema_path);
      remove(output_path);
      check_equal(write_text_file(schema_path, rejected[i]), 0);
      check_equal(write_text_file(output_path, "caller-output-sentinel"), 0);
      check_not_equal(tbe_compiler_run(&options), 0);
      check_true(test_render_file_contains(output_path, "caller-output-sentinel"));
    }
    remove(schema_path);
    remove(output_path);
  }
}
