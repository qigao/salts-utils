#include "schema_parser_dsl.h"

#include "tinytest.h"

#include <string.h>

static Node *channel_test_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const char *channel_test_string(Node *parent, const char *name) {
  Node *child = channel_test_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static Node *channel_test_named(
    Node *root, const char *list_name, const char *name) {
  Node *list = channel_test_child(root, list_name);
  size_t i;
  if (list == NULL || list->type != NODE_LIST || name == NULL) return NULL;
  for (i = 0u; i < list->data.list.count; ++i) {
    Node *item = list->data.list.items[i];
    const char *item_name = channel_test_string(item, "name");
    if (item_name != NULL && strcmp(item_name, name) == 0) return item;
  }
  return NULL;
}

static Node *channel_test_parse(const char *schema, tbe_error_t *error) {
  Node *root = create_node_map(NULL);
  if (root == NULL) return NULL;
  if (parse_schema(schema, strlen(schema), root, error) != 0) {
    node_free(root);
    return NULL;
  }
  return root;
}

spec("DataBind Channel canonical IR") {
  it("parses one-way Channel contracts and Component refs without copying subtrees") {
    static const char schema[] =
        "schema Device [version(1)];"
        "component DeviceRuntime {"
        " service Control;"
        " channel Telemetry;"
        "}"
        "channel Telemetry: TelemetryEvent;"
        "message TelemetryEvent { uint32 sequence; }"
        "message PingRequest { uint32 value; }"
        "message PingResponse { uint32 value; }"
        "service Control { Ping: PingRequest -> PingResponse; }"
        "message channel { uint32 channel; }";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    Node *channels;
    Node *channel;
    Node *component;
    Node *capabilities;
    Node *channel_ref;

    check_not_null(root);

    channels = channel_test_child(root, "channels");
    check_not_null(channels);
    check_equal(channels->type, NODE_LIST);
    check_equal(channels->data.list.count, (size_t)1u);

    channel = channels->data.list.items[0];
    check_equal(channel_test_string(channel, "name"), "Telemetry");
    check_equal(channel_test_string(channel, "channel_name"), "Telemetry");
    check_equal(channel_test_string(channel, "qualified_name"),
                "Device.Telemetry");
    check_equal(channel_test_string(channel, "message_type"),
                "TelemetryEvent");
    check_null(channel_test_child(channel, "operations"));

    component = channel_test_named(root, "components", "DeviceRuntime");
    check_not_null(component);
    capabilities = channel_test_child(component, "capabilities");
    check_not_null(capabilities);
    check_equal(capabilities->data.list.count, (size_t)2u);

    channel_ref = capabilities->data.list.items[1];
    check_equal(channel_test_string(channel_ref, "kind"), "channel");
    check_equal(channel_test_string(channel_ref, "name"), "Telemetry");
    check_equal(channel_test_string(channel_ref, "qualified_name"),
                "Device.Telemetry");
    check_null(channel_test_child(channel_ref, "message_type"));

    /* channel remains contextual where ordinary identifiers are accepted. */
    check_not_null(channel_test_named(root, "messages", "channel"));

    node_free(root);
  }

  it("accepts forward Channel payload type references") {
    static const char schema[] =
        "channel Later: LaterEvent;"
        "message LaterEvent { uint32 value; }";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    check_not_null(root);
    check_not_null(channel_test_named(root, "channels", "Later"));
    node_free(root);
  }

  it("rejects duplicate Channels") {
    static const char schema[] =
        "message Event { uint32 value; }"
        "channel Events: Event;"
        "channel Events: Event;";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }

  it("rejects unknown Channel payload types") {
    static const char schema[] =
        "channel Events: MissingEvent;";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }

  it("rejects ambiguous Service and Channel capability names") {
    static const char schema[] =
        "message Request { uint32 value; }"
        "message Response { uint32 value; }"
        "service Events { Read: Request -> Response; }"
        "channel Events: Request;";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }

  it("rejects unknown Channel Component references") {
    static const char schema[] =
        "component Runtime { channel Missing; }"
        "message Event { uint32 value; }"
        "channel Present: Event;";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }

  it("rejects duplicate Channel refs inside one Component") {
    static const char schema[] =
        "message Event { uint32 value; }"
        "channel Events: Event;"
        "component Runtime { channel Events; channel Events; }";
    tbe_error_t error;
    Node *root = channel_test_parse(schema, &error);
    check_null(root);
    check_equal(error.code, TBE_ERR_SEMANTIC_ERROR);
  }
}
