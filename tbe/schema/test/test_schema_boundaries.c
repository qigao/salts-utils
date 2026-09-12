#include "schema_parser_dsl.h"
#include "tbe_error.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LONG_MAP_KEY_LEN 140u
#define LONG_MAP_VALUE_LEN 160u
#define LONG_MAP_SCHEMA_CAPACITY 512u

static Node *find_child(Node *parent, const char *name) {
  if (!parent || !name) return NULL;

  if (parent->type == NODE_MAP) {
    for (size_t i = 0; i < parent->data.map.count; ++i) {
      Node *child = parent->data.map.items[i];
      if (child && child->name && strcmp(child->name, name) == 0) return child;
    }
  } else if (parent->type == NODE_LIST) {
    for (size_t i = 0; i < parent->data.list.count; ++i) {
      Node *child = parent->data.list.items[i];
      if (child && child->name && strcmp(child->name, name) == 0) return child;
    }
  }
  return NULL;
}

static Node *first_message_field(Node *root) {
  Node *messages = find_child(root, "messages");
  Node *fields;

  if (!messages || messages->type != NODE_LIST || messages->data.list.count != 1u) return NULL;
  fields = find_child(messages->data.list.items[0], "fields");
  if (!fields || fields->type != NODE_LIST || fields->data.list.count == 0u) return NULL;
  return fields->data.list.items[0];
}

suite("schema_boundaries") {
  describe("binary layout ordering") {
    it("accepts fixed then group then variable data") {
      const char *schema =
          "group Level { uint64 price; } "
          "message Valid { uint32 seq; group<Level> levels; string symbol; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, 0);
      check_equal(err.code, TBE_OK);
      node_free(root);
    }

    it("rejects a group after variable data") {
      const char *schema =
          "group Level { uint64 price; } "
          "message Broken { string symbol; group<Level> levels; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "levels"));
      check_not_null(strstr(err.message, "order"));
      node_free(root);
    }

    it("rejects a fixed field after a group") {
      const char *schema =
          "group Level { uint64 price; } "
          "message Broken { group<Level> levels; uint32 seq; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "seq"));
      check_not_null(strstr(err.message, "order"));
      node_free(root);
    }

    it("rejects a fixed field after variable data") {
      const char *schema = "message Broken { string symbol; uint32 seq; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "seq"));
      check_not_null(strstr(err.message, "order"));
      node_free(root);
    }
  }

  describe("fixed layout numeric boundaries") {
    it("accepts a large representable fixed array length") {
      const char *schema = "message Large { uint8[2147483647] values; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, 0);
      check_equal(err.code, TBE_OK);
      node_free(root);
    }

    it("rejects a fixed array length at SIZE_MAX-scale") {
      const char *schema = "message Huge { uint8[18446744073709551615] values; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "18446744073709551615"));
      check_not_null(strstr(err.message, "values"));
      node_free(root);
    }

    it("rejects a fixed array length that overflows unsigned conversion") {
      const char *schema = "message Huge { uint8[18446744073709551616] values; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "18446744073709551616"));
      check_not_null(strstr(err.message, "values"));
      node_free(root);
    }
  }

  describe("unsupported varint boundary") {
    it("rejects scalar varint before metadata generation") {
      const char *schema = "message Bad { varint value; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "varint"));
      check_null(find_child(root, "messages"));
      node_free(root);
    }

    it("rejects varint nested in collections") {
      const char *schema = "message Bad { list<varint> values; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "varint"));
      check_null(find_child(root, "messages"));
      node_free(root);
    }

    it("rejects varint as an enum underlying type") {
      const char *schema = "enum Bad <varint> { A = 1; }";
      Node *root = create_node_map("root");
      tbe_error_t err;
      int rc = parse_schema(schema, strlen(schema), root, &err);

      check_equal(rc, -1);
      check_not_null(strstr(err.message, "varint"));
      check_null(find_child(root, "enums"));
      node_free(root);
    }
  }

  describe("map type metadata boundaries") {
    it("preserves long map key and value type names without truncation") {
      char key_type[LONG_MAP_KEY_LEN + 1u];
      char value_type[LONG_MAP_VALUE_LEN + 1u];
      char schema[LONG_MAP_SCHEMA_CAPACITY];
      Node *root = create_node_map("root");
      tbe_error_t err;
      Node *field;
      Node *key_node;
      Node *value_node;
      Node *inner_node;
      int written;
      int rc;

      memset(key_type, 'K', LONG_MAP_KEY_LEN);
      key_type[LONG_MAP_KEY_LEN] = '\0';
      memset(value_type, 'V', LONG_MAP_VALUE_LEN);
      value_type[LONG_MAP_VALUE_LEN] = '\0';
      written = snprintf(schema, sizeof(schema),
                         "message LongMap { map<%s,%s> values; }", key_type, value_type);
      check_greater(written, 0);
      check_true((size_t)written < sizeof(schema));

      rc = parse_schema(schema, strlen(schema), root, &err);
      check_equal(rc, 0);
      check_equal(err.code, TBE_OK);

      field = first_message_field(root);
      check_not_null(field);
      key_node = field ? find_child(field, "key_type") : NULL;
      value_node = field ? find_child(field, "value_type") : NULL;
      inner_node = field ? find_child(field, "inner_type") : NULL;
      check_not_null(key_node);
      check_not_null(value_node);
      check_not_null(inner_node);
      if (key_node && value_node && inner_node) {
        check_equal(key_node->data.string_val, key_type);
        check_equal(value_node->data.string_val, value_type);
        check_equal(inner_node->data.string_val, value_type);
      }

      node_free(root);
    }
  }
}
