#include "tinytest.h"
#include "schema_cmeta.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
#include <string.h>

static const IdlDataDecl TEST_DATA[] = {
    {IDL_DATA_COMPOSITE, "Point", NULL, 0, 0u, NULL, 0u, NULL, 0u, NULL},
    {IDL_DATA_MESSAGE, "Record", NULL, 0, 0u, NULL, 0u, NULL, 0u, NULL},
    {IDL_DATA_ENUM, "State", "int32", 0, 0u, NULL, 0u, NULL, 0u, NULL},
    {IDL_DATA_GROUP, "Rows", NULL, 0, 0u, NULL, 0u, NULL, 0u, NULL},
    {IDL_DATA_UNION, "Choice", NULL, 0, 0u, NULL, 0u, NULL, 0u, NULL},
};

static const IdlContract TEST_CONTRACT = {
    sizeof(IdlContract), IDL_CONTRACT_ABI_VERSION,
    "SchemaCMeta", "1",
    sizeof(TEST_DATA) / sizeof(TEST_DATA[0]), TEST_DATA,
    0u, NULL, 0u, NULL, 0u, NULL};

static IdlField field_of(const char *type, IdlCollectionKind collection) {
    IdlField field = {0};
    field.name = "value";
    field.type_name = type;
    field.collection_kind = collection;
    return field;
}

suite("schema_cmeta_fields") {
    it("resolves canonical scalar descriptors without consuming presence overlay") {
        IdlField field = field_of("i32", IDL_COLLECTION_NONE);
        schema_cmeta_field_type type;
        field.optional = 1;
        field.default_value = "7";
        check(schema_cmeta_field_resolve(&TEST_CONTRACT, &field, &type));
        check_equal(type.kind, CMETA_DATA_SINT);
        check_equal(type.schema_kind, "scalar");
        check(type.data != NULL);
        if (type.data) {
            cmeta_type_desc copy = cmeta_type_int32;
            cmeta_type_identity identity = *copy.identity;
            copy.identity = &identity;
            check(cmeta_type_equal(type.data->storage_type, &copy));
            check_equal(type.data->storage_type->identity->form, CMETA_TYPE_ATOM);
        }

        field = field_of("uuid", IDL_COLLECTION_NONE);
        check(schema_cmeta_field_resolve(&TEST_CONTRACT, &field, &type));
        check_equal(type.kind, CMETA_DATA_CUSTOM);
        check(type.data == &salts_uuid_cmeta_data);
        check_equal(type.data->kind, CMETA_DATA_STRING);
    }

    it("exposes canonical kind-only containers without choosing storage") {
        static const struct {
            IdlCollectionKind collection;
            cmeta_data_kind kind;
            const cmeta_data_desc *data;
        } cases[] = {
            {IDL_COLLECTION_LIST, CMETA_DATA_SEQUENCE, &cmeta_data_sequence},
            {IDL_COLLECTION_SET, CMETA_DATA_SET, &cmeta_data_set},
            {IDL_COLLECTION_MAP, CMETA_DATA_MAP, &cmeta_data_map},
            {IDL_COLLECTION_GROUP, CMETA_DATA_SEQUENCE, &cmeta_data_sequence},
            {IDL_COLLECTION_ARRAY, CMETA_DATA_SEQUENCE, &cmeta_data_sequence},
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            IdlField field = field_of("int32", cases[i].collection);
            schema_cmeta_field_type type;
            check(schema_cmeta_field_resolve(&TEST_CONTRACT, &field, &type));
            check_equal(type.kind, cases[i].kind);
            check(type.data == cases[i].data);
            check(cmeta_data_desc_valid(type.data));
            check_null(type.data->storage_type);
            check_null(type.data->shape);
        }
    }

    it("keeps named structure and storage gaps as explicit semantics only") {
        static const struct {
            const char *name;
            cmeta_data_kind kind;
            const char *label;
        } cases[] = {
            {"Point", CMETA_DATA_STRUCT, "composite"},
            {"Record", CMETA_DATA_STRUCT, "message"},
            {"State", CMETA_DATA_ENUM, "enum"},
            {"Choice", CMETA_DATA_VARIANT, "union"},
            {"string", CMETA_DATA_STRING, "string"},
            {"bytes", CMETA_DATA_BYTES, "bytes"},
            {"datetime", CMETA_DATA_CUSTOM, "custom"},
            {"date", CMETA_DATA_CUSTOM, "custom"},
            {"time", CMETA_DATA_CUSTOM, "custom"},
            {"duration", CMETA_DATA_CUSTOM, "custom"},
            {"decimal", CMETA_DATA_CUSTOM, "custom"},
            {"money", CMETA_DATA_CUSTOM, "custom"},
            {"bigint", CMETA_DATA_CUSTOM, "custom"},
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            IdlField field = field_of(cases[i].name, IDL_COLLECTION_NONE);
            schema_cmeta_field_type type;
            check(schema_cmeta_field_resolve(&TEST_CONTRACT, &field, &type));
            check_equal(type.kind, cases[i].kind);
            check_equal(type.schema_kind, cases[i].label);
            if (cases[i].kind != CMETA_DATA_CUSTOM)
                check_null(type.data);
        }
    }

    it("rejects unknown field semantics atomically instead of a private custom fallback") {
        static const char *const names[] = {
            "MadeUp", "Option", "Pair", "Tuple", "Result", "Variant", "oneof",
            "pointer", "const_pointer", "Trait", "callable", "typed_any",
            "interface", "implements", "Range", "Collector", "effect", "property"
        };
        schema_cmeta_field_type out, before;
        size_t i;
        memset(&out, 0xa5, sizeof(out));
        memcpy(&before, &out, sizeof(out));
        for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            IdlField field = field_of(names[i], IDL_COLLECTION_NONE);
            check(!schema_cmeta_field_resolve(&TEST_CONTRACT, &field, &out));
            check_equal(memcmp(&out, &before, sizeof(out)), 0);
        }
        check(!schema_cmeta_field_resolve(NULL, NULL, &out));
        check_equal(memcmp(&out, &before, sizeof(out)), 0);
    }
}
