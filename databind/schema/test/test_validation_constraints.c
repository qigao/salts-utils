#include "schema_parser_dsl.h"
#include "tinytest.h"

#include <string.h>

static Node *child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || name == NULL) return NULL;
  if (parent->type == NODE_MAP) {
    for (i = 0u; i < parent->data.map.count; ++i) {
      Node *item = parent->data.map.items[i];
      if (item != NULL && item->name != NULL &&
          strcmp(item->name, name) == 0)
        return item;
    }
  } else if (parent->type == NODE_LIST) {
    for (i = 0u; i < parent->data.list.count; ++i) {
      Node *item = parent->data.list.items[i];
      if (item != NULL && item->name != NULL &&
          strcmp(item->name, name) == 0)
        return item;
    }
  }
  return NULL;
}

static Node *constraint(Node *field, size_t index) {
  Node *constraints = child(field, "constraints");
  if (constraints == NULL || constraints->type != NODE_LIST ||
      index >= constraints->data.list.count)
    return NULL;
  return constraints->data.list.items[index];
}

static Node *parse_fields(const char *schema, Node **out_root) {
  Node *root = create_node_map("root");
  Node *messages;
  Node *fields;

  if (out_root != NULL) *out_root = root;
  if (root == NULL ||
      parse_schema(schema, strlen(schema), root, NULL) != 0)
    return NULL;

  messages = child(root, "messages");
  if (messages == NULL || messages->type != NODE_LIST ||
      messages->data.list.count != 1u)
    return NULL;
  fields = child(messages->data.list.items[0], "fields");
  return fields != NULL && fields->type == NODE_LIST ? fields : NULL;
}

static void expect_rejected(const char *schema) {
  Node *root = create_node_map("root");
  check_not_null(root);
  if (root == NULL) return;
  check_not_equal(parse_schema(schema, strlen(schema), root, NULL), 0);
  node_free(root);
}

spec("DataBind validation constraint authoring") {
  it("normalizes core annotations into typed field Constraint IR") {
    static const char schema[] =
        "message User {"
        " @Min(-5) @Max(150) int32 age;"
        " @Size(min = 1, max = 100) [name(display_name)] string name;"
        " optional @Pattern(\"^[^@]+@[^@]+$\") string email;"
        "}";
    Node *root = NULL;
    Node *fields = parse_fields(schema, &root);
    Node *item;

    check_not_null(fields);
    if (fields == NULL) {
      node_free(root);
      return;
    }
    check_equal(fields->data.list.count, (size_t)3u);

    item = constraint(fields->data.list.items[0], 0u);
    check_not_null(item);
    if (item != NULL) {
      check_equal(child(item, "kind")->data.string_val, "min");
      check_equal(child(item, "value")->data.string_val, "-5");
    }
    item = constraint(fields->data.list.items[0], 1u);
    check_not_null(item);
    if (item != NULL) {
      check_equal(child(item, "kind")->data.string_val, "max");
      check_equal(child(item, "value")->data.string_val, "150");
    }

    item = constraint(fields->data.list.items[1], 0u);
    check_not_null(item);
    if (item != NULL) {
      check_equal(child(item, "kind")->data.string_val, "size");
      check_equal(child(item, "min")->data.string_val, "1");
      check_equal(child(item, "max")->data.string_val, "100");
    }
    check_not_null(child(fields->data.list.items[1], "attributes"));

    item = constraint(fields->data.list.items[2], 0u);
    check_not_null(item);
    if (item != NULL) {
      check_equal(child(item, "kind")->data.string_val, "pattern");
      check_equal(child(item, "pattern")->data.string_val,
                  "^[^@]+@[^@]+$");
    }
    check_not_null(child(fields->data.list.items[2], "is_optional"));

    node_free(root);
  }

  it("accepts one-sided Size bounds") {
    static const char schema[] =
        "message Bounds {"
        " @Size(min = 1) string lower;"
        " @Size(max = 32) string upper;"
        "}";
    Node *root = NULL;
    Node *fields = parse_fields(schema, &root);
    Node *item;

    check_not_null(fields);
    if (fields != NULL) {
      item = constraint(fields->data.list.items[0], 0u);
      check_not_null(item);
      if (item != NULL) {
        check_equal(child(item, "min")->data.string_val, "1");
        check_null(child(item, "max"));
      }
      item = constraint(fields->data.list.items[1], 0u);
      check_not_null(item);
      if (item != NULL) {
        check_null(child(item, "min"));
        check_equal(child(item, "max")->data.string_val, "32");
      }
    }
    node_free(root);
  }

  it("rejects duplicate singleton constraints") {
    expect_rejected(
        "message Invalid { @Min(1) @Min(2) int32 value; }");
    expect_rejected(
        "message Invalid { @Pattern(\"a\") @Pattern(\"b\") string value; }");
  }

  it("rejects annotations outside the minimal vocabulary or argument shape") {
    expect_rejected(
        "message Invalid { @Email(\"x\") string value; }");
    expect_rejected(
        "message Invalid { @Pattern(1) string value; }");
    expect_rejected(
        "message Invalid { @Min(min = 1) int32 value; }");
    expect_rejected(
        "message Invalid { @Size(foo = 1) string value; }");
  }

  it("rejects invalid Size bounds") {
    expect_rejected(
        "message Invalid { @Size(min = 10, max = 2) string value; }");
    expect_rejected(
        "message Invalid { @Size(min = -1) string value; }");
    expect_rejected(
        "message Invalid { @Size(min = 1, min = 2) string value; }");
  }

  it("keeps modifier order canonical") {
    expect_rejected(
        "message Invalid { @Pattern(\"x\") optional string value; }");
    {
      static const char schema[] =
          "message Valid { optional @Pattern(\"x\") string value; }";
      Node *root = NULL;
      Node *fields = parse_fields(schema, &root);
      check_not_null(fields);
      node_free(root);
    }
  }
}
