#include "tinytest.h"
#include "compiler_core.h"
#include "schema_parser_dsl.h"

#include <stdio.h>
#include <string.h>

/* #45: exercise the actual parser -> compiler annotation path, not a private
 * mapper copy. Compare backend metadata while retaining source type spelling. */
enum { PROJECTION_SCHEMA_CAPACITY = 256 };
#define PROJECTION_COUNT(items_) (sizeof(items_) / sizeof((items_)[0]))

static Node *projection_child(const Node *parent, const char *name) {
    size_t i;
    if (parent == NULL || parent->type != NODE_MAP) return NULL;
    for (i = 0; i < parent->data.map.count; ++i) {
        Node *child = parent->data.map.items[i];
        if (child != NULL && child->name != NULL && strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

static const char *projection_text(const Node *parent, const char *name) {
    Node *child = projection_child(parent, name);
    return child != NULL && child->type == NODE_STRING ? child->data.string_val : NULL;
}

static Node *projection_field(Node *root) {
    Node *messages = projection_child(root, "messages");
    Node *fields;
    if (messages == NULL || messages->type != NODE_LIST || messages->data.list.count != 1u)
        return NULL;
    fields = projection_child(messages->data.list.items[0], "fields");
    if (fields == NULL || fields->type != NODE_LIST || fields->data.list.count != 1u)
        return NULL;
    return fields->data.list.items[0];
}

static int projection_equal(const char *alias, const char *canonical, const char *format) {
    static const char *const keys[] = {
        "cpp_type", "go_type", "ts_type", "python_type", "rust_type", "rfl_type",
        "typed_kind", "typed_declaration", "typed_element_kind", "typed_element_c_type",
        "typed_element_wire_kind", "typed_map_value_kind", "typed_map_value_wire_kind",
        "typed_map_entry_type", "typed_vector_type", "typed_fixed_count",
        "is_numeric", "is_integer", "is_unsigned", "is_float", "offset", "field_size_bytes"
    };
    const char *const names[] = {alias, canonical};
    Node *roots[2] = {NULL, NULL};
    Node *fields[2] = {NULL, NULL};
    int result = 0;
    size_t i;
    for (i = 0; i < PROJECTION_COUNT(roots); ++i) {
        char schema[PROJECTION_SCHEMA_CAPACITY];
        DataBindSchemaError error;
        int length = snprintf(schema, sizeof(schema), format, names[i]);
        if (length <= 0 || (size_t)length >= sizeof(schema)) goto done;
        roots[i] = create_node_map("root");
        if (roots[i] == NULL || parse_schema(schema, (size_t)length, roots[i], &error) != 0)
            goto done;
        data_bind_compiler_annotate_language_types(roots[i]);
        fields[i] = projection_field(roots[i]);
        if (fields[i] == NULL) goto done;
    }
    /* The canonical case must reach native code generation, not silently lack
     * the same annotation as a broken alias. */
    if (projection_text(fields[1], "typed_kind") == NULL) goto done;
    for (i = 0; i < PROJECTION_COUNT(keys); ++i) {
        const char *left = projection_text(fields[0], keys[i]);
        const char *right = projection_text(fields[1], keys[i]);
        if ((left == NULL) != (right == NULL) ||
            (left != NULL && strcmp(left, right) != 0)) {
            fprintf(stderr, "scalar projection %s -> %s differs at %s: %s / %s\n",
                    alias, canonical, keys[i], left ? left : "<missing>",
                    right ? right : "<missing>");
            goto done;
        }
    }
    if (strcmp(format, "message Scalar { %s value; }") == 0 &&
        (strcmp(projection_text(fields[0], "type"), alias) != 0 ||
         strcmp(projection_text(fields[1], "type"), canonical) != 0)) goto done;
    result = 1;
done:
    node_free(roots[0]);
    node_free(roots[1]);
    return result;
}

static int projection_float_is(const char *type, const char *cpp, const char *go,
                                const char *rust, const char *typed) {
    char schema[PROJECTION_SCHEMA_CAPACITY];
    Node *root = create_node_map("root");
    Node *field;
    DataBindSchemaError error;
    int ok = 0;
    int length = snprintf(schema, sizeof(schema), "message Scalar { %s value; }", type);
    if (root == NULL || length <= 0 || (size_t)length >= sizeof(schema) ||
        parse_schema(schema, (size_t)length, root, &error) != 0) goto done;
    data_bind_compiler_annotate_language_types(root);
    field = projection_field(root);
    if (field != NULL) {
        const char *keys[] = {"cpp_type", "go_type", "rust_type", "typed_kind",
                              "python_type", "ts_type"};
        const char *values[] = {cpp, go, rust, typed, "float", "number"};
        size_t i;
        ok = 1;
        for (i = 0; i < PROJECTION_COUNT(keys); ++i) {
            const char *actual = projection_text(field, keys[i]);
            if (actual == NULL || strcmp(actual, values[i]) != 0) ok = 0;
        }
    }
done:
    node_free(root);
    return ok;
}

suite("compiler_cmeta_scalar_projection") {
    it("preserves all existing integer aliases across backend projections") {
        static const char *const pairs[][2] = {
            {"int8_t", "int8_t"}, {"int8", "int8_t"}, {"i8", "int8_t"},
            {"uint8_t", "uint8_t"}, {"uint8", "uint8_t"}, {"u8", "uint8_t"},
            {"byte", "uint8_t"},
            {"int16_t", "int16_t"}, {"int16", "int16_t"}, {"i16", "int16_t"},
            {"uint16_t", "uint16_t"}, {"uint16", "uint16_t"}, {"u16", "uint16_t"},
            {"int32_t", "int32_t"}, {"int32", "int32_t"}, {"i32", "int32_t"},
            {"uint32_t", "uint32_t"}, {"uint32", "uint32_t"}, {"u32", "uint32_t"},
            {"int64_t", "int64_t"}, {"int64", "int64_t"}, {"i64", "int64_t"},
            {"uint64_t", "uint64_t"}, {"uint64", "uint64_t"}, {"u64", "uint64_t"}
        };
        size_t i;
        for (i = 0; i < PROJECTION_COUNT(pairs); ++i)
            check_true(projection_equal(pairs[i][0], pairs[i][1], "message Scalar { %s value; }"));
    }

    it("projects f32 exactly like canonical float without changing schema spelling") {
        check_true(projection_float_is("float", "float", "float32", "f32", "DATA_BIND_TYPED_F32"));
        check_true(projection_equal("f32", "float", "message Scalar { %s value; }"));
        check_true(projection_float_is("f32", "float", "float32", "f32", "DATA_BIND_TYPED_F32"));
    }

    it("projects f64 exactly like canonical double without changing schema spelling") {
        check_true(projection_float_is("double", "double", "float64", "f64", "DATA_BIND_TYPED_F64"));
        check_true(projection_equal("f64", "double", "message Scalar { %s value; }"));
        check_true(projection_float_is("f64", "double", "float64", "f64", "DATA_BIND_TYPED_F64"));
    }

    it("uses the same scalar projection for list elements") {
        check_true(projection_equal("f32", "float", "message Scalar { list<%s> value; }"));
        check_true(projection_equal("f64", "double", "message Scalar { list<%s> value; }"));
    }

    it("uses the same scalar projection for set elements") {
        check_true(projection_equal("f32", "float", "message Scalar { set<%s> value; }"));
        check_true(projection_equal("f64", "double", "message Scalar { set<%s> value; }"));
    }

    it("uses the same scalar projection for map values") {
        check_true(projection_equal("f32", "float", "message Scalar { map<string,%s> value; }"));
        check_true(projection_equal("f64", "double", "message Scalar { map<string,%s> value; }"));
    }
}

#undef PROJECTION_COUNT
