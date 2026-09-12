#include "schema_cmeta.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <string.h>

typedef struct schema_cmeta_builtin_entry {
    const char *name;
    const cmeta_data_desc *data;
} schema_cmeta_builtin_entry_t;

typedef struct schema_cmeta_kind_entry {
    const char *name;
    cmeta_data_kind kind;
} schema_cmeta_kind_entry_t;

#define SCHEMA_CMETA_ENTRY(name_, data_) {name_, &(data_)}

static const schema_cmeta_builtin_entry_t SCHEMA_CMETA_BUILTINS[] = {
    SCHEMA_CMETA_ENTRY("bool", cmeta_data_bool),
    SCHEMA_CMETA_ENTRY("int8_t", salts_int8_cmeta_data),
    SCHEMA_CMETA_ENTRY("int8", salts_int8_cmeta_data),
    SCHEMA_CMETA_ENTRY("i8", salts_int8_cmeta_data),
    SCHEMA_CMETA_ENTRY("int16_t", salts_int16_cmeta_data),
    SCHEMA_CMETA_ENTRY("int16", salts_int16_cmeta_data),
    SCHEMA_CMETA_ENTRY("i16", salts_int16_cmeta_data),
    SCHEMA_CMETA_ENTRY("int32_t", salts_int32_cmeta_data),
    SCHEMA_CMETA_ENTRY("int32", salts_int32_cmeta_data),
    SCHEMA_CMETA_ENTRY("i32", salts_int32_cmeta_data),
    SCHEMA_CMETA_ENTRY("int64_t", salts_int64_cmeta_data),
    SCHEMA_CMETA_ENTRY("int64", salts_int64_cmeta_data),
    SCHEMA_CMETA_ENTRY("i64", salts_int64_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint8_t", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint8", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("u8", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("byte", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint16_t", salts_uint16_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint16", salts_uint16_cmeta_data),
    SCHEMA_CMETA_ENTRY("u16", salts_uint16_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint32_t", salts_uint32_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint32", salts_uint32_cmeta_data),
    SCHEMA_CMETA_ENTRY("u32", salts_uint32_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint64_t", salts_uint64_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint64", salts_uint64_cmeta_data),
    SCHEMA_CMETA_ENTRY("u64", salts_uint64_cmeta_data),
    SCHEMA_CMETA_ENTRY("float", cmeta_data_float),
    SCHEMA_CMETA_ENTRY("f32", cmeta_data_float),
    SCHEMA_CMETA_ENTRY("double", cmeta_data_double),
    SCHEMA_CMETA_ENTRY("f64", cmeta_data_double),
    SCHEMA_CMETA_ENTRY("uuid", salts_uuid_cmeta_data),
};

#undef SCHEMA_CMETA_ENTRY

static const schema_cmeta_kind_entry_t SCHEMA_CMETA_KINDS[] = {
    {"bool", CMETA_DATA_BOOL},
    {"int8_t", CMETA_DATA_SINT}, {"int8", CMETA_DATA_SINT}, {"i8", CMETA_DATA_SINT},
    {"int16_t", CMETA_DATA_SINT}, {"int16", CMETA_DATA_SINT}, {"i16", CMETA_DATA_SINT},
    {"int32_t", CMETA_DATA_SINT}, {"int32", CMETA_DATA_SINT}, {"i32", CMETA_DATA_SINT},
    {"int64_t", CMETA_DATA_SINT}, {"int64", CMETA_DATA_SINT}, {"i64", CMETA_DATA_SINT},
    {"uint8_t", CMETA_DATA_UINT}, {"uint8", CMETA_DATA_UINT}, {"u8", CMETA_DATA_UINT},
    {"byte", CMETA_DATA_UINT},
    {"uint16_t", CMETA_DATA_UINT}, {"uint16", CMETA_DATA_UINT}, {"u16", CMETA_DATA_UINT},
    {"uint32_t", CMETA_DATA_UINT}, {"uint32", CMETA_DATA_UINT}, {"u32", CMETA_DATA_UINT},
    {"uint64_t", CMETA_DATA_UINT}, {"uint64", CMETA_DATA_UINT}, {"u64", CMETA_DATA_UINT},
    {"float", CMETA_DATA_FLOAT}, {"f32", CMETA_DATA_FLOAT},
    {"double", CMETA_DATA_FLOAT}, {"f64", CMETA_DATA_FLOAT},
    {"string", CMETA_DATA_STRING}, {"bytes", CMETA_DATA_BYTES},
    {"uuid", CMETA_DATA_CUSTOM}, {"datetime", CMETA_DATA_CUSTOM},
    {"date", CMETA_DATA_CUSTOM}, {"time", CMETA_DATA_CUSTOM},
    {"duration", CMETA_DATA_CUSTOM}, {"decimal", CMETA_DATA_CUSTOM},
    {"bigint", CMETA_DATA_CUSTOM}, {"money", CMETA_DATA_CUSTOM},
    {"message", CMETA_DATA_STRUCT}, {"composite", CMETA_DATA_STRUCT},
    {"group", CMETA_DATA_STRUCT}, {"enum", CMETA_DATA_ENUM},
    {"flags", CMETA_DATA_ENUM}, {"union", CMETA_DATA_VARIANT},
    {"list", CMETA_DATA_SEQUENCE}, {"set", CMETA_DATA_SET},
    {"map", CMETA_DATA_MAP},
};

static int schema_cmeta_nonempty(const char *text) {
    return text != NULL && text[0] != '\0';
}

const cmeta_data_desc *schema_cmeta_builtin_data(const char *name) {
    size_t i;

    if (name == NULL) return NULL;
    for (i = 0; i < sizeof(SCHEMA_CMETA_BUILTINS) / sizeof(SCHEMA_CMETA_BUILTINS[0]); ++i) {
        if (strcmp(name, SCHEMA_CMETA_BUILTINS[i].name) == 0)
            return SCHEMA_CMETA_BUILTINS[i].data;
    }
    return NULL;
}

int schema_cmeta_data_kind(const char *semantic, cmeta_data_kind *out_kind) {
    size_t i;

    if (semantic == NULL || out_kind == NULL) return 0;
    for (i = 0; i < sizeof(SCHEMA_CMETA_KINDS) / sizeof(SCHEMA_CMETA_KINDS[0]); ++i) {
        if (strcmp(semantic, SCHEMA_CMETA_KINDS[i].name) == 0) {
            *out_kind = SCHEMA_CMETA_KINDS[i].kind;
            return 1;
        }
    }
    return 0;
}

int schema_cmeta_generic_identity(cmeta_type_identity *out_identity,
                                  const cmeta_generic_desc *constructor,
                                  const cmeta_type_identity *const *args,
                                  size_t arity) {
    cmeta_type_identity identity;

    if (out_identity == NULL ||
        !cmeta_type_application_valid(constructor, args, arity))
        return 0;

    identity.form = CMETA_TYPE_APPLY;
    identity.stable_atom_id = NULL;
    identity.constructor = constructor;
    identity.base = NULL;
    identity.args = args;
    identity.arity = arity;

    if (!cmeta_type_identity_valid(&identity)) return 0;

    *out_identity = identity;
    return 1;
}

int schema_cmeta_struct_data(cmeta_data_desc *out_data,
                             cmeta_data_struct_shape *out_shape,
                             const char *stable_id,
                             const char *stable_display_name,
                             const cmeta_type_desc *storage_type,
                             const cmeta_struct_desc *layout,
                             const cmeta_data_field_desc *fields,
                             size_t field_count) {
    cmeta_data_struct_shape shape;
    cmeta_data_desc data;

    if (out_data == NULL || out_shape == NULL ||
        !schema_cmeta_nonempty(stable_id) || !schema_cmeta_nonempty(stable_display_name) ||
        storage_type == NULL || layout == NULL)
        return 0;

    shape.layout = layout;
    shape.fields = fields;
    shape.field_count = field_count;
    data.struct_size = sizeof(data);
    data.abi_version = CMETA_DATA_DESC_ABI_VERSION;
    data.stable_id = stable_id;
    data.display_name = stable_display_name;
    data.kind = CMETA_DATA_STRUCT;
    data.storage_type = storage_type;
    data.shape = &shape;
    data.buffer_ops = NULL;
    data.enum_ops = NULL;
    data.variant_ops = NULL;

    if (!cmeta_data_desc_valid(&data)) return 0;

    *out_shape = shape;
    data.shape = out_shape;
    *out_data = data;
    return 1;
}

int schema_cmeta_enum_data(cmeta_data_desc *out_data,
                           cmeta_data_enum_shape *out_shape,
                           const char *stable_id,
                           const char *display_name,
                           const cmeta_type_desc *storage_type,
                           const cmeta_enum_desc *meta) {
    cmeta_data_enum_shape shape;
    cmeta_data_desc data;

    if (out_data == NULL || out_shape == NULL ||
        !schema_cmeta_nonempty(stable_id) || !schema_cmeta_nonempty(display_name) ||
        storage_type == NULL || meta == NULL)
        return 0;

    shape.meta = meta;
    data.struct_size = sizeof(data);
    data.abi_version = CMETA_DATA_DESC_ABI_VERSION;
    data.stable_id = stable_id;
    data.display_name = display_name;
    data.kind = CMETA_DATA_ENUM;
    data.storage_type = storage_type;
    data.shape = &shape;
    data.buffer_ops = NULL;
    data.enum_ops = NULL;
    data.variant_ops = NULL;

    if (!cmeta_data_desc_valid(&data)) return 0;

    *out_shape = shape;
    data.shape = out_shape;
    *out_data = data;
    return 1;
}
