#include "data_bind.h"
#include "tinytest.h"
#include <cmeta/data.h>
#include <cmeta/data.h>
#include <string.h>

/* Mutations caught: the legacy flag/string classifier masks canonical kind;
 * optional becomes Option; unresolved storage publishes NULL/partial output;
 * UUID loses its domain kind; reflection writes past a caller's size prefix. */
#ifdef TBE_CMETA_NODE_FRONTEND_SMOKE
DataBind *test_cmeta_reflection_codec(void);
#endif

#ifdef DATA_BIND_SCHEMA_CMETA_REFLECTION
static DataBind *reflection_codec(void) {
#ifdef TBE_CMETA_NODE_FRONTEND_SMOKE
    return test_cmeta_reflection_codec();
#else
    static const char schema[] =
        "schema Reflect [id(1), version(2), byte_order(little)];"
        "composite Point { int32 x; }"
        "enum State <int16> { Idle = 0; Ready = 7; }"
        "flags Permission <uint8> { Read = 1; Write = 2; }"
        "union Choice { Point point; }"
        "message Shape { int32 id; optional int32 count default 7; Point point; "
        "State state; Permission permission; uuid identity; bool enabled; "
        "Choice choice; datetime timestamp; MadeUp unknown; "
        "string title; bytes payload; list<int32> items; set<int32> unique; map<string,int32> lookup; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error), DATA_BIND_OK);
    return codec;
#endif
}

static size_t reflection_field(DataBind *codec, const char *name, DataBindSchemaField *out) {
    size_t i;
    for (i = 0; i < data_bind_schema_field_count(codec, "Shape"); ++i) {
        *out = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
        if (data_bind_schema_field_at(codec, "Shape", i, out) && strcmp(out->name, name) == 0)
            return i;
    }
    check(0 && "missing reflection fixture field");
    return SIZE_MAX;
}
#endif

suite("databind_cmeta_reflection") {
    it("publishes canonical kinds and descriptors while preserving the schema overlay") {
#ifdef DATA_BIND_SCHEMA_CMETA_REFLECTION
        static const struct { const char *name; cmeta_data_kind kind; const char *label; const char *id; } cases[] = {
            {"id", CMETA_DATA_SINT, "scalar", "salts.int32.data"},
            {"count", CMETA_DATA_SINT, "scalar", "salts.int32.data"},
            {"point", CMETA_DATA_STRUCT, "composite", NULL},
            {"state", CMETA_DATA_ENUM, "enum", NULL},
            {"permission", CMETA_DATA_ENUM, "enum", NULL},
            {"identity", CMETA_DATA_CUSTOM, "custom", "salts.uuid.data"},
            {"enabled", CMETA_DATA_BOOL, "scalar", "cmeta.bool.data"},
            {"choice", CMETA_DATA_VARIANT, "union", NULL},
            {"timestamp", CMETA_DATA_CUSTOM, "custom", NULL},
            {"title", CMETA_DATA_STRING, "string", NULL},
            {"payload", CMETA_DATA_BYTES, "bytes", NULL},
            {"items", CMETA_DATA_SEQUENCE, "list", "cmeta.data.sequence"},
            {"unique", CMETA_DATA_SET, "set", "cmeta.data.set"},
            {"lookup", CMETA_DATA_MAP, "map", "cmeta.data.map"}
        };
        DataBind *codec = reflection_codec();
        DataBindSchemaField field;
        size_t i;
        check_not_null(codec);
        if (!codec) return;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            reflection_field(codec, cases[i].name, &field);
            check_equal(field.has_cmeta_kind, 1);
            check_equal(field.cmeta_kind, cases[i].kind);
            check_equal(field.kind, cases[i].label);
            check_equal(field.is_enum, cases[i].kind == CMETA_DATA_ENUM);
            check_equal(field.is_map, cases[i].kind == CMETA_DATA_MAP);
            if (cases[i].id) {
                check_not_null(field.cmeta_data);
                if (field.cmeta_data) check_equal(field.cmeta_data->stable_id, cases[i].id);
            } else check_null(field.cmeta_data);
        }
        reflection_field(codec, "count", &field);
        check_equal(field.is_optional, 1);
        check_equal(field.has_default, 1);
        check_equal(field.default_value, "7");
        check_equal(field.cmeta_data->storage_type->identity->form, CMETA_TYPE_ATOM);
        {
            cmeta_type_desc copy = cmeta_type_int32;
            cmeta_type_identity identity = *copy.identity;
            copy.identity = &identity;
            check(cmeta_type_equal(field.cmeta_data->storage_type, &copy));
        }
        reflection_field(codec, "identity", &field);
        check_equal(field.cmeta_data->kind, CMETA_DATA_STRING);
        reflection_field(codec, "items", &field);
        check_null(field.cmeta_data->storage_type);
        check_null(field.cmeta_data->shape);
        reflection_field(codec, "unknown", &field);
        check_equal(field.has_cmeta_kind, 0);
        check_equal(field.kind, "unknown");
        check_null(field.cmeta_data);
        data_bind_free(codec);
#else
        check(0 && "missing canonical DataBind reflection facade");
#endif
    }

    it("fails storage descriptor queries atomically with Type.field diagnostics") {
#ifdef DATA_BIND_SCHEMA_CMETA_REFLECTION
        static const char *const unresolved[] = {"point", "state", "permission", "title", "payload", "items", "unique", "lookup", "choice", "timestamp", "unknown"};
        DataBind *codec = reflection_codec();
        const cmeta_data_desc *out = &cmeta_data_int32;
        DataBindSchemaField field;
        DataBindError error = DATA_BIND_ERROR_INIT;
        size_t i, index;
        check_not_null(codec);
        if (!codec) return;
        for (i = 0; i < sizeof(unresolved) / sizeof(unresolved[0]); ++i) {
            char path[64];
            index = reflection_field(codec, unresolved[i], &field);
            snprintf(path, sizeof(path), "Shape.%s", unresolved[i]);
            check_equal(data_bind_schema_field_cmeta_data(codec, "Shape", index, &out, &error), DATA_BIND_ERR_SCHEMA);
            check(out == &cmeta_data_int32);
            check_equal(error.code, DATA_BIND_ERR_SCHEMA);
            check_equal(error.path, path);
            check_not_null(strstr(error.message, "CMeta"));
        }
        index = reflection_field(codec, "count", &field);
        out = NULL;
        check_equal(data_bind_schema_field_cmeta_data(codec, "Shape", index, &out, &error), DATA_BIND_OK);
        /* Mutation: a successful lookup retains the previous failed query's diagnostic. */
        check_equal(error.code, DATA_BIND_OK);
        check_equal(error.path, "");
        check_not_null(out);
        if (out) {
            check_equal(out->stable_id, "salts.int32.data");
            check(cmeta_type_equal(out->storage_type, &cmeta_type_int32));
        }
        const cmeta_data_desc *published = out;
        check_equal(data_bind_schema_field_cmeta_data(codec, "Missing", 0u, &out, &error), DATA_BIND_ERR_TYPE_NOT_FOUND);
        check(out == published);
        check_equal(data_bind_schema_field_cmeta_data(codec, "Shape", SIZE_MAX, &out, &error), DATA_BIND_ERR_INVALID_ARG);
        check(out == published);
        data_bind_free(codec);
#else
        check(0 && "missing canonical DataBind storage query");
#endif
    }

    it("preserves the original reflection prefix and future caller tail") {
#ifdef DATA_BIND_SCHEMA_CMETA_REFLECTION
        DataBind *codec = reflection_codec();
        DataBindSchemaField field, before;
        size_t prefix = offsetof(DataBindSchemaField, has_cmeta_kind);
        struct { DataBindSchemaField field; unsigned char tail[16]; } future;
        size_t i;
        check_not_null(codec);
        if (!codec) return;
        memset(&field, 0xa5, sizeof(field));
        field.size = prefix;
        memcpy(&before, &field, sizeof(field));
        check(data_bind_schema_field_at(codec, "Shape", 0u, &field));
        check_equal(field.size, prefix);
        check_equal(memcmp((char *)&field + prefix, (char *)&before + prefix, sizeof(field) - prefix), 0);
        memset(&future, 0xa5, sizeof(future));
        future.field.size = sizeof(future);
        check(data_bind_schema_field_at(codec, "Shape", 0u, &future.field));
        for (i = 0; i < sizeof(future.tail); ++i) check_equal(future.tail[i], 0xa5u);
        data_bind_free(codec);
#else
        check(0 && "missing append-only CMeta reflection prefix");
#endif
    }
}
