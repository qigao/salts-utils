#include "schema_type_ref.h"

#include "schema_builtin_type.h"

#include <stddef.h>
#include <string.h>

static Node *type_ref_child(Node *parent, const char *name) {
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

static const char *type_ref_string(Node *parent, const char *name) {
    Node *child = type_ref_child(parent, name);
    return child != NULL && child->type == NODE_STRING
               ? child->data.string_val
               : NULL;
}

static Node *type_ref_named_record(
    Node *root, const char *list_name, const char *name) {
    Node *list = type_ref_child(root, list_name);
    size_t i;
    if (list == NULL || list->type != NODE_LIST || name == NULL) return NULL;
    for (i = 0u; i < list->data.list.count; ++i) {
        Node *record = list->data.list.items[i];
        const char *record_name = type_ref_string(record, "name");
        if (record_name != NULL && strcmp(record_name, name) == 0)
            return record;
    }
    return NULL;
}

Node *schema_type_ref_node(Node *root, const char *name) {
    static const char *const lists[] = {
        "messages", "composites", "groups", "unions", "enums"
    };
    size_t i;

    if (root == NULL || name == NULL) return NULL;

    for (i = 0u; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        Node *record = type_ref_named_record(root, lists[i], name);
        if (record != NULL) return record;
    }
    return NULL;
}

int schema_type_ref_exists(Node *root, const char *name) {
    static const char *const extended_scalars[] = {
        "string", "bytes", "uuid", "datetime", "date", "time",
        "duration", "decimal", "bigint", "money"
    };
    size_t i;

    if (name == NULL || name[0] == '\0') return 0;
    if (schema_builtin_type_find(name) != NULL) return 1;

    for (i = 0u;
         i < sizeof(extended_scalars) / sizeof(extended_scalars[0]);
         ++i)
        if (strcmp(name, extended_scalars[i]) == 0) return 1;

    return schema_type_ref_node(root, name) != NULL;
}
