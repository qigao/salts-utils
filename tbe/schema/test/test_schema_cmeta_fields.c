#include "tinytest.h"
#include "schema_cmeta.h"
#include "node_tree.h"
#include <salts_cmeta_fixed_width.h>
#include <salts_cmeta_data.h>
#include <string.h>

/* Mutations caught: classify collection elements instead of the collection;
 * manufacture storage from wire flags; conflate UUID domain and text adapter;
 * publish partial output for an unknown schema type. */
#ifdef SCHEMA_CMETA_FIELD_RESOLUTION
static Node *field_root(void) {
    static const char *const lists[] = {"composites", "messages", "enums", "groups", "unions"};
    static const char *const names[] = {"Point", "Record", "State", "Rows", "Choice"};
    Node *root = create_node_map("root");
    size_t i;
    for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        Node *list = create_node_list(lists[i]);
        Node *record = create_node_map(NULL);
        check_equal(map_add(record, create_node_string("name", names[i])), 0);
        check_equal(list_add(list, record), 0);
        check_equal(map_add(root, list), 0);
    }
    return root;
}

static Node *field_node(const char *type, const char *flag) {
    Node *field = create_node_map(NULL);
    check_equal(map_add(field, create_node_string("type", type)), 0);
    if (flag) check_equal(map_add(field, create_node_string(flag, "1")), 0);
    return field;
}
#endif

suite("schema_cmeta_fields") {
    it("resolves canonical scalar descriptors without consuming presence overlay") {
#ifdef SCHEMA_CMETA_FIELD_RESOLUTION
        Node *root = field_root();
        Node *field = field_node("i32", "is_optional");
        schema_cmeta_field_type type;
        check_equal(map_add(field, create_node_string("default_value", "7")), 0);
        check(schema_cmeta_field_resolve(root, field, &type));
        check_equal(type.kind, CMETA_DATA_SINT);
        check_equal(type.schema_kind, "scalar");
        check(type.data != NULL);
        if (type.data) {
            cmeta_type_desc copy = salts_int32_cmeta_type;
            cmeta_type_identity identity = *copy.identity;
            copy.identity = &identity;
            check(cmeta_type_equal(type.data->storage_type, &copy));
            check_equal(type.data->storage_type->identity->form, CMETA_TYPE_ATOM);
        }
        node_free(field);
        field = field_node("uuid", NULL);
        check(schema_cmeta_field_resolve(root, field, &type));
        check_equal(type.kind, CMETA_DATA_CUSTOM);
        check(type.data == &salts_uuid_cmeta_data);
        check_equal(type.data->kind, CMETA_DATA_STRING);
        node_free(field);
        node_free(root);
#else
        check(0 && "missing shared CMeta field resolution");
#endif
    }

    it("exposes canonical kind-only containers without choosing storage") {
#ifdef SCHEMA_CMETA_FIELD_RESOLUTION
        static const struct { const char *flag; cmeta_data_kind kind; const cmeta_data_desc *data; } cases[] = {
            {"is_list", CMETA_DATA_SEQUENCE, &cmeta_data_sequence},
            {"is_set", CMETA_DATA_SET, &cmeta_data_set},
            {"is_map", CMETA_DATA_MAP, &cmeta_data_map},
            {"is_group_field", CMETA_DATA_SEQUENCE, &cmeta_data_sequence},
            {"is_collection", CMETA_DATA_SEQUENCE, &cmeta_data_sequence}
        };
        Node *root = field_root();
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            Node *field = field_node("int32", cases[i].flag);
            schema_cmeta_field_type type;
            check(schema_cmeta_field_resolve(root, field, &type));
            check_equal(type.kind, cases[i].kind);
            check(type.data == cases[i].data);
            check(cmeta_data_desc_valid(type.data));
            check_null(type.data->storage_type);
            check_null(type.data->shape);
            node_free(field);
        }
        node_free(root);
#else
        check(0 && "missing shared CMeta field resolution");
#endif
    }

    it("keeps named structure and storage gaps as explicit semantics only") {
#ifdef SCHEMA_CMETA_FIELD_RESOLUTION
        static const struct { const char *name; cmeta_data_kind kind; const char *label; } cases[] = {
            {"Point", CMETA_DATA_STRUCT, "composite"}, {"Record", CMETA_DATA_STRUCT, "message"},
            {"State", CMETA_DATA_ENUM, "enum"}, {"Choice", CMETA_DATA_VARIANT, "union"},
            {"string", CMETA_DATA_STRING, "string"}, {"bytes", CMETA_DATA_BYTES, "bytes"},
            {"datetime", CMETA_DATA_CUSTOM, "custom"}, {"date", CMETA_DATA_CUSTOM, "custom"},
            {"time", CMETA_DATA_CUSTOM, "custom"}, {"duration", CMETA_DATA_CUSTOM, "custom"},
            {"decimal", CMETA_DATA_CUSTOM, "custom"}, {"money", CMETA_DATA_CUSTOM, "custom"},
            {"bigint", CMETA_DATA_CUSTOM, "custom"}
        };
        Node *root = field_root();
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            Node *field = field_node(cases[i].name, NULL);
            schema_cmeta_field_type type;
            check(schema_cmeta_field_resolve(root, field, &type));
            check_equal(type.kind, cases[i].kind);
            check_equal(type.schema_kind, cases[i].label);
            check_null(type.data);
            node_free(field);
        }
        node_free(root);
#else
        check(0 && "missing shared CMeta field resolution");
#endif
    }

    it("rejects unknown field semantics atomically instead of a private custom fallback") {
#ifdef SCHEMA_CMETA_FIELD_RESOLUTION
        static const char *const names[] = {
            "MadeUp", "Option", "Pair", "Tuple", "Result", "Variant", "oneof",
            "pointer", "const_pointer", "Trait", "callable", "typed_any",
            "interface", "implements", "Range", "Collector", "effect", "property"
        };
        Node *root = field_root();
        schema_cmeta_field_type out, before;
        size_t i;
        memset(&out, 0xa5, sizeof(out));
        memcpy(&before, &out, sizeof(out));
        for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            Node *field = field_node(names[i], "is_numeric");
            check(!schema_cmeta_field_resolve(root, field, &out));
            check_equal(memcmp(&out, &before, sizeof(out)), 0);
            node_free(field);
        }
        check(!schema_cmeta_field_resolve(NULL, NULL, &out));
        check_equal(memcmp(&out, &before, sizeof(out)), 0);
        node_free(root);
#else
        check(0 && "missing shared CMeta field resolution");
#endif
    }
}
