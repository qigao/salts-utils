#include "tinytest.h"
#include "compiler_core.h"
#include <cmeta/data.h>
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
}
