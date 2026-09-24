#include "schema_service.h"

#include "schema_type_ref.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *service_find_child(Node *parent, const char *name) {
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

static const char *service_string(Node *parent, const char *name) {
    Node *child = service_find_child(parent, name);
    return child != NULL && child->type == NODE_STRING
               ? child->data.string_val
               : NULL;
}

static int service_flag(Node *node, const char *name) {
    const char *value = service_string(node, name);
    return value != NULL && strcmp(value, "1") == 0;
}

static Node *service_attributes(Node *node) {
    Node *attrs = service_find_child(node, "attributes");
    return attrs != NULL && attrs->type == NODE_LIST ? attrs : NULL;
}

static const char *service_attribute_name(Node *attr) {
    return service_string(attr, "name");
}

static int service_attribute_is_bare(Node *attr) {
    return service_flag(attr, "bare");
}

static Node *service_attribute_values(Node *attr) {
    Node *values = service_find_child(attr, "values");
    return values != NULL && values->type == NODE_LIST ? values : NULL;
}

static size_t service_attribute_value_count(Node *attr) {
    Node *values = service_attribute_values(attr);
    return values != NULL ? values->data.list.count : 0u;
}

static const char *service_attribute_value_at(Node *attr, size_t index) {
    Node *values = service_attribute_values(attr);
    Node *value;
    if (values == NULL || index >= values->data.list.count) return NULL;
    value = values->data.list.items[index];
    return value != NULL && value->type == NODE_STRING
               ? value->data.string_val
               : NULL;
}

static size_t service_named_attribute_count(Node *node, const char *name) {
    Node *attrs = service_attributes(node);
    size_t i;
    size_t count = 0u;
    if (attrs == NULL || name == NULL) return 0u;
    for (i = 0u; i < attrs->data.list.count; ++i) {
        Node *attr = attrs->data.list.items[i];
        const char *attr_name = service_attribute_name(attr);
        if (attr_name != NULL && strcmp(attr_name, name) == 0) ++count;
    }
    return count;
}

static Node *service_named_attribute(Node *node, const char *name) {
    Node *attrs = service_attributes(node);
    size_t i;
    if (attrs == NULL || name == NULL) return NULL;
    for (i = 0u; i < attrs->data.list.count; ++i) {
        Node *attr = attrs->data.list.items[i];
        const char *attr_name = service_attribute_name(attr);
        if (attr_name != NULL && strcmp(attr_name, name) == 0) return attr;
    }
    return NULL;
}

static int service_add_string(Node *map, const char *name, const char *value) {
    Node *node;
    if (map == NULL || map->type != NODE_MAP || name == NULL || value == NULL)
        return 0;
    node = create_node_string(name, value);
    if (node == NULL) return 0;
    if (map_add(map, node) != 0) {
        node_free(node);
        return 0;
    }
    return 1;
}

static int service_error(tbe_error_t *err, const char *message) {
    if (err != NULL)
        tbe_error_set(err, TBE_ERR_SEMANTIC_ERROR, -1, -1, message);
    return 0;
}

static int service_errorf(tbe_error_t *err, const char *fmt,
                          const char *a, const char *b) {
    char message[256];
    snprintf(message, sizeof(message), fmt,
             a != NULL ? a : "<unnamed>",
             b != NULL ? b : "<unnamed>");
    return service_error(err, message);
}

static int service_http_method(const char *name) {
    static const char *const methods[] = {
        "GET", "HEAD", "POST", "PUT", "DELETE",
        "CONNECT", "OPTIONS", "TRACE", "PATCH"
    };
    size_t i;
    if (name == NULL) return 0;
    for (i = 0u; i < sizeof(methods) / sizeof(methods[0]); ++i)
        if (strcmp(name, methods[i]) == 0) return 1;
    return 0;
}

static int service_identifier_span(const char *text, size_t length) {
    size_t i;
    if (text == NULL || length == 0u ||
        !(isalpha((unsigned char)text[0]) || text[0] == '_'))
        return 0;
    for (i = 1u; i < length; ++i)
        if (!(isalnum((unsigned char)text[i]) || text[i] == '_'))
            return 0;
    return 1;
}

static size_t service_path_placeholder_count(const char *path,
                                             const char *name) {
    const char *p;
    size_t count = 0u;
    size_t name_len = name != NULL ? strlen(name) : 0u;
    if (path == NULL || name == NULL) return 0u;
    p = path;
    while (*p != '\0') {
        if (*p == '{') {
            const char *end = strchr(p + 1, '}');
            if (end == NULL) return 0u;
            if ((size_t)(end - (p + 1)) == name_len &&
                memcmp(p + 1, name, name_len) == 0)
                ++count;
            p = end + 1;
        } else {
            ++p;
        }
    }
    return count;
}

static int service_route_valid(const char *path) {
    const char *p;
    if (path == NULL || path[0] != '/' ||
        strchr(path, '?') != NULL || strchr(path, '#') != NULL)
        return 0;
    p = path;
    while (*p != '\0') {
        if (*p == '}') return 0;
        if (*p != '{') {
            ++p;
            continue;
        }
        {
            const char *end = strchr(p + 1, '}');
            const char *again;
            size_t length;
            if (end == NULL) return 0;
            length = (size_t)(end - (p + 1));
            if (!service_identifier_span(p + 1, length)) return 0;
            again = end + 1;
            while ((again = strchr(again, '{')) != NULL) {
                const char *again_end = strchr(again + 1, '}');
                if (again_end == NULL) return 0;
                if ((size_t)(again_end - (again + 1)) == length &&
                    memcmp(again + 1, p + 1, length) == 0)
                    return 0;
                again = again_end + 1;
            }
            p = end + 1;
        }
    }
    return 1;
}

static const char *service_field_binding_kind(Node *field, Node **out_attr) {
    static const char *const kinds[] = {
        "path", "query", "header", "cookie", "body"
    };
    const char *found = NULL;
    Node *found_attr = NULL;
    size_t i;
    size_t total = 0u;

    for (i = 0u; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        size_t count = service_named_attribute_count(field, kinds[i]);
        if (count != 0u) {
            if (count != 1u) return NULL;
            total += count;
            found = kinds[i];
            found_attr = service_named_attribute(field, kinds[i]);
        }
    }

    if (total > 1u) return NULL;
    if (out_attr != NULL) *out_attr = found_attr;
    return found;
}

static const char *service_field_binding_name(Node *field, Node *attr) {
    const char *field_name = service_string(field, "name");
    if (attr == NULL || service_attribute_is_bare(attr)) return field_name;
    return service_attribute_value_at(attr, 0u);
}

static int service_field_binding_valid(Node *field, const char *kind,
                                       Node *attr, tbe_error_t *err) {
    size_t values;
    if (field == NULL || kind == NULL || attr == NULL) return 0;
    values = service_attribute_value_count(attr);

    if (strcmp(kind, "body") == 0) {
        if (!service_attribute_is_bare(attr))
            return service_error(err, "[body] does not accept a wire name");
        return 1;
    }

    if (!service_attribute_is_bare(attr) && values != 1u)
        return service_error(err, "Field binding accepts exactly one wire name");
    if (!service_attribute_is_bare(attr)) {
        const char *wire_name = service_attribute_value_at(attr, 0u);
        if (wire_name == NULL || wire_name[0] == '\0')
            return service_error(err, "Field binding wire name must not be empty");
    }

    if ((strcmp(kind, "path") == 0 || strcmp(kind, "query") == 0 ||
         strcmp(kind, "header") == 0 || strcmp(kind, "cookie") == 0) &&
        (service_flag(field, "is_collection") ||
         service_flag(field, "is_group_field") ||
         service_flag(field, "is_composite_ref")))
        return service_error(err,
                             "Non-body HTTP bindings require scalar or enum fields");

    if (strcmp(kind, "path") == 0 &&
        (service_flag(field, "is_optional") ||
         service_flag(field, "has_default")))
        return service_error(err, "HTTP path fields must be required");

    return 1;
}

static int service_validate_request_bindings(Node *root, Node *operation,
                                             tbe_error_t *err) {
    static const char *const binding_names[] = {
        "path", "query", "header", "cookie", "body"
    };
    const char *request_type = service_string(operation, "request_type");
    Node *request = schema_type_ref_node(root, request_type);
    Node *fields = request != NULL ? service_find_child(request, "fields") : NULL;
    size_t body_count = 0u;
    size_t i;

    if (fields == NULL || fields->type != NODE_LIST)
        return 1;

    for (i = 0u; i < fields->data.list.count; ++i) {
        Node *field = fields->data.list.items[i];
        Node *attr = NULL;
        const char *kind = service_field_binding_kind(field, &attr);
        size_t total = 0u;
        size_t k;

        for (k = 0u; k < sizeof(binding_names) / sizeof(binding_names[0]); ++k)
            total += service_named_attribute_count(field, binding_names[k]);

        if (total > 1u)
            return service_errorf(err,
                                  "Field '%s' has conflicting transport bindings in '%s'",
                                  service_string(field, "name"), request_type);
        if (total == 1u && kind == NULL)
            return service_errorf(err,
                                  "Field '%s' has duplicate transport binding in '%s'",
                                  service_string(field, "name"), request_type);
        if (kind == NULL) continue;
        if (!service_field_binding_valid(field, kind, attr, err)) return 0;
        if (strcmp(kind, "body") == 0 && ++body_count > 1u)
            return service_errorf(err,
                                  "Request type '%s' has more than one body field for '%s'",
                                  request_type, service_string(operation, "name"));
    }

    return 1;
}

static int service_validate_http_fields(Node *root, Node *operation,
                                        const char *path,
                                        tbe_error_t *err) {
    const char *request_type = service_string(operation, "request_type");
    Node *request = schema_type_ref_node(root, request_type);
    Node *fields = request != NULL ? service_find_child(request, "fields") : NULL;
    size_t i;
    size_t body_count = 0u;
    const char *p;

    if (fields == NULL || fields->type != NODE_LIST)
        return service_errorf(err,
                              "HTTP operation '%s' requires a record request type '%s'",
                              service_string(operation, "name"), request_type);

    for (i = 0u; i < fields->data.list.count; ++i) {
        Node *field = fields->data.list.items[i];
        Node *attr = NULL;
        const char *kind = service_field_binding_kind(field, &attr);
        size_t binding_attribute_total = 0u;
        static const char *const binding_names[] = {
            "path", "query", "header", "cookie", "body"
        };
        size_t k;

        for (k = 0u; k < sizeof(binding_names) / sizeof(binding_names[0]); ++k)
            binding_attribute_total +=
                service_named_attribute_count(field, binding_names[k]);

        if (binding_attribute_total > 1u)
            return service_errorf(err,
                                  "Field '%s' has conflicting transport bindings in '%s'",
                                  service_string(field, "name"), request_type);
        if (binding_attribute_total == 1u && kind == NULL)
            return service_errorf(err,
                                  "Field '%s' has duplicate transport binding in '%s'",
                                  service_string(field, "name"), request_type);
        if (kind == NULL) continue;
        if (!service_field_binding_valid(field, kind, attr, err)) return 0;

        if (strcmp(kind, "body") == 0) {
            if (++body_count > 1u)
                return service_errorf(err,
                                      "Request type '%s' has more than one body field for '%s'",
                                      request_type, service_string(operation, "name"));
        } else if (strcmp(kind, "path") == 0) {
            const char *binding_name = service_field_binding_name(field, attr);
            if (binding_name == NULL ||
                service_path_placeholder_count(path, binding_name) != 1u)
                return service_errorf(err,
                                      "Path binding '%s' does not match route '%s'",
                                      binding_name, path);
        }
    }

    p = path;
    while (*p != '\0') {
        if (*p != '{') {
            ++p;
            continue;
        }
        {
            const char *end = strchr(p + 1, '}');
            size_t length = (size_t)(end - (p + 1));
            size_t matches = 0u;
            for (i = 0u; i < fields->data.list.count; ++i) {
                Node *field = fields->data.list.items[i];
                Node *attr = NULL;
                const char *kind = service_field_binding_kind(field, &attr);
                const char *binding_name;
                if (kind == NULL || strcmp(kind, "path") != 0) continue;
                binding_name = service_field_binding_name(field, attr);
                if (binding_name != NULL && strlen(binding_name) == length &&
                    memcmp(binding_name, p + 1, length) == 0)
                    ++matches;
            }
            if (matches != 1u)
                return service_errorf(err,
                                      "Route placeholder '%s' must map to exactly one path field in '%s'",
                                      path, request_type);
            p = end + 1;
        }
    }

    return 1;
}

static int service_set_rpc_name(Node *operation, const char *service_name,
                                Node *rpc_attr) {
    const char *operation_name = service_string(operation, "name");
    const char *explicit_name = NULL;
    char *generated = NULL;
    size_t service_len;
    size_t operation_len;
    size_t total;
    int ok;

    if (rpc_attr == NULL) return 1;
    if (!service_attribute_is_bare(rpc_attr)) {
        if (service_attribute_value_count(rpc_attr) != 1u)
            return 0;
        explicit_name = service_attribute_value_at(rpc_attr, 0u);
        if (explicit_name == NULL || explicit_name[0] == '\0') return 0;
        return service_add_string(operation, "rpc_name", explicit_name);
    }

    service_len = strlen(service_name);
    operation_len = strlen(operation_name);
    if (service_len > SIZE_MAX - operation_len - 2u) return 0;
    total = service_len + operation_len + 2u;
    generated = (char *)malloc(total);
    if (generated == NULL) return 0;
    memcpy(generated, service_name, service_len);
    generated[service_len] = '.';
    memcpy(generated + service_len + 1u, operation_name, operation_len + 1u);
    ok = service_add_string(operation, "rpc_name", generated);
    free(generated);
    return ok;
}

static int service_validate_operation(Node *root, Node *service,
                                      Node *operation, tbe_error_t *err) {
    Node *attrs = service_attributes(operation);
    Node *rpc_attr = NULL;
    const char *service_name = service_string(service, "name");
    const char *operation_name = service_string(operation, "name");
    const char *request_type = service_string(operation, "request_type");
    const char *response_type = service_string(operation, "response_type");
    Node *errors = service_find_child(operation, "errors");
    const char *http_method = NULL;
    const char *http_path = NULL;
    size_t http_count = 0u;
    size_t rpc_count = 0u;
    size_t i;

    if (!schema_type_ref_exists(root, request_type))
        return service_errorf(err, "Service operation '%s' has unknown request type '%s'",
                              operation_name, request_type);
    if (response_type == NULL ||
        (strcmp(response_type, "void") != 0 &&
         !schema_type_ref_exists(root, response_type)))
        return service_errorf(err, "Service operation '%s' has unknown response type '%s'",
                              operation_name, response_type);

    if (!service_validate_request_bindings(root, operation, err))
        return 0;

    if (errors != NULL && errors->type == NODE_LIST) {
        for (i = 0u; i < errors->data.list.count; ++i) {
            Node *item = errors->data.list.items[i];
            const char *error_type =
                item != NULL && item->type == NODE_STRING
                    ? item->data.string_val
                    : NULL;
            size_t j;
            if (!schema_type_ref_exists(root, error_type))
                return service_errorf(err,
                                      "Service operation '%s' has unknown error type '%s'",
                                      operation_name, error_type);
            for (j = 0u; j < i; ++j) {
                Node *previous = errors->data.list.items[j];
                if (previous != NULL && previous->type == NODE_STRING &&
                    strcmp(previous->data.string_val, error_type) == 0)
                    return service_errorf(err,
                                          "Service operation '%s' repeats error type '%s'",
                                          operation_name, error_type);
            }
        }
    }

    if (attrs != NULL) {
        for (i = 0u; i < attrs->data.list.count; ++i) {
            Node *attr = attrs->data.list.items[i];
            const char *name = service_attribute_name(attr);
            if (service_http_method(name)) {
                ++http_count;
                http_method = name;
                if (service_attribute_is_bare(attr) ||
                    service_attribute_value_count(attr) != 1u)
                    return service_errorf(err,
                                          "HTTP operation '%s' requires exactly one route for '%s'",
                                          operation_name, name);
                http_path = service_attribute_value_at(attr, 0u);
            } else if (name != NULL && strcmp(name, "rpc") == 0) {
                ++rpc_count;
                rpc_attr = attr;
            }
        }
    }

    if (http_count > 1u)
        return service_errorf(err,
                              "Service operation '%s' has conflicting HTTP methods in '%s'",
                              operation_name, service_name);
    if (rpc_count > 1u)
        return service_errorf(err,
                              "Service operation '%s' repeats RPC projection in '%s'",
                              operation_name, service_name);

    if (http_count == 1u) {
        if (!service_route_valid(http_path))
            return service_errorf(err, "Service operation '%s' has invalid HTTP route '%s'",
                                  operation_name, http_path);
        if (!service_add_string(operation, "http_method", http_method) ||
            !service_add_string(operation, "http_path", http_path))
            return service_error(err, "Out of memory normalizing HTTP service projection");
        if (!service_validate_http_fields(root, operation, http_path, err)) return 0;
    }

    if (rpc_count == 1u) {
        if (!service_set_rpc_name(operation, service_name, rpc_attr))
            return service_error(err, "Invalid or unallocatable RPC service projection");
    }

    return 1;
}

static int service_operation_name_duplicate(Node *operations, size_t index) {
    const char *name = service_string(operations->data.list.items[index], "name");
    size_t i;
    for (i = 0u; i < index; ++i) {
        const char *previous =
            service_string(operations->data.list.items[i], "name");
        if (name != NULL && previous != NULL && strcmp(name, previous) == 0)
            return 1;
    }
    return 0;
}

static int service_name_duplicate(Node *services, size_t index) {
    const char *name = service_string(services->data.list.items[index], "name");
    size_t i;
    for (i = 0u; i < index; ++i) {
        const char *previous =
            service_string(services->data.list.items[i], "name");
        if (name != NULL && previous != NULL && strcmp(name, previous) == 0)
            return 1;
    }
    return 0;
}

static int service_validate_global_duplicates(Node *services, tbe_error_t *err) {
    size_t si;
    size_t sj;
    for (si = 0u; si < services->data.list.count; ++si) {
        Node *left_service = services->data.list.items[si];
        Node *left_ops = service_find_child(left_service, "operations");
        size_t oi;
        if (left_ops == NULL || left_ops->type != NODE_LIST) continue;
        for (oi = 0u; oi < left_ops->data.list.count; ++oi) {
            Node *left = left_ops->data.list.items[oi];
            const char *left_rpc = service_string(left, "rpc_name");
            const char *left_method = service_string(left, "http_method");
            const char *left_path = service_string(left, "http_path");
            size_t sj_start = si;
            for (sj = sj_start; sj < services->data.list.count; ++sj) {
                Node *right_service = services->data.list.items[sj];
                Node *right_ops = service_find_child(right_service, "operations");
                size_t oj_start = sj == si ? oi + 1u : 0u;
                size_t oj;
                if (right_ops == NULL || right_ops->type != NODE_LIST) continue;
                for (oj = oj_start; oj < right_ops->data.list.count; ++oj) {
                    Node *right = right_ops->data.list.items[oj];
                    const char *right_rpc = service_string(right, "rpc_name");
                    const char *right_method = service_string(right, "http_method");
                    const char *right_path = service_string(right, "http_path");
                    if (left_rpc != NULL && right_rpc != NULL &&
                        strcmp(left_rpc, right_rpc) == 0)
                        return service_errorf(err,
                                              "Duplicate effective RPC method '%s' in service '%s'",
                                              left_rpc,
                                              service_string(left_service, "name"));
                    if (left_method != NULL && left_path != NULL &&
                        right_method != NULL && right_path != NULL &&
                        strcmp(left_method, right_method) == 0 &&
                        strcmp(left_path, right_path) == 0)
                        return service_errorf(err,
                                              "Duplicate HTTP projection '%s' for route '%s'",
                                              left_method, left_path);
                }
            }
        }
    }
    return 1;
}

int schema_validate_services(Node *root, tbe_error_t *err) {
    Node *services = service_find_child(root, "services");
    size_t i;

    if (services == NULL || services->type != NODE_LIST)
        return service_error(err, "Schema service list is missing");

    for (i = 0u; i < services->data.list.count; ++i) {
        Node *service = services->data.list.items[i];
        Node *operations = service_find_child(service, "operations");
        const char *service_name = service_string(service, "name");
        size_t j;

        if (service_name == NULL || service_name[0] == '\0')
            return service_error(err, "Service declaration has no name");
        if (service_name_duplicate(services, i))
            return service_errorf(err, "Duplicate service '%s' in '%s'",
                                  service_name, "DataBind IDL");
        if (operations == NULL || operations->type != NODE_LIST)
            return service_errorf(err, "Service '%s' has no operation list in '%s'",
                                  service_name, "DataBind IDL");

        for (j = 0u; j < operations->data.list.count; ++j) {
            Node *operation = operations->data.list.items[j];
            if (service_operation_name_duplicate(operations, j))
                return service_errorf(err,
                                      "Duplicate operation '%s' in service '%s'",
                                      service_string(operation, "name"),
                                      service_name);
            if (!service_validate_operation(root, service, operation, err))
                return 0;
        }
    }

    return service_validate_global_duplicates(services, err);
}
