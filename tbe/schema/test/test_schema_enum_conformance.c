#include "schema_parser_dsl.h"
#include "tinytest.h"
#include <stdio.h>
#include <string.h>

static Node *enum_child(Node *map, const char *name) {
  if (!map || map->type != NODE_MAP) return NULL;
  for (size_t i = 0; i < map->data.map.count; ++i) {
    Node *node = map->data.map.items[i];
    if (node->name && strcmp(node->name, name) == 0) return node;
  }
  return NULL;
}

static const char *enum_text(Node *map, const char *name) {
  Node *node = enum_child(map, name);
  return node && node->type == NODE_STRING ? node->data.string_val : NULL;
}

static void enum_reject_unchanged(const char *schema) {
  const char *baseline = "message Unchanged { uint32 id; }";
  Node *root = create_node_map(NULL);
  tbe_error_t error;
  check_not_null(root);
  if (!root) return;
  check_equal(parse_schema(baseline, strlen(baseline), root, &error), 0);
  Node *messages = enum_child(root, "messages");
  check_equal(parse_schema(schema, strlen(schema), root, &error), -1);
  check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  check_contains(error.message, "enum");
  check(enum_child(root, "messages") == messages);
  Node *enums = enum_child(root, "enums");
  check_not_null(enums);
  if (enums) check_equal(enums->data.list.count, 0u);
  node_free(root);
}

spec("enum storage and transactional schema validation") {
  it("publishes one canonical storage type for defaults and integer aliases") {
    const char *source =
        "enum Default { Zero; } flags DefaultFlags { One; }"
        "enum Signed <i8> { Min=-128; Next; Max=127; }"
        "enum Unsigned <uint64_t> { Max=18446744073709551615; }"
        "enum Decimal <u16> { Eight=008; Nine; }";
    const char *expected[] = {"int32", "uint32", "int8", "uint64", "uint16"};
    Node *root = create_node_map(NULL);
    tbe_error_t error;
    check_not_null(root);
    if (!root) return;
    check_equal(parse_schema(source, strlen(source), root, &error), 0);
    Node *enums = enum_child(root, "enums");
    check_not_null(enums);
    if (enums && enums->data.list.count == 5u) {
      for (size_t i = 0; i < 5u; ++i)
        check_equal(enum_text(enums->data.list.items[i], "underlying_type"), expected[i]);
      Node *signed_enum = enums->data.list.items[2];
      check_equal(enum_text(signed_enum, "min_value"), "Signed_Min");
      check_equal(enum_text(signed_enum, "max_value"), "Signed_Max");
      Node *items = enum_child(signed_enum, "items");
      check_equal(enum_text(items->data.list.items[1], "value"), "-127");
      items = enum_child(enums->data.list.items[4], "items");
      check_equal(enum_text(items->data.list.items[0], "value"), "8");
      check_equal(enum_text(items->data.list.items[1], "value"), "9");
    } else {
      check(0);
    }
    node_free(root);
  }

  it("rejects aliases range errors and unsupported storage before replacing a root") {
    const char *invalid[] = {
        "enum Bad <uint8> { A=1; B=0x01; }",
        "flags Bad <uint8> { A=1; B=01; }",
        "enum Bad <int8> { A=-128; B=-129; }",
        "enum Bad <int8> { A=128; }",
        "enum Bad <uint8> { A=-1; }",
        "enum Bad <uint64> { A=18446744073709551616; }",
        "enum Bad <int64> { A=-9223372036854775809; }",
        "enum Bad <float> { A=1; }",
        "flags Bad <bool> { A=1; }",
        "enum Bad { }",
        "flags Bad { }",
        "enum Bad <uint8> { A=1; A=2; }",
        "enum Bad <int32> { A=1.5; }",
        "enum Bad <int32> { A=1e3; }",
        "enum Bad <uint8> { A=255; Next; }",
        "flags Bad <uint64> { A=9223372036854775808; Next; }",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      info("schema=%s", invalid[i]);
      enum_reject_unchanged(invalid[i]);
    }
  }
}
