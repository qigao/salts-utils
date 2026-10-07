#include "tinytest.h"
#include "schema_cmeta.h"
#include "binary_contract_overlay.h"
#include "binary_scalar_profile.h"

#include <stdio.h>
#include <string.h>

#define PROFILE_COUNT(items_) (sizeof(items_) / sizeof((items_)[0]))
enum { PROFILE_SCHEMA_CAPACITY = 128, PROFILE_SIZE_TEXT_CAPACITY = 32 };

typedef struct ProfileCase {
    const char *alias;
    const char *canonical;
    size_t wire_size;
    const char *reader;
    const char *host;
    cmeta_data_kind kind;
} ProfileCase;

static const ProfileCase profile_cases[] = {
    {"bool", "bool", 1u, "u8", "uint8_t", CMETA_DATA_BOOL},
    {"uint8_t", "uint8_t", 1u, "u8", "uint8_t", CMETA_DATA_UINT},
    {"uint8", "uint8_t", 1u, "u8", "uint8_t", CMETA_DATA_UINT},
    {"u8", "uint8_t", 1u, "u8", "uint8_t", CMETA_DATA_UINT},
    {"uint16_t", "uint16_t", 2u, "u16", "uint16_t", CMETA_DATA_UINT},
    {"uint16", "uint16_t", 2u, "u16", "uint16_t", CMETA_DATA_UINT},
    {"u16", "uint16_t", 2u, "u16", "uint16_t", CMETA_DATA_UINT},
    {"uint32_t", "uint32_t", 4u, "u32", "uint32_t", CMETA_DATA_UINT},
    {"uint32", "uint32_t", 4u, "u32", "uint32_t", CMETA_DATA_UINT},
    {"u32", "uint32_t", 4u, "u32", "uint32_t", CMETA_DATA_UINT},
    {"uint64_t", "uint64_t", 8u, "u64", "uint64_t", CMETA_DATA_UINT},
    {"uint64", "uint64_t", 8u, "u64", "uint64_t", CMETA_DATA_UINT},
    {"u64", "uint64_t", 8u, "u64", "uint64_t", CMETA_DATA_UINT},
    {"int8_t", "int8_t", 1u, "i8", "int8_t", CMETA_DATA_SINT},
    {"int8", "int8_t", 1u, "i8", "int8_t", CMETA_DATA_SINT},
    {"i8", "int8_t", 1u, "i8", "int8_t", CMETA_DATA_SINT},
    {"int16_t", "int16_t", 2u, "i16", "int16_t", CMETA_DATA_SINT},
    {"int16", "int16_t", 2u, "i16", "int16_t", CMETA_DATA_SINT},
    {"i16", "int16_t", 2u, "i16", "int16_t", CMETA_DATA_SINT},
    {"int32_t", "int32_t", 4u, "i32", "int32_t", CMETA_DATA_SINT},
    {"int32", "int32_t", 4u, "i32", "int32_t", CMETA_DATA_SINT},
    {"i32", "int32_t", 4u, "i32", "int32_t", CMETA_DATA_SINT},
    {"int64_t", "int64_t", 8u, "i64", "int64_t", CMETA_DATA_SINT},
    {"int64", "int64_t", 8u, "i64", "int64_t", CMETA_DATA_SINT},
    {"i64", "int64_t", 8u, "i64", "int64_t", CMETA_DATA_SINT},
    {"byte", "uint8_t", 1u, "u8", "uint8_t", CMETA_DATA_UINT},
    {"float", "float", 4u, "f32", "float", CMETA_DATA_FLOAT},
    {"f32", "float", 4u, "f32", "float", CMETA_DATA_FLOAT},
    {"double", "double", 8u, "f64", "double", CMETA_DATA_FLOAT},
    {"f64", "double", 8u, "f64", "double", CMETA_DATA_FLOAT},
};

static Node *profile_child(const Node *parent, const char *name) {
    size_t i;
    if (parent == NULL || parent->type != NODE_MAP) return NULL;
    for (i = 0u; i < parent->data.map.count; ++i) {
        Node *child = parent->data.map.items[i];
        if (child != NULL && child->name != NULL && strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

static int profile_text_is(const Node *parent, const char *name, const char *expected) {
    Node *child = profile_child(parent, name);
    return child != NULL && child->type == NODE_STRING &&
           child->data.string_val != NULL && strcmp(child->data.string_val, expected) == 0;
}

suite("schema_cmeta_production_profiles") {
    it("resolves scalar aliases to canonical wire profiles") {
        size_t i;
        for (i = 0u; i < PROFILE_COUNT(profile_cases); ++i) {
            const ProfileCase *item = &profile_cases[i];
            const databind_binary_scalar_profile_t *info = databind_binary_scalar_profile_find(item->alias);
            check_not_null(info);
            check_equal(strcmp(info->name, item->canonical), 0);
        }
    }

    it("preserves every existing numeric wire width reader and host projection") {
        size_t i;
        for (i = 0u; i < PROFILE_COUNT(profile_cases); ++i) {
            const ProfileCase *item = &profile_cases[i];
            const databind_binary_scalar_profile_t *info = databind_binary_scalar_profile_find(item->alias);
            check_not_null(info);
            check_equal(info->size, item->wire_size);
            check_equal(strcmp(info->wire_reader, item->reader), 0);
            check_equal(strcmp(info->host_type, item->host), 0);
        }
    }

    it("does not guess a numeric or buffer storage profile for unsupported names") {
        static const char *const names[] = {
            NULL, "", "string", "bytes", "uuid", "datetime", "UserRecord",
            "int128", "varint", "INT32", " int32", "int32 "
        };
        size_t i;
        for (i = 0u; i < PROFILE_COUNT(names); ++i)
            check_null(databind_binary_scalar_profile_find(names[i]));
        check_null(schema_cmeta_builtin_data("string"));
        check_null(schema_cmeta_builtin_data("bytes"));
    }

    it("preserves production parser spelling layout and numeric annotations for all aliases") {
        size_t i;
        for (i = 0u; i < PROFILE_COUNT(profile_cases); ++i) {
            const ProfileCase *item = &profile_cases[i];
            char schema[PROFILE_SCHEMA_CAPACITY];
            char size_text[PROFILE_SIZE_TEXT_CAPACITY];
            Node *root = create_node_map("root");
            Node *messages, *record = NULL, *fields, *field = NULL;
            tbe_error_t error;
            int parsed, matches = 0;
            int written = snprintf(schema, sizeof(schema), "message Scalar { %s value; }", item->alias);
            check_true(written > 0 && (size_t)written < sizeof(schema));
            check_not_null(root);
            snprintf(size_text, sizeof(size_text), "%zu", item->wire_size);
            parsed = databind_binary_contract_parse(schema, (size_t)written, root, &error);
            messages = profile_child(root, "messages");
            if (parsed == 0 && messages != NULL && messages->type == NODE_LIST &&
                messages->data.list.count == 1u) {
                record = messages->data.list.items[0];
                fields = profile_child(record, "fields");
                if (fields != NULL && fields->type == NODE_LIST && fields->data.list.count == 1u)
                    field = fields->data.list.items[0];
            }
            if (field != NULL) {
                matches = profile_text_is(field, "type", item->alias) &&
                          profile_text_is(field, "offset", "0") &&
                          profile_text_is(field, "field_size_bytes", size_text) &&
                          profile_text_is(record, "fixed_block_size", size_text);
                if (item->kind != CMETA_DATA_BOOL) {
                    matches = matches && profile_text_is(field, "host_type", item->host) &&
                              profile_text_is(field, "wire_reader", item->reader) &&
                              profile_text_is(field, "is_numeric", "1");
                } else {
                    matches = matches && profile_child(field, "is_numeric") == NULL;
                }
                matches = matches &&
                    ((profile_child(field, "is_integer") != NULL) ==
                     (item->kind == CMETA_DATA_SINT || item->kind == CMETA_DATA_UINT)) &&
                    ((profile_child(field, "is_unsigned") != NULL) == (item->kind == CMETA_DATA_UINT)) &&
                    ((profile_child(field, "is_float") != NULL) == (item->kind == CMETA_DATA_FLOAT));
            }
            node_free(root);
            if (parsed != 0 || !matches)
                fprintf(stderr, "scalar production profile mismatch for %s\n", item->alias);
            check_equal(parsed, 0);
            check_true(matches);
        }
    }
}

#undef PROFILE_COUNT
