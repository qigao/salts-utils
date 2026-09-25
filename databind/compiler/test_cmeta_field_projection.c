#include "tinytest.h"
#include "compiler_core.h"
#include <cmeta/data.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum ExpectedRuntimeRequirement {
    EXPECT_EXPLICIT_ADAPTER,
    EXPECT_BOUNDED_ADAPTER,
    EXPECT_LIFECYCLE,
    EXPECT_OVERLAY_PRESENCE,
    EXPECT_OVERLAY_NULL,
    EXPECT_OVERLAY_PRESENCE_NULL,
    EXPECT_DEFERRED_CONTAINER
} ExpectedRuntimeRequirement;

typedef struct ExpectedRuntimeCapability {
    const char *schema;
    const char *generated_storage;
    cmeta_data_kind semantic_kind;
    ExpectedRuntimeRequirement requirement;
} ExpectedRuntimeCapability;

static const ExpectedRuntimeCapability EXPECTED[] = {
    { "bool", "uint8_t", CMETA_DATA_BOOL, EXPECT_EXPLICIT_ADAPTER },
    { "uuid", "salts_uuid_t", CMETA_DATA_CUSTOM, EXPECT_EXPLICIT_ADAPTER },
    { "bytes[16]", "uint8_t[16]", CMETA_DATA_BYTES, EXPECT_BOUNDED_ADAPTER },
    { "string", "tstr", CMETA_DATA_STRING, EXPECT_LIFECYCLE },
    { "bytes", "tbe_bytes_t", CMETA_DATA_BYTES, EXPECT_LIFECYCLE },
    { "optional int32", "presence + int32_t", CMETA_DATA_SINT, EXPECT_OVERLAY_PRESENCE },
    { "nullable int32", "null + int32_t", CMETA_DATA_SINT, EXPECT_OVERLAY_NULL },
    { "optional nullable int32", "presence + null + int32_t", CMETA_DATA_SINT,
      EXPECT_OVERLAY_PRESENCE_NULL },
    { "list<int32>", "vec_t", CMETA_DATA_SEQUENCE, EXPECT_DEFERRED_CONTAINER },
};

static const char *expected_native_requirement(ExpectedRuntimeRequirement requirement) {
    switch (requirement) {
        case EXPECT_EXPLICIT_ADAPTER:
        case EXPECT_BOUNDED_ADAPTER:
            return "fixed_value";
        case EXPECT_LIFECYCLE:
            return "owned_lifecycle";
        case EXPECT_OVERLAY_PRESENCE:
            return "overlay_presence";
        case EXPECT_OVERLAY_NULL:
            return "overlay_null";
        case EXPECT_OVERLAY_PRESENCE_NULL:
            return "overlay_presence_null";
        case EXPECT_DEFERRED_CONTAINER:
            return "deferred_container";
    }
    return NULL;
}

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
            if (strcmp(cases[i].type, "string") == 0) {
                check_equal(field_projection_text(field, "native_data_symbol"),
                            "salts_tstr_cmeta_data");
                check_equal(field_projection_text(field, "native_type_symbol"),
                            "salts_tstr_cmeta_type");
                check_equal(field_projection_text(field, "native_c_type"), "tstr");
                check_not_null(field_projection_child(field, "native_external"));
            }
            node_free(root);
        }
    }

    it("publishes installed fixed providers and keeps remaining capabilities deferred") {
        static const char *const names[] = {
            "BoolStorage", "UuidStorage", "FixedBytesStorage", "TextStorage",
            "BytesStorage", "OptionalStorage", "NullableStorage",
            "TriStateStorage", "ListStorage"};
        static const char *const types[] = {
            "bool", "uuid", "bytes", "string", "bytes", "int32", "int32",
            "int32", "list"};
        Node *root = create_node_map("root");
        size_t i;

        check_not_null(root);
        if (!root) return;
        for (i = 0; i < sizeof(EXPECTED) / sizeof(EXPECTED[0]); ++i) {
            Node *record = field_projection_add_record(root, "messages", names[i]);
            Node *field = field_projection_add_field(record, names[i], "value", types[i]);
            check_not_null(field);
            if (!field) continue;
            if (EXPECTED[i].requirement == EXPECT_BOUNDED_ADAPTER) {
                check_equal(map_add(field, create_node_string("is_fixed_size", "1")), 0);
                check_equal(map_add(field, create_node_string("size_bytes", "16")), 0);
            } else if (EXPECTED[i].requirement == EXPECT_OVERLAY_PRESENCE) {
                check_equal(map_add(field, create_node_string("is_optional", "1")), 0);
            } else if (EXPECTED[i].requirement == EXPECT_OVERLAY_NULL) {
                check_equal(map_add(field, create_node_string("is_nullable", "1")), 0);
            } else if (EXPECTED[i].requirement == EXPECT_OVERLAY_PRESENCE_NULL) {
                check_equal(map_add(field, create_node_string("is_optional", "1")), 0);
                check_equal(map_add(field, create_node_string("is_nullable", "1")), 0);
            } else if (EXPECTED[i].requirement == EXPECT_DEFERRED_CONTAINER) {
                check_equal(map_add(field, create_node_string("is_list", "1")), 0);
                check_equal(map_add(field, create_node_string("inner_type", "int32")), 0);
            }
        }

        tbe_compiler_annotate_language_types(root);

        for (i = 0; i < sizeof(EXPECTED) / sizeof(EXPECTED[0]); ++i) {
            Node *record = field_projection_record(root, "messages", names[i]);
            Node *fields = field_projection_child(record, "fields");
            Node *field = fields && fields->type == NODE_LIST && fields->data.list.count == 1u
                              ? fields->data.list.items[0]
                              : NULL;
            const char *kind = field_projection_text(field, "cmeta_kind");
            info("schema=%s storage=%s", EXPECTED[i].schema,
                 EXPECTED[i].generated_storage);
            check_not_null(field);
            check_not_null(kind);
            if (kind) check_equal(atoi(kind), EXPECTED[i].semantic_kind);
            check_equal(field_projection_text(field, "cmeta_native_requirement"),
                        expected_native_requirement(EXPECTED[i].requirement));
            if (i < 3u)
                check_not_null(field_projection_child(
                    record, "typed_cmeta_runtime_supported"));
            else
                check_null(field_projection_child(
                    record, "typed_cmeta_runtime_supported"));
            if (strcmp(names[i], "TextStorage") == 0) {
                check_equal(field_projection_text(field, "native_data_symbol"),
                            "salts_tstr_cmeta_data");
                check_equal(field_projection_text(field, "native_type_symbol"),
                            "salts_tstr_cmeta_type");
                check_not_null(field_projection_child(record, "cmeta_graph_supported"));
            }
        }

        node_free(root);
    }

    it("keeps optional containers deferred to the container provider boundary") {
        Node *root = create_node_map("root");
        Node *record;
        Node *field;

        check_not_null(root);
        if (!root) return;
        record = field_projection_add_record(root, "messages", "OptionalListStorage");
        field = field_projection_add_field(record, "OptionalListStorage", "value", "list");
        check_not_null(field);
        if (!field) {
            node_free(root);
            return;
        }
        check_equal(map_add(field, create_node_string("is_list", "1")), 0);
        check_equal(map_add(field, create_node_string("is_optional", "1")), 0);
        check_equal(map_add(field, create_node_string("inner_type", "int32")), 0);

        tbe_compiler_annotate_language_types(root);

        check_equal(field_projection_text(field, "cmeta_native_requirement"),
                    "deferred_container");
        check_null(field_projection_child(record, "typed_cmeta_runtime_supported"));
        check_null(field_projection_text(field, "typed_cmeta_runtime_supported"));
        node_free(root);
    }

    it("length-encodes fixed-byte provider identifiers without owner-field collisions") {
        /* A real generated TU cannot isolate this namespace: the older public
         * wire API already maps both A_B.C and A.B_C to A_B_C_* before the
         * provider source is compiled. Keep this compiler-metadata regression
         * scoped to the new private provider namespace; public API mangling is
         * a separate generator ABI decision. */
        Node *root = create_node_map("root");
        Node *left = field_projection_add_record(root, "messages", "A_B");
        Node *right = field_projection_add_record(root, "messages", "A");
        Node *left_field = field_projection_add_field(left, "A_B", "C", "bytes");
        Node *right_field = field_projection_add_field(right, "A", "B_C", "bytes");
        const char *left_symbol;
        const char *right_symbol;

        check_not_null(left_field);
        check_not_null(right_field);
        if (!left_field || !right_field) {
            node_free(root);
            return;
        }
        check_equal(map_add(left_field, create_node_string("is_fixed_size", "1")), 0);
        check_equal(map_add(left_field, create_node_string("size_bytes", "4")), 0);
        check_equal(map_add(right_field, create_node_string("is_fixed_size", "1")), 0);
        check_equal(map_add(right_field, create_node_string("size_bytes", "4")), 0);

        tbe_compiler_annotate_language_types(root);
        left_symbol = field_projection_text(left_field, "native_fixed_bytes_name");
        right_symbol = field_projection_text(right_field, "native_fixed_bytes_name");
        check_equal(left_symbol, "tbe_fixed_bytes_3_A_B_1_C");
        check_equal(right_symbol, "tbe_fixed_bytes_1_A_3_B_C");
        check(strcmp(left_symbol, right_symbol) != 0);
        check_not_null(field_projection_child(left, "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(right, "typed_cmeta_runtime_supported"));
        node_free(root);
    }

    it("injects enum symbols without sharing the public item macro namespace") {
        Node *root = create_node_map("root");
        Node *left = field_projection_add_enum(root, "A_B", "uint16", 0);
        Node *right = field_projection_add_enum(root, "AB", "uint16", 0);
        Node *record = field_projection_add_record(root, "messages", "Symbols");
        Node *field = field_projection_add_field(record, "Symbols", "value", "A_B");
        Node *long_enum;
        Node *long_field;
        char long_name[241];
        const char *left_symbol;
        const char *right_symbol;
        const char *long_symbol;
        const char *long_data;
        memset(long_name, 'A', sizeof(long_name) - 1u);
        long_name[sizeof(long_name) - 1u] = '\0';
        long_enum = field_projection_add_enum(root, long_name, "uint16", 0);
        long_field = field_projection_add_field(record, "Symbols", "long_value", long_name);
        check_not_null(left);
        check_not_null(right);
        check_not_null(field);
        check_not_null(long_enum);
        check_not_null(long_field);
        if (!left || !right || !field || !long_enum || !long_field) {
            node_free(root);
            return;
        }
        tbe_compiler_annotate_language_types(root);
        left_symbol = field_projection_text(left, "native_enum_symbol");
        right_symbol = field_projection_text(right, "native_enum_symbol");
        long_symbol = field_projection_text(long_enum, "native_enum_symbol");
        long_data = field_projection_text(long_field, "native_data_symbol");
        check_not_null(left_symbol);
        check_not_null(right_symbol);
        check_not_null(long_symbol);
        check_not_null(long_data);
        if (left_symbol && right_symbol && long_symbol && long_data) {
            check_equal(left_symbol, "tbeCmetaEnum3x415f42");
            check_equal(right_symbol, "tbeCmetaEnum2x4142");
            check(strcmp(left_symbol, right_symbol) != 0);
            check_null(strchr(left_symbol, '_'));
            check_null(strchr(right_symbol, '_'));
            check_equal(field_projection_text(field, "native_data_symbol"),
                        "tbeCmetaEnum3x415f42Data");
            check_equal(field_projection_text(field, "native_type_symbol"),
                        "tbeCmetaEnum3x415f42Type");
            check_equal(strlen(long_symbol), strlen("tbeCmetaEnum240x") + 480u);
            check_equal(strlen(long_data), strlen(long_symbol) + 4u);
            check_equal(strncmp(long_data, long_symbol, strlen(long_symbol)), 0);
            check_equal(long_data + strlen(long_symbol), "Data");
            check_null(strchr(long_data, '_'));
            check_not_null(field_projection_child(record, "typed_cmeta_runtime_supported"));
        }
        node_free(root);
    }

    it("classifies only complete native CMeta graphs for descriptor routing") {
        static const char *unsupported_records[] = {
            "TextStorage", "BytesStorage", "FixedArrayStorage", "ListStorage", "SetStorage",
            "MapStorage", "OptionalStorage",
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
        check_not_null(field_projection_child(perms, "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(wide, "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "FlagStorage"),
            "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "WideStorage"),
            "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "Depth32"),
            "typed_cmeta_runtime_supported"));
        check_null(field_projection_child(
            field_projection_record(root, "messages", "Depth33"),
            "typed_cmeta_runtime_supported"));

        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "Sample"),
            "cmeta_graph_supported"));
        check_null(field_projection_child(
            field_projection_record(root, "messages", "OptionalStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "FlagStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "UuidStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "BoolStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "Depth32"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "BoolStorage"),
            "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "TextStorage"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "UnsupportedNested"),
            "cmeta_graph_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "FixedBytesStorage"),
            "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
            field_projection_record(root, "messages", "UuidStorage"),
            "typed_cmeta_runtime_supported"));
        check_not_null(field_projection_child(
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
