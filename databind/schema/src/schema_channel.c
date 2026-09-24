#include "schema_channel.h"

#include "schema_type_ref.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *channel_child(Node *parent, const char *name) {
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

static const char *channel_string(Node *parent, const char *name) {
    Node *child = channel_child(parent, name);
    return child != NULL && child->type == NODE_STRING
               ? child->data.string_val
               : NULL;
}

static Node *channel_list(Node *parent, const char *name) {
    Node *child = channel_child(parent, name);
    return child != NULL && child->type == NODE_LIST ? child : NULL;
}

static int channel_error(tbe_error_t *err, const char *message) {
    if (err != NULL)
        tbe_error_set(err, TBE_ERR_SEMANTIC_ERROR, -1, -1, message);
    return 0;
}

static int channel_errorf(tbe_error_t *err, const char *fmt,
                          const char *a, const char *b) {
    char message[256];
    snprintf(message, sizeof(message), fmt,
             a != NULL ? a : "<unnamed>",
             b != NULL ? b : "<unnamed>");
    return channel_error(err, message);
}

static int channel_set_string(
    Node *map, const char *name, const char *value) {
    Node *node;
    const char *current;

    if (map == NULL || map->type != NODE_MAP ||
        name == NULL || value == NULL)
        return 0;

    current = channel_string(map, name);
    if (current != NULL) return strcmp(current, value) == 0;

    node = create_node_string(name, value);
    if (node == NULL) return 0;
    if (map_add(map, node) != 0) {
        node_free(node);
        return 0;
    }
    return 1;
}

static char *channel_qualified_name(
    const char *schema_name, const char *channel_name) {
    size_t schema_len;
    size_t channel_len;
    char *qualified;

    if (channel_name == NULL || channel_name[0] == '\0') return NULL;

    if (schema_name == NULL || schema_name[0] == '\0') {
        channel_len = strlen(channel_name);
        qualified = (char *)malloc(channel_len + 1u);
        if (qualified != NULL)
            memcpy(qualified, channel_name, channel_len + 1u);
        return qualified;
    }

    schema_len = strlen(schema_name);
    channel_len = strlen(channel_name);
    if (schema_len > SIZE_MAX - channel_len - 2u) return NULL;

    qualified = (char *)malloc(schema_len + channel_len + 2u);
    if (qualified == NULL) return NULL;
    memcpy(qualified, schema_name, schema_len);
    qualified[schema_len] = '.';
    memcpy(qualified + schema_len + 1u, channel_name, channel_len + 1u);
    return qualified;
}

static int channel_name_duplicate(Node *channels, size_t index) {
    const char *name =
        channel_string(channels->data.list.items[index], "name");
    size_t i;

    for (i = 0u; i < index; ++i) {
        const char *previous =
            channel_string(channels->data.list.items[i], "name");
        if (name != NULL && previous != NULL &&
            strcmp(name, previous) == 0)
            return 1;
    }
    return 0;
}

static int channel_service_name_exists(Node *root, const char *name) {
    Node *services = channel_list(root, "services");
    size_t i;

    if (services == NULL || name == NULL) return 0;
    for (i = 0u; i < services->data.list.count; ++i) {
        const char *service_name =
            channel_string(services->data.list.items[i], "name");
        if (service_name != NULL && strcmp(service_name, name) == 0)
            return 1;
    }
    return 0;
}

int schema_validate_channels(Node *root, tbe_error_t *err) {
    Node *channels;
    Node *schema;
    const char *schema_name;
    size_t i;

    if (root == NULL || root->type != NODE_MAP)
        return channel_error(err, "Invalid Channel schema root");

    channels = channel_list(root, "channels");
    if (channels == NULL) return 1;

    schema = channel_child(root, "schema");
    schema_name = channel_string(schema, "schema_name");

    for (i = 0u; i < channels->data.list.count; ++i) {
        Node *channel = channels->data.list.items[i];
        const char *name;
        const char *message_type;
        char *qualified;

        if (channel == NULL || channel->type != NODE_MAP)
            return channel_error(err, "Malformed Channel declaration");

        name = channel_string(channel, "name");
        message_type = channel_string(channel, "message_type");

        if (name == NULL || name[0] == '\0')
            return channel_error(err, "Channel name must not be empty");

        if (channel_name_duplicate(channels, i))
            return channel_errorf(
                err, "Duplicate Channel '%s'", name, NULL);

        if (channel_service_name_exists(root, name))
            return channel_errorf(
                err,
                "Capability name '%s' is declared as both Service and Channel",
                name, NULL);

        if (!schema_type_ref_exists(root, message_type))
            return channel_errorf(
                err, "Channel '%s' has unknown message type '%s'",
                name, message_type);

        qualified = channel_qualified_name(schema_name, name);
        if (qualified == NULL ||
            !channel_set_string(channel, "qualified_name", qualified)) {
            free(qualified);
            return channel_error(
                err, "Out of memory normalizing Channel identity");
        }
        free(qualified);
    }

    return 1;
}
