#include "schema_component.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *component_child(Node *parent, const char *name) {
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

static const char *component_string(Node *parent, const char *name) {
    Node *child = component_child(parent, name);
    return child != NULL && child->type == NODE_STRING
               ? child->data.string_val
               : NULL;
}

static Node *component_list(Node *parent, const char *name) {
    Node *child = component_child(parent, name);
    return child != NULL && child->type == NODE_LIST ? child : NULL;
}

static int component_error(tbe_error_t *err, const char *message) {
    if (err != NULL)
        tbe_error_set(err, TBE_ERR_SEMANTIC_ERROR, -1, -1, message);
    return 0;
}

static int component_errorf(tbe_error_t *err, const char *fmt,
                            const char *a, const char *b) {
    char message[256];
    snprintf(message, sizeof(message), fmt,
             a != NULL ? a : "<unnamed>",
             b != NULL ? b : "<unnamed>");
    return component_error(err, message);
}

static char *component_qualified_name(const char *schema_name,
                                      const char *name) {
    size_t schema_len;
    size_t name_len;
    char *qualified;

    if (name == NULL || name[0] == '\0') return NULL;
    if (schema_name == NULL || schema_name[0] == '\0') {
        name_len = strlen(name);
        qualified = (char *)malloc(name_len + 1u);
        if (qualified != NULL) memcpy(qualified, name, name_len + 1u);
        return qualified;
    }

    schema_len = strlen(schema_name);
    name_len = strlen(name);
    if (schema_len > SIZE_MAX - name_len - 2u) return NULL;

    qualified = (char *)malloc(schema_len + name_len + 2u);
    if (qualified == NULL) return NULL;
    memcpy(qualified, schema_name, schema_len);
    qualified[schema_len] = '.';
    memcpy(qualified + schema_len + 1u, name, name_len + 1u);
    return qualified;
}

static int component_set_string(Node *map, const char *key,
                                const char *value) {
    const char *current;
    Node *node;

    if (map == NULL || map->type != NODE_MAP ||
        key == NULL || value == NULL)
        return 0;

    current = component_string(map, key);
    if (current != NULL) return strcmp(current, value) == 0;

    node = create_node_string(key, value);
    if (node == NULL) return 0;
    if (map_add(map, node) != 0) {
        node_free(node);
        return 0;
    }
    return 1;
}

static Node *component_find_service(Node *root, const char *name) {
    Node *services = component_list(root, "services");
    size_t i;

    if (services == NULL || name == NULL) return NULL;
    for (i = 0u; i < services->data.list.count; ++i) {
        Node *service = services->data.list.items[i];
        const char *candidate = component_string(service, "name");
        if (candidate != NULL && strcmp(candidate, name) == 0)
            return service;
    }
    return NULL;
}

static int component_name_duplicate(Node *components, size_t index) {
    const char *name = component_string(
        components->data.list.items[index], "name");
    size_t i;
    for (i = 0u; i < index; ++i) {
        const char *previous = component_string(
            components->data.list.items[i], "name");
        if (name != NULL && previous != NULL &&
            strcmp(name, previous) == 0)
            return 1;
    }
    return 0;
}

static int component_capability_duplicate(Node *capabilities, size_t index) {
    Node *capability = capabilities->data.list.items[index];
    const char *kind = component_string(capability, "kind");
    const char *name = component_string(capability, "name");
    size_t i;

    for (i = 0u; i < index; ++i) {
        Node *previous = capabilities->data.list.items[i];
        const char *previous_kind = component_string(previous, "kind");
        const char *previous_name = component_string(previous, "name");
        if (kind != NULL && name != NULL &&
            previous_kind != NULL && previous_name != NULL &&
            strcmp(kind, previous_kind) == 0 &&
            strcmp(name, previous_name) == 0)
            return 1;
    }
    return 0;
}

int schema_validate_components(Node *root, tbe_error_t *err) {
    Node *components;
    Node *schema;
    const char *schema_name;
    size_t i;

    if (root == NULL || root->type != NODE_MAP)
        return component_error(err, "Invalid Component schema root");

    components = component_list(root, "components");
    if (components == NULL) return 1;

    schema = component_child(root, "schema");
    schema_name = component_string(schema, "schema_name");

    for (i = 0u; i < components->data.list.count; ++i) {
        Node *component = components->data.list.items[i];
        Node *capabilities;
        const char *component_name;
        char *qualified_component;
        size_t j;

        if (component == NULL || component->type != NODE_MAP)
            return component_error(err, "Malformed Component declaration");

        component_name = component_string(component, "name");
        if (component_name == NULL || component_name[0] == '\0')
            return component_error(err, "Component name must not be empty");

        if (component_name_duplicate(components, i))
            return component_errorf(
                err, "Duplicate Component '%s'", component_name, NULL);

        qualified_component =
            component_qualified_name(schema_name, component_name);
        if (qualified_component == NULL ||
            !component_set_string(
                component, "qualified_name", qualified_component)) {
            free(qualified_component);
            return component_error(
                err, "Out of memory normalizing Component identity");
        }
        free(qualified_component);

        capabilities = component_list(component, "capabilities");
        if (capabilities == NULL)
            return component_errorf(
                err, "Component '%s' has malformed capability list",
                component_name, NULL);

        for (j = 0u; j < capabilities->data.list.count; ++j) {
            Node *capability = capabilities->data.list.items[j];
            const char *kind;
            const char *name;
            char *qualified_capability;

            if (capability == NULL || capability->type != NODE_MAP)
                return component_errorf(
                    err, "Component '%s' has malformed capability reference",
                    component_name, NULL);

            kind = component_string(capability, "kind");
            name = component_string(capability, "name");
            if (kind == NULL || name == NULL || name[0] == '\0')
                return component_errorf(
                    err, "Component '%s' has incomplete capability reference",
                    component_name, NULL);

            if (strcmp(kind, "service") != 0)
                return component_errorf(
                    err, "Component '%s' has unsupported capability kind '%s'",
                    component_name, kind);

            if (component_capability_duplicate(capabilities, j))
                return component_errorf(
                    err, "Component '%s' repeats capability '%s'",
                    component_name, name);

            if (component_find_service(root, name) == NULL)
                return component_errorf(
                    err, "Component '%s' references unknown Service '%s'",
                    component_name, name);

            qualified_capability =
                component_qualified_name(schema_name, name);
            if (qualified_capability == NULL ||
                !component_set_string(
                    capability, "qualified_name", qualified_capability)) {
                free(qualified_capability);
                return component_error(
                    err, "Out of memory normalizing Component capability");
            }
            free(qualified_capability);
        }
    }

    return 1;
}
