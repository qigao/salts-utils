#include "schema_parser_dsl.h"

#include "tinytest.h"

#include <string.h>

static Node *component_test_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || name == NULL) return NULL;
  if (parent->type == NODE_MAP) {
    for (i = 0u; i < parent->data.map.count; ++i) {
      Node *child = parent->data.map.items[i];
      if (child != NULL && child->name != NULL &&
          strcmp(child->name, name) == 0)
        return child;
    }
  }
  return NULL;
}

static const char *component_test_string(Node *parent, const char *name) {
  Node *child = component_test_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static Node *component_test_parse(const char *schema, tbe_error_t *error) {
  Node *root = create_node_map(NULL);
  if (root == NULL) return NULL;
  if (parse_schema(schema, strlen(schema), root, error) != 0) {
    node_free(root);
    return NULL;
  }
  return root;
}

spec("DataBind Component canonical IR") {
  it("composes forward-declared Services by reference without copying them") {
    static const char schema[] =
        "schema Image [version(1)];"
        "component ImageProcessor {"
        " service Codec;"
        " service Metadata;"
        "}"
        "message Request { uint32 id; }"
        "message Response { uint32 value; }"
        "service Codec { Decode: Request -> Response; }"
        "service Metadata { Inspect: Request -> Response; }"
        "message component { uint32 component; }";
    tbe_error_t error;
    Node *root = component_test_parse(schema, &error);
    Node *components;
    Node *component;
    Node *capabilities;
    Node *codec;
    Node *metadata;

    check_not_null(root);
    components = component_test_child(root, "components");
    check_not_null(components);
    check_equal(components->type, NODE_LIST);
    check_equal(components->data.list.count, (size_t)1u);

    component = components->data.list.items[0];
    check_equal(component_test_string(component, "name"), "ImageProcessor");
    check_equal(component_test_string(component, "component_name"),
                "ImageProcessor");
    check_equal(component_test_string(component, "qualified_name"),
                "Image.ImageProcessor");

    capabilities = component_test_child(component, "capabilities");
    check_not_null(capabilities);
    check_equal(capabilities->type, NODE_LIST);
    check_equal(capabilities->data.list.count, (size_t)2u);

    codec = capabilities->data.list.items[0];
    check_equal(component_test_string(codec, "kind"), "service");
    check_equal(component_test_string(codec, "name"), "Codec");
    check_equal(component_test_string(codec, "qualified_name"), "Image.Codec");
    check_null(component_test_child(codec, "operations"));

    metadata = capabilities->data.list.items[1];
    check_equal(component_test_string(metadata, "kind"), "service");
    check_equal(component_test_string(metadata, "name"), "Metadata");
    check_equal(component_test_string(metadata, "qualified_name"),
                "Image.Metadata");
    check_null(component_test_child(metadata, "operations"));

    /* The keyword remains usable as an identifier outside declaration position. */
    check_not_null(component_test_child(root, "messages"));

    node_free(root);
  }

  it("rejects duplicate Components") {
    static const char schema[] =
        "service S { Op: uint32 -> uint32; }"
        "component App { service S; }"
        "component App { service S; }";
    tbe_error_t error;
    Node *root = component_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }

  it("rejects duplicate capability references") {
    static const char schema[] =
        "service S { Op: uint32 -> uint32; }"
        "component App { service S; service S; }";
    tbe_error_t error;
    Node *root = component_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }

  it("rejects unknown Service references after the full schema is parsed") {
    static const char schema[] =
        "component App { service Missing; }"
        "service Present { Op: uint32 -> uint32; }";
    tbe_error_t error;
    Node *root = component_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }
}
