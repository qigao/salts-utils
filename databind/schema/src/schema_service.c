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

static Node *service_attributes(Node *node) {
    Node *attrs = service_find_child(node, "attributes");
    return attrs != NULL && attrs->type == NODE_LIST ? attrs : NULL;
}

static const char *service_attribute_name(Node *attr) {
    return service_string(attr, "name");
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

static int service_transport_operation_attribute(const char *name) {
    static const char *const reserved[] = {
        "GET", "HEAD", "POST", "PUT", "DELETE",
        "CONNECT", "OPTIONS", "TRACE", "PATCH", "rpc"
    };
    size_t i;
    if (name == NULL) return 0;
    for (i = 0u; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (strcmp(name, reserved[i]) == 0) return 1;
    return 0;
}

static int service_transport_field_attribute(const char *name) {
    static const char *const reserved[] = {
        "path", "query", "header", "cookie", "body"
    };
    size_t i;
    if (name == NULL) return 0;
    for (i = 0u; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (strcmp(name, reserved[i]) == 0) return 1;
    return 0;
}

static int service_validate_transport_neutral(Node *root, Node *operation,
                                              tbe_error_t *err) {
    Node *attrs = service_attributes(operation);
    const char *request_type = service_string(operation, "request_type");
    Node *request = schema_type_ref_node(root, request_type);
    Node *fields = request != NULL ? service_find_child(request, "fields") : NULL;
    size_t i;

    if (attrs != NULL && attrs->type == NODE_LIST) {
        for (i = 0u; i < attrs->data.list.count; ++i) {
            Node *attr = attrs->data.list.items[i];
            const char *name = service_attribute_name(attr);
            if (service_transport_operation_attribute(name))
                return service_errorf(
                    err,
                    "Transport projection attribute '%s' is not part of canonical Service operation '%s'",
                    name, service_string(operation, "name"));
        }
    }

    if (fields != NULL && fields->type == NODE_LIST) {
        for (i = 0u; i < fields->data.list.count; ++i) {
            Node *field = fields->data.list.items[i];
            Node *field_attrs = service_attributes(field);
            size_t j;
            if (field_attrs == NULL || field_attrs->type != NODE_LIST) continue;
            for (j = 0u; j < field_attrs->data.list.count; ++j) {
                Node *attr = field_attrs->data.list.items[j];
                const char *name = service_attribute_name(attr);
                if (service_transport_field_attribute(name))
                    return service_errorf(
                        err,
                        "Transport field attribute '%s' is not part of canonical request type '%s'",
                        name, request_type);
            }
        }
    }

    return 1;
}

static int service_validate_operation(Node *root, Node *service,
                                      Node *operation, tbe_error_t *err) {
    const char *operation_name = service_string(operation, "name");
    const char *request_type = service_string(operation, "request_type");
    const char *response_type = service_string(operation, "response_type");
    Node *errors = service_find_child(operation, "errors");
    size_t i;
    (void)service;

    if (!schema_type_ref_exists(root, request_type))
        return service_errorf(err, "Service operation '%s' has unknown request type '%s'",
                              operation_name, request_type);
    if (response_type == NULL ||
        (strcmp(response_type, "void") != 0 &&
         !schema_type_ref_exists(root, response_type)))
        return service_errorf(err, "Service operation '%s' has unknown response type '%s'",
                              operation_name, response_type);

    if (!service_validate_transport_neutral(root, operation, err))
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

    return 1;
}
