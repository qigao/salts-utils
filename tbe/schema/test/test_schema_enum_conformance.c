#include "schema_parser_dsl.h"
#include "schema_cmeta.h"
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

/* Independent schema literals lock wire-domain limits, while descriptor checks
 * lock the canonical native identity that production normalization consumes. */
typedef struct enum_cmeta_case {
  const char *aliases[4];
  const char *canonical;
  cmeta_data_kind kind;
  unsigned bits;
  const char *minimum;
  const char *maximum;
  const char *below;
  const char *above;
} enum_cmeta_case_t;

static const enum_cmeta_case_t enum_cmeta_cases[] = {
    {{"int8_t", "int8", "i8", NULL}, "int8", CMETA_DATA_SINT, 8u,
     "-128", "127", "-129", "128"},
    {{"uint8_t", "uint8", "u8", "byte"}, "uint8", CMETA_DATA_UINT, 8u,
     "0", "255", "-1", "256"},
    {{"int16_t", "int16", "i16", NULL}, "int16", CMETA_DATA_SINT, 16u,
     "-32768", "32767", "-32769", "32768"},
    {{"uint16_t", "uint16", "u16", NULL}, "uint16", CMETA_DATA_UINT, 16u,
     "0", "65535", "-1", "65536"},
    {{"int32_t", "int32", "i32", NULL}, "int32", CMETA_DATA_SINT, 32u,
     "-2147483648", "2147483647", "-2147483649", "2147483648"},
    {{"uint32_t", "uint32", "u32", NULL}, "uint32", CMETA_DATA_UINT, 32u,
     "0", "4294967295", "-1", "4294967296"},
    {{"int64_t", "int64", "i64", NULL}, "int64", CMETA_DATA_SINT, 64u,
     "-9223372036854775808", "9223372036854775807",
     "-9223372036854775809", "9223372036854775808"},
    {{"uint64_t", "uint64", "u64", NULL}, "uint64", CMETA_DATA_UINT, 64u,
     "0", "18446744073709551615", "-1", "18446744073709551616"},
};

enum { ENUM_TEST_SOURCE_CAPACITY = 256 };

static void enum_check_cmeta_bounds(const enum_cmeta_case_t *item,
                                    const char *alias, const char *family) {
  char source[ENUM_TEST_SOURCE_CAPACITY];
  const cmeta_data_desc *data = schema_cmeta_builtin_data(alias);
  const cmeta_data_desc *canonical = schema_cmeta_builtin_data(item->canonical);
  cmeta_data_kind kind = CMETA_DATA_MAP;
  check_not_null(data);
  check_not_null(canonical);
  if (!data || !canonical) return;
  check_true(cmeta_data_desc_valid(data));
  check_true(schema_cmeta_data_kind(alias, &kind));
  check_equal(kind, item->kind);
  check_equal(data->kind, item->kind);
  check_not_null(data->shape);
  if (!data->shape) return;
  check_equal(((const cmeta_data_integer_shape *)data->shape)->bits, item->bits);
  check_true(cmeta_type_identity_equal(data->storage_type->identity,
                                       canonical->storage_type->identity));

  int length = snprintf(source, sizeof(source),
      "%s Bounds <%s> { Max=%s; Min=%s; } message Use { Bounds value; }",
      family, alias, item->maximum, item->minimum);
  check(length > 0 && (size_t)length < sizeof(source));
  if (length <= 0 || (size_t)length >= sizeof(source)) return;
  Node *root = create_node_map(NULL);
  tbe_error_t error;
  check_not_null(root);
  if (!root) return;
  int result = parse_schema(source, (size_t)length, root, &error);
  check_equal(result, 0);
  if (result == 0) {
    Node *enums = enum_child(root, "enums");
    check(enums && enums->type == NODE_LIST && enums->data.list.count == 1u);
    if (enums && enums->type == NODE_LIST && enums->data.list.count == 1u) {
      Node *owner = enums->data.list.items[0];
      Node *items = enum_child(owner, "items");
      check_equal(enum_text(owner, "underlying_type"), item->canonical);
      check_equal(enum_text(owner, "min_value"), "Bounds_Min");
      check_equal(enum_text(owner, "max_value"), "Bounds_Max");
      check_equal(enum_child(owner, "is_flags") != NULL, strcmp(family, "flags") == 0);
      check(items && items->type == NODE_LIST && items->data.list.count == 2u);
      if (items && items->type == NODE_LIST && items->data.list.count == 2u) {
        check_equal(enum_text(items->data.list.items[0], "name"), "Max");
        check_equal(enum_text(items->data.list.items[0], "value"), item->maximum);
        check_equal(enum_text(items->data.list.items[1], "name"), "Min");
        check_equal(enum_text(items->data.list.items[1], "value"), item->minimum);
      }
    }
  }
  node_free(root);
}

spec("production enum normalization agrees with canonical CMeta integers") {
  it("preserves exact bounds and declaration order for every integer alias") {
    static const char *families[] = {"enum", "flags"};
    for (size_t i = 0; i < sizeof(enum_cmeta_cases) / sizeof(enum_cmeta_cases[0]); ++i) {
      const enum_cmeta_case_t *item = &enum_cmeta_cases[i];
      for (size_t a = 0; a < sizeof(item->aliases) / sizeof(item->aliases[0]); ++a) {
        if (!item->aliases[a]) continue;
        for (size_t f = 0; f < sizeof(families) / sizeof(families[0]); ++f) {
          info("family=%s alias=%s", families[f], item->aliases[a]);
          enum_check_cmeta_bounds(item, item->aliases[a], families[f]);
        }
      }
    }
  }

  it("rejects each alias immediately outside its integer domain without publishing") {
    char source[ENUM_TEST_SOURCE_CAPACITY];
    for (size_t i = 0; i < sizeof(enum_cmeta_cases) / sizeof(enum_cmeta_cases[0]); ++i) {
      const enum_cmeta_case_t *item = &enum_cmeta_cases[i];
      const char *invalid[] = {item->below, item->above};
      for (size_t a = 0; a < sizeof(item->aliases) / sizeof(item->aliases[0]); ++a) {
        if (!item->aliases[a]) continue;
        for (size_t n = 0; n < sizeof(invalid) / sizeof(invalid[0]); ++n) {
          int length = snprintf(source, sizeof(source), "enum Bad <%s> { Value=%s; }",
                                item->aliases[a], invalid[n]);
          check(length > 0 && (size_t)length < sizeof(source));
          if (length <= 0 || (size_t)length >= sizeof(source)) continue;
          info("schema=%s", source);
          enum_reject_unchanged(source);
        }
      }
    }
  }

  it("does not treat noninteger canonical descriptors or buffer semantics as integer storage") {
    static const char *types[] = {"bool", "float", "f32", "double", "f64",
                                  "uuid", "string", "bytes", "Unknown"};
    char source[ENUM_TEST_SOURCE_CAPACITY];
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
      int length = snprintf(source, sizeof(source), "enum Bad <%s> { Value=1; }", types[i]);
      check(length > 0 && (size_t)length < sizeof(source));
      if (length <= 0 || (size_t)length >= sizeof(source)) continue;
      info("schema=%s", source);
      enum_reject_unchanged(source);
    }
  }

  it("keeps a previously published enum graph intact after a later enum fails") {
    const char *baseline = "enum Keep <i16> { Low=-2; High=5; } message Use { Keep value; }";
    const char *invalid = "enum Good <u8> { Value=1; } enum Bad <i8> { Value=128; }";
    Node *root = create_node_map(NULL);
    tbe_error_t error;
    check_not_null(root);
    if (!root) return;
    int result = parse_schema(baseline, strlen(baseline), root, &error);
    check_equal(result, 0);
    if (result == 0) {
      Node *enums = enum_child(root, "enums");
      Node *messages = enum_child(root, "messages");
      size_t count = root->data.map.count;
      check_equal(parse_schema(invalid, strlen(invalid), root, &error), -1);
      check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
      check_contains(error.message, "Bad");
      check_equal(root->data.map.count, count);
      check(enum_child(root, "enums") == enums);
      check(enum_child(root, "messages") == messages);
      check(enums && enums->type == NODE_LIST && enums->data.list.count == 1u);
      if (enums && enums->type == NODE_LIST && enums->data.list.count == 1u) {
        Node *owner = enums->data.list.items[0];
        check_equal(enum_text(owner, "enum_name"), "Keep");
        check_equal(enum_text(owner, "underlying_type"), "int16");
        check_equal(enum_text(owner, "min_value"), "Keep_Low");
        check_equal(enum_text(owner, "max_value"), "Keep_High");
      }
    }
    node_free(root);
  }
}
