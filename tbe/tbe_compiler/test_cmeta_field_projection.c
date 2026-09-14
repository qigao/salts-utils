#include "tinytest.h"
#include "compiler_core.h"
#include <cmeta/data.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *field_projection_child(Node *node, const char *name) {
    size_t i;
    for (i = 0; node && i < node->data.map.count; ++i)
        if (node->data.map.items[i]->name && strcmp(node->data.map.items[i]->name, name) == 0)
            return node->data.map.items[i];
    return NULL;
}

static const char *field_projection_text(Node *node, const char *name) {
    Node *child = field_projection_child(node, name);
    return child && child->type == NODE_STRING ? child->data.string_val : NULL;
}

static Node *field_projection_record(Node *root, const char *list_name,
                                     const char *name) {
    Node *list = field_projection_child(root, list_name);
    size_t i;
    if (!list || list->type != NODE_LIST) return NULL;
    for (i = 0; i < list->data.list.count; ++i) {
        Node *record = list->data.list.items[i];
        const char *record_name = field_projection_text(record, "name");
        if (record_name && strcmp(record_name, name) == 0) return record;
    }
    return NULL;
}

static Node *field_projection_add_record(Node *root, const char *list_name,
                                         const char *name) {
    Node *list = field_projection_child(root, list_name);
    Node *record;
    Node *fields;
    if (!list) {
        list = create_node_list(list_name);
        if (!list || map_add(root, list) != 0) return NULL;
    }
    record = create_node_map(NULL);
    fields = create_node_list("fields");
    if (!record || !fields ||
        map_add(record, create_node_string("name", name)) != 0 ||
        map_add(record, fields) != 0 || list_add(list, record) != 0) {
        node_free(record);
        return NULL;
    }
    return record;
}

static Node *field_projection_add_field(Node *record, const char *owner,
                                        const char *name, const char *type) {
    Node *fields = field_projection_child(record, "fields");
    Node *field = create_node_map(NULL);
    if (!fields || !field ||
        map_add(field, create_node_string("name", name)) != 0 ||
        map_add(field, create_node_string("owner_name", owner)) != 0 ||
        map_add(field, create_node_string("type", type)) != 0 ||
        list_add(fields, field) != 0) {
        node_free(field);
        return NULL;
    }
    return field;
}

static Node *field_projection_add_enum(Node *root, const char *name,
                                       const char *underlying, int is_flags) {
    Node *list = field_projection_child(root, "enums");
    Node *value;
    if (!list) {
        list = create_node_list("enums");
        if (!list || map_add(root, list) != 0) return NULL;
    }
    value = create_node_map(NULL);
    if (!value || map_add(value, create_node_string("name", name)) != 0 ||
        map_add(value, create_node_string("enum_name", name)) != 0 ||
        map_add(value, create_node_string("underlying_type", underlying)) != 0 ||
        (is_flags && map_add(value, create_node_string("is_flags", "1")) != 0) ||
        list_add(list, value) != 0) {
        node_free(value);
        return NULL;
    }
    return value;
}

/* Mutation: retain a compiler-only flag tree or classify a collection by its
 * element, causing the C/other-language projection to disagree with CMeta. */
suite("compiler_cmeta_field_projection") {
    it("annotates real backend projections from the shared field semantic rule") {
        static const struct { const char *type; const char *flag; cmeta_data_kind kind; const char *label; const char *cpp; const char *typed; const char *id; } cases[] = {
            {"int32", NULL, CMETA_DATA_SINT, "scalar", "std::int32_t", "TBE_TYPED_I32", "salts.int32.data"},
            {"f32", "is_optional", CMETA_DATA_FLOAT, "scalar", "float", "TBE_TYPED_F32", "cmeta.float.data"},
            {"bool", NULL, CMETA_DATA_BOOL, "scalar", "bool", "TBE_TYPED_BOOL", "cmeta.bool.data"},
            {"uuid", NULL, CMETA_DATA_CUSTOM, "custom", "salts_uuid_t", "TBE_TYPED_UUID", "salts.uuid.data"},
            {"string", NULL, CMETA_DATA_STRING, "string", "std::string", "TBE_TYPED_STRING", NULL},
            {"bytes", NULL, CMETA_DATA_BYTES, "bytes", "std::vector<std::uint8_t>", "TBE_TYPED_BYTES", NULL},
            {"list", "is_list", CMETA_DATA_SEQUENCE, "list", "std::vector<std::int32_t>", "TBE_TYPED_LIST", "cmeta.data.sequence"},
            {"set", "is_set", CMETA_DATA_SET, "set", "std::set<std::int32_t>", "TBE_TYPED_SET", "cmeta.data.set"},
            {"map", "is_map", CMETA_DATA_MAP, "map", "std::map<std::string, std::int32_t>", "TBE_TYPED_MAP", "cmeta.data.map"}
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            Node *root = create_node_map("root"), *messages = create_node_list("messages");
            Node *record = create_node_map(NULL), *fields = create_node_list("fields");
            Node *field = create_node_map(NULL);
            const char *kind;
            map_add(root, messages); list_add(messages, record);
            map_add(record, create_node_string("name", "Shape")); map_add(record, fields); list_add(fields, field);
            map_add(field, create_node_string("name", "value"));
            map_add(field, create_node_string("owner_name", "Shape"));
            map_add(field, create_node_string("type", cases[i].type));
            map_add(field, create_node_string("inner_type", "int32"));
            map_add(field, create_node_string("key_type", "string"));
            map_add(field, create_node_string("value_type", "int32"));
            if (cases[i].flag) map_add(field, create_node_string(cases[i].flag, "1"));
            tbe_compiler_annotate_language_types(root);
            kind = field_projection_text(field, "cmeta_kind");
            check_not_null(kind);
            if (kind) check_equal(atoi(kind), cases[i].kind);
            check_equal(field_projection_text(field, "cmeta_schema_kind"), cases[i].label);
            check_equal(field_projection_text(field, "cpp_type"), cases[i].cpp);
            check_equal(field_projection_text(field, "typed_kind"), cases[i].typed);
            if (cases[i].id) check_equal(field_projection_text(field, "cmeta_data_id"), cases[i].id);
            else check_null(field_projection_text(field, "cmeta_data_id"));
            check_equal(field_projection_text(field, "type"), cases[i].type);
            if (cases[i].kind == CMETA_DATA_SEQUENCE || cases[i].kind == CMETA_DATA_SET || cases[i].kind == CMETA_DATA_MAP)
                check_null(field_projection_text(field, "native_data_symbol"));
            node_free(root);
        }
    }

    it("classifies only complete native CMeta graphs for descriptor routing") {
        static const char *unsupported_records[] = {
            "BoolStorage", "TextStorage", "BytesStorage", "FixedBytesStorage",
            "FixedArrayStorage", "UuidStorage", "ListStorage", "SetStorage",
            "MapStorage", "OptionalStorage", "FlagStorage", "WideStorage",
            "UnsupportedNested", "Cycle"
        };
        Node *root = create_node_map("root");
        Node *record;
        Node *field;
        Node *sample;
        Node *fields;
        Node *state;
        Node *perms;
        Node *wide;
        size_t i;

        check_not_null(root);
        if (!root) return;
        check_not_null(field_projection_add_enum(root, "State", "uint8", 0));
        check_not_null(field_projection_add_enum(root, "Perms", "uint16", 1));
        check_not_null(field_projection_add_enum(root, "Wide", "uint64", 0));

        record = field_projection_add_record(root, "composites", "Point");
        field = field_projection_add_field(record, "Point", "x", "int32");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
        field = field_projection_add_field(record, "Point", "y", "double");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
        record = field_projection_add_record(root, "composites", "Header");
        check_not_null(field_projection_add_field(record, "Header", "state", "State"));
        check_not_null(field_projection_add_field(record, "Header", "point", "Point"));
        record = field_projection_add_record(root, "messages", "Sample");
        field = field_projection_add_field(record, "Sample", "count", "uint32");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
        check_not_null(field_projection_add_field(record, "Sample", "header", "Header"));

#define ADD_UNSUPPORTED_RECORD(NAME, TYPE) \
        record = field_projection_add_record(root, "messages", NAME); \
        check_not_null(field_projection_add_field(record, NAME, "value", TYPE))
        ADD_UNSUPPORTED_RECORD("BoolStorage", "bool");
        ADD_UNSUPPORTED_RECORD("TextStorage", "string");
        ADD_UNSUPPORTED_RECORD("BytesStorage", "bytes");
        ADD_UNSUPPORTED_RECORD("UuidStorage", "uuid");
        ADD_UNSUPPORTED_RECORD("FlagStorage", "Perms");
        ADD_UNSUPPORTED_RECORD("WideStorage", "Wide");
        ADD_UNSUPPORTED_RECORD("UnsupportedNested", "TextStorage");
        ADD_UNSUPPORTED_RECORD("Cycle", "Cycle");
#undef ADD_UNSUPPORTED_RECORD

        record = field_projection_add_record(root, "messages", "FixedBytesStorage");
        field = field_projection_add_field(record, "FixedBytesStorage", "value", "bytes");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
        record = field_projection_add_record(root, "messages", "FixedArrayStorage");
        field = field_projection_add_field(record, "FixedArrayStorage", "value", "list");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_list", "1")), 0);
        check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
        check_equal(map_add(field, create_node_string("inner_type", "uint32")), 0);
        record = field_projection_add_record(root, "messages", "ListStorage");
        field = field_projection_add_field(record, "ListStorage", "value", "list");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_list", "1")), 0);
        check_equal(map_add(field, create_node_string("inner_type", "int32")), 0);
        record = field_projection_add_record(root, "messages", "SetStorage");
        field = field_projection_add_field(record, "SetStorage", "value", "set");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_set", "1")), 0);
        check_equal(map_add(field, create_node_string("inner_type", "int32")), 0);
        record = field_projection_add_record(root, "messages", "MapStorage");
        field = field_projection_add_field(record, "MapStorage", "value", "map");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_map", "1")), 0);
        check_equal(map_add(field, create_node_string("key_type", "string")), 0);
        check_equal(map_add(field, create_node_string("value_type", "int32")), 0);
        record = field_projection_add_record(root, "messages", "OptionalStorage");
        field = field_projection_add_field(record, "OptionalStorage", "value", "uint32");
        check_not_null(field);
        check_equal(map_add(field, create_node_string("is_optional", "1")), 0);

        for (i = 0; i <= 33u; ++i) {
            char name[32];
            char child[32];
            snprintf(name, sizeof(name), "Depth%u", (unsigned)i);
            record = field_projection_add_record(root, "messages", name);
            if (i == 0u) {
                field = field_projection_add_field(record, name, "value", "int32");
                check_not_null(field);
                check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
            } else {
                snprintf(child, sizeof(child), "Depth%u", (unsigned)(i - 1u));
                check_not_null(field_projection_add_field(record, name, "value", child));
            }
        }

        tbe_compiler_annotate_language_types(root);

        check_not_null(field_projection_child(
            field_projection_record(root, "composites", "Point"),
            "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "composites", "Header"),
            "typed_cmeta_runtime_supported"));
        sample = field_projection_record(root, "messages", "Sample");
        check_not_null(field_projection_child(sample, "typed_cmeta_runtime_supported"));
        fields = field_projection_child(sample, "fields");
        check_not_null(fields);
        if (fields && fields->type == NODE_LIST && fields->data.list.count == 2u) {
            check_not_null(field_projection_child(fields->data.list.items[0],
                                                  "typed_cmeta_runtime_supported"));
            check_not_null(field_projection_child(fields->data.list.items[1],
                                                  "typed_cmeta_runtime_supported"));
            check_equal(field_projection_text(fields->data.list.items[1],
                                              "typed_nested_overlay"),
                        "&Header_TYPED_TYPE");
        }

        state = field_projection_record(root, "enums", "State");
        perms = field_projection_record(root, "enums", "Perms");
        wide = field_projection_record(root, "enums", "Wide");
        check_not_null(field_projection_child(state, "typed_cmeta_runtime_supported"));
        check_null(field_projection_child(perms, "typed_cmeta_runtime_supported"));
        check_null(field_projection_child(wide, "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "Depth32"),
            "typed_cmeta_runtime_supported"));
        check_null(field_projection_child(
            field_projection_record(root, "messages", "Depth33"),
            "typed_cmeta_runtime_supported"));

        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "Sample"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "OptionalStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "FlagStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "UuidStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "Depth32"),
            "cmeta_graph_supported"));
        check_null(field_projection_child(
            field_projection_record(root, "messages", "BoolStorage"),
            "cmeta_graph_supported"));
        check_null(field_projection_child(
            field_projection_record(root, "messages", "WideStorage"),
            "cmeta_graph_supported"));
        check_null(field_projection_child(
            field_projection_record(root, "messages", "Depth33"),
            "cmeta_graph_supported"));

        for (i = 0; i < sizeof(unsupported_records) / sizeof(unsupported_records[0]); ++i) {
            Node *record = field_projection_record(root, "messages", unsupported_records[i]);
            info("record=%s", unsupported_records[i]);
            check_not_null(record);
            check_null(field_projection_child(record, "typed_cmeta_runtime_supported"));
        }
        node_free(root);
    }
}
