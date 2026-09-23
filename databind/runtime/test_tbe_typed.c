#include "tbe_typed.h"
#include "tinytest.h"

#include <salts_cmeta_fixed_width.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct TestPacket {
  uint16_t code;
  uint32_t count;
  tstr name;
  uint8_t presence[1];
} TestPacket;

static const TbeTypedField TEST_PACKET_FIELDS[] = {
    {.name = "code",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(TestPacket, code),
     .wire_offset = 1,
     .wire_size = 2,
     .optional_bit = 0,
     .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_WIRE_OFFSET},
    {.name = "count",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(TestPacket, count),
     .wire_offset = 3,
     .wire_size = 4,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET},
    {.name = "name",
     .kind = TBE_TYPED_STRING,
     .wire_kind = TBE_TYPED_STRING,
     .offset = offsetof(TestPacket, name),
     .flags = TBE_TYPED_FIELD_VAR_DATA}};

static const TbeTypedType TEST_PACKET_TYPE = {
    .name = "Packet",
    .size = sizeof(TestPacket),
    .fields = TEST_PACKET_FIELDS,
    .field_count = sizeof(TEST_PACKET_FIELDS) / sizeof(TEST_PACKET_FIELDS[0]),
    .fixed_block_size = 7,
    .presence_offset = offsetof(TestPacket, presence),
    .presence_size = 1,
    .wire_big_endian = 1};

TBE_TYPED_VEC_DEFINE(test_u32_vec_t, uint32_t)

typedef struct TestValues {
  test_u32_vec_t values;
} TestValues;

static const TbeTypedField TEST_VALUES_FIELDS[] = {{
    .name = "values",
    .kind = TBE_TYPED_LIST,
    .wire_kind = TBE_TYPED_LIST,
    .offset = offsetof(TestValues, values),
    .element_kind = TBE_TYPED_U32,
    .element_wire_kind = TBE_TYPED_U32,
    .element_size = sizeof(uint32_t),
}};

static const TbeTypedType TEST_VALUES_TYPE = {
    .name = "Values",
    .size = sizeof(TestValues),
    .fields = TEST_VALUES_FIELDS,
    .field_count = sizeof(TEST_VALUES_FIELDS) / sizeof(TEST_VALUES_FIELDS[0]),
};

typedef struct TestFixedValues {
  uint16_t values[2];
} TestFixedValues;

static const TbeTypedField TEST_FIXED_VALUES_FIELDS[] = {{
    .name = "values",
    .kind = TBE_TYPED_FIXED_ARRAY,
    .wire_kind = TBE_TYPED_FIXED_ARRAY,
    .offset = offsetof(TestFixedValues, values),
    .element_kind = TBE_TYPED_U16,
    .element_wire_kind = TBE_TYPED_U16,
    .element_size = sizeof(uint16_t),
    .fixed_count = 2,
    .wire_offset = 0,
    .wire_size = 4,
    .flags = TBE_TYPED_FIELD_WIRE_OFFSET,
}};

static const TbeTypedType TEST_FIXED_VALUES_TYPE = {
    .name = "FixedValues",
    .size = sizeof(TestFixedValues),
    .fields = TEST_FIXED_VALUES_FIELDS,
    .field_count = sizeof(TEST_FIXED_VALUES_FIELDS) / sizeof(TEST_FIXED_VALUES_FIELDS[0]),
    .fixed_block_size = 4,
};

typedef struct TestText {
  tstr text;
} TestText;

static const TbeTypedField TEST_TEXT_FIELDS[] = {{
    .name = "text",
    .kind = TBE_TYPED_STRING,
    .wire_kind = TBE_TYPED_STRING,
    .offset = offsetof(TestText, text),
    .flags = TBE_TYPED_FIELD_VAR_DATA,
}};

static const TbeTypedType TEST_TEXT_TYPE = {
    .name = "Text",
    .size = sizeof(TestText),
    .fields = TEST_TEXT_FIELDS,
    .field_count = sizeof(TEST_TEXT_FIELDS) / sizeof(TEST_TEXT_FIELDS[0]),
};

typedef struct TestWideEnum {
  uint64_t value;
} TestWideEnum;

static const TbeTypedField TEST_WIDE_ENUM_FIELDS[] = {{
    .name = "value",
    .kind = TBE_TYPED_ENUM,
    .wire_kind = TBE_TYPED_U64,
    .offset = offsetof(TestWideEnum, value),
    .wire_offset = 0,
    .wire_size = 8,
    .flags = TBE_TYPED_FIELD_WIRE_OFFSET,
}};

static const TbeTypedType TEST_WIDE_ENUM_TYPE = {
    .name = "WideRecord",
    .size = sizeof(TestWideEnum),
    .fields = TEST_WIDE_ENUM_FIELDS,
    .field_count = sizeof(TEST_WIDE_ENUM_FIELDS) / sizeof(TEST_WIDE_ENUM_FIELDS[0]),
    .fixed_block_size = 8,
};

typedef struct TestFloat32 {
  float value;
} TestFloat32;

static const TbeTypedField TEST_FLOAT32_FIELDS[] = {{
    .name = "value",
    .kind = TBE_TYPED_F32,
    .wire_kind = TBE_TYPED_F32,
    .offset = offsetof(TestFloat32, value),
}};

static const TbeTypedType TEST_FLOAT32_TYPE = {
    .name = "FloatRecord",
    .size = sizeof(TestFloat32),
    .fields = TEST_FLOAT32_FIELDS,
    .field_count = sizeof(TEST_FLOAT32_FIELDS) / sizeof(TEST_FLOAT32_FIELDS[0]),
};

typedef struct TestFloat64 {
  double value;
} TestFloat64;

static const TbeTypedField TEST_FLOAT64_FIELDS[] = {{
    .name = "value",
    .kind = TBE_TYPED_F64,
    .wire_kind = TBE_TYPED_F64,
    .offset = offsetof(TestFloat64, value),
}};

static const TbeTypedType TEST_FLOAT64_TYPE = {
    .name = "DoubleRecord",
    .size = sizeof(TestFloat64),
    .fields = TEST_FLOAT64_FIELDS,
    .field_count = sizeof(TEST_FLOAT64_FIELDS) / sizeof(TEST_FLOAT64_FIELDS[0]),
};

typedef struct TestBytes {
  tbe_bytes_t value;
} TestBytes;

static const TbeTypedField TEST_BYTES_FIELDS[] = {{
    .name = "value",
    .kind = TBE_TYPED_BYTES,
    .wire_kind = TBE_TYPED_BYTES,
    .offset = offsetof(TestBytes, value),
}};

static const TbeTypedType TEST_BYTES_TYPE = {
    .name = "BytesRecord",
    .size = sizeof(TestBytes),
    .fields = TEST_BYTES_FIELDS,
    .field_count = sizeof(TEST_BYTES_FIELDS) / sizeof(TEST_BYTES_FIELDS[0]),
};

TBE_TYPED_VEC_DEFINE(macro_u32_vec_t, uint32_t)

typedef struct MacroOrder {
  uint32_t order_id;
  tstr note;
  macro_u32_vec_t values;
  uint8_t presence[1];
} MacroOrder;

TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE(
    MACRO_ORDER_BINDING, MacroOrder, "MacroOrder", presence,
    TBE_TYPED_FIELD(MacroOrder, order_id, "id", TBE_TYPED_U32, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(MacroOrder, note, "note", TBE_TYPED_STRING, TBE_TYPED_OPTIONAL(0)),
    TBE_TYPED_LIST_FIELD(MacroOrder, values, "values", TBE_TYPED_U32, uint32_t, NULL,
                         TBE_TYPED_REQUIRED));

typedef struct InvalidMacroOrder {
  tstr note;
} InvalidMacroOrder;

TBE_TYPED_DEFINE_STRUCT(
    INVALID_MACRO_ORDER_BINDING, InvalidMacroOrder, "MacroOrder",
    TBE_TYPED_FIELD(InvalidMacroOrder, note, "note", TBE_TYPED_STRING,
                    TBE_TYPED_OPTIONAL(0)));

typedef struct MacroChild {
  uint16_t code;
} MacroChild;

TBE_TYPED_DEFINE_STRUCT(
    MACRO_CHILD_BINDING, MacroChild, "MacroChild",
    TBE_TYPED_FIELD(MacroChild, code, "code", TBE_TYPED_U16, TBE_TYPED_REQUIRED));

TBE_TYPED_VEC_DEFINE(macro_child_vec_t, MacroChild)

typedef struct MacroChildMapEntry {
  tstr key;
  MacroChild value;
} MacroChildMapEntry;

TBE_TYPED_VEC_DEFINE(macro_child_map_vec_t, MacroChildMapEntry)

typedef struct MacroCollections {
  MacroChild child;
  uint16_t fixed_values[2];
  uint8_t fixed_bytes[4];
  macro_child_vec_t children;
  macro_child_vec_t unique_children;
  macro_child_map_vec_t children_by_name;
} MacroCollections;

TBE_TYPED_DEFINE_STRUCT(
    MACRO_COLLECTIONS_BINDING, MacroCollections, "MacroCollections",
    TBE_TYPED_OBJECT_FIELD(MacroCollections, child, "child", &MACRO_CHILD_BINDING,
                           TBE_TYPED_REQUIRED),
    TBE_TYPED_FIXED_ARRAY_FIELD(MacroCollections, fixed_values, "fixed_values", TBE_TYPED_U16,
                                uint16_t, NULL, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIXED_BYTES_FIELD(MacroCollections, fixed_bytes, "fixed_bytes",
                                TBE_TYPED_REQUIRED),
    TBE_TYPED_LIST_FIELD(MacroCollections, children, "children", TBE_TYPED_OBJECT, MacroChild,
                         &MACRO_CHILD_BINDING, TBE_TYPED_REQUIRED),
    TBE_TYPED_SET_FIELD(MacroCollections, unique_children, "unique_children", TBE_TYPED_OBJECT,
                        MacroChild, &MACRO_CHILD_BINDING, TBE_TYPED_REQUIRED),
    TBE_TYPED_MAP_FIELD(MacroCollections, children_by_name, "children_by_name",
                        MacroChildMapEntry, key, value, TBE_TYPED_OBJECT, &MACRO_CHILD_BINDING,
                        TBE_TYPED_REQUIRED));

typedef struct MacroWire {
  uint32_t id;
} MacroWire;

TBE_TYPED_DEFINE_STRUCT_EX(
    MACRO_WIRE_BINDING, MacroWire, "MacroWire", 4u, 0u, 0u, 0,
    TBE_TYPED_FIELD_EX(MacroWire, id, "id", TBE_TYPED_U32, TBE_TYPED_U32, TBE_TYPED_BOOL,
                       TBE_TYPED_BOOL, 0u, 0u, NULL, 0u, 0u, 0u, TBE_TYPED_BOOL,
                       TBE_TYPED_BOOL, NULL, 0u, 4u, 0u, TBE_TYPED_FIELD_WIRE_OFFSET));

static const cmeta_type_identity MACRO_WIRE_CMETA_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.MacroWire");
static const cmeta_type_desc MACRO_WIRE_CMETA_TYPE = {
    "MacroWire", sizeof(MacroWire), _Alignof(MacroWire), CMETA_T_OBJECT,
    NULL, NULL, &MACRO_WIRE_CMETA_ID};
static const cmeta_field_desc MACRO_WIRE_CMETA_LAYOUT_FIELDS[] = {{
    "id", "uint32_t", offsetof(MacroWire, id), sizeof(uint32_t),
    _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc MACRO_WIRE_CMETA_LAYOUT = {
    "MacroWire", sizeof(MacroWire), _Alignof(MacroWire),
    MACRO_WIRE_CMETA_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc MACRO_WIRE_CMETA_FIELDS[] = {{
    "test.MacroWire.id", "id", offsetof(MacroWire, id),
    &salts_uint32_cmeta_data}};
static const cmeta_data_struct_shape MACRO_WIRE_CMETA_SHAPE = {
    &MACRO_WIRE_CMETA_LAYOUT, MACRO_WIRE_CMETA_FIELDS, 1u};
static const cmeta_data_desc MACRO_WIRE_CMETA_DATA = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.MacroWire.data", "MacroWire", CMETA_DATA_STRUCT,
    &MACRO_WIRE_CMETA_TYPE, &MACRO_WIRE_CMETA_SHAPE, NULL, NULL, NULL};
static const TbeTypedDescriptor MACRO_WIRE_DESCRIPTOR =
    TBE_TYPED_DESCRIPTOR_INIT(&MACRO_WIRE_BINDING, &MACRO_WIRE_CMETA_DATA);

spec("typed DataBind binary") {
  it("round-trips an optional big-endian owning struct directly") {
    static const char schema[] =
        "schema Direct [byte_order(big)]; "
        "message Packet { optional uint16 code; uint32 count; string name; }";
    static const uint8_t expected[] = {1,    0x12, 0x34, 0x01, 0x02, 0x03, 0x04,
                                       0x00, 0x00, 0x00, 0x03, 'A',  'B',  'C'};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestPacket source;
    TestPacket decoded;
    uint8_t *wire = NULL;
    size_t wire_len = 0;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_PACKET_TYPE, &source, &error), DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_PACKET_TYPE, &decoded, &error), DATA_BIND_OK);
    source.presence[0] = 1;
    source.code = UINT16_C(0x1234);
    source.count = UINT32_C(0x01020304);
    source.name = tstr_dup("ABC");
    check_not_null(source.name);

    if (codec != NULL && source.name != NULL) {
      check_equal(tbe_typed_validate_schema(codec, "Packet", &TEST_PACKET_TYPE, &error),
                   DATA_BIND_OK);
      check_equal(tbe_typed_serialize_binary(&TEST_PACKET_TYPE, &source, &wire, &wire_len, &error),
                   DATA_BIND_OK);
      check_equal(wire_len, sizeof(expected));
      check_equal(wire, expected, sizeof(expected));
      check_equal(tbe_typed_parse(codec, "Packet", &TEST_PACKET_TYPE, "bin", wire, wire_len, 0,
                                   &decoded, &error),
                   DATA_BIND_OK);
      check_equal(decoded.presence[0], 1u);
      check_equal(decoded.code, UINT16_C(0x1234));
      check_equal(decoded.count, UINT32_C(0x01020304));
      check_equal(decoded.name, "ABC");
    }

    tbe_typed_serialized_free(wire);
    tbe_typed_clear(&TEST_PACKET_TYPE, &decoded);
    tbe_typed_clear(&TEST_PACKET_TYPE, &source);
    data_bind_free(codec);
  }

  it("rejects a descriptor whose byte order differs from the schema") {
    static const char schema[] =
        "schema Direct [byte_order(big)]; "
        "message Packet { optional uint16 code; uint32 count; string name; }";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TbeTypedType wrong_order = TEST_PACKET_TYPE;

    wrong_order.wire_big_endian = 0;
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    if (codec != NULL)
      check_equal(tbe_typed_validate_schema(codec, "Packet", &wrong_order, &error),
                   DATA_BIND_ERR_SCHEMA);
    data_bind_free(codec);
  }

  it("preserves the owning struct when direct decoding is truncated") {
    static const uint8_t truncated[] = {1, 0x12, 0x34, 0, 0, 0, 7, 0, 0, 0, 4, 'x'};
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestPacket packet;

    check_equal(tbe_typed_init(&TEST_PACKET_TYPE, &packet, &error), DATA_BIND_OK);
    packet.presence[0] = 1;
    packet.code = 9;
    packet.count = 11;
    packet.name = tstr_dup("stable");
    check_not_null(packet.name);
    if (packet.name != NULL) {
      check_equal(tbe_typed_parse_binary(&TEST_PACKET_TYPE, truncated, sizeof(truncated), &packet,
                                          &error),
                   DATA_BIND_ERR_PARSE);
      check_equal(packet.presence[0], 1u);
      check_equal(packet.code, 9u);
      check_equal(packet.count, 11u);
      check_equal(packet.name, "stable");
    }
    tbe_typed_clear(&TEST_PACKET_TYPE, &packet);
  }

  it("rejects trailing bytes without replacing the destination object") {
    static const uint8_t wire[] = {0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                   0, 0,    0,    3,    'n',  'e',  'w',  0xff};
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestPacket packet;

    check_equal(tbe_typed_init(&TEST_PACKET_TYPE, &packet, &error), DATA_BIND_OK);
    packet.count = 11;
    packet.name = tstr_dup("stable");
    check_not_null(packet.name);
    if (packet.name != NULL) {
      check_equal(tbe_typed_parse_binary(&TEST_PACKET_TYPE, wire, sizeof(wire), &packet, &error),
                   DATA_BIND_ERR_PARSE);
      check_equal(packet.count, 11u);
      check_equal(packet.name, "stable");
    }
    tbe_typed_clear(&TEST_PACKET_TYPE, &packet);
  }

  it("round-trips uint64 enum values without narrowing") {
    static const char schema[] =
        "enum Wide <uint64> { Max = 18446744073709551615; } "
        "message WideRecord { Wide value; }";
    static const char json[] = "{\"value\":\"Max\"}";
    static const uint8_t expected[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestWideEnum value;
    TestWideEnum decoded;
    uint8_t *wire = NULL;
    size_t wire_len = 0;
    char *encoded_json = NULL;
    size_t json_len = 0;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_WIDE_ENUM_TYPE, &value, &error), DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_WIDE_ENUM_TYPE, &decoded, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(tbe_typed_parse(codec, "WideRecord", &TEST_WIDE_ENUM_TYPE, "json", json,
                                   sizeof(json) - 1, 0, &value, &error),
                   DATA_BIND_OK);
      check(value.value == UINT64_MAX);
      check_equal(tbe_typed_serialize_binary(&TEST_WIDE_ENUM_TYPE, &value, &wire, &wire_len,
                                              &error),
                   DATA_BIND_OK);
      check_equal(wire_len, sizeof(expected));
      check_equal(wire, expected, sizeof(expected));
      check_equal(tbe_typed_parse_binary(&TEST_WIDE_ENUM_TYPE, wire, wire_len, &decoded, &error),
                   DATA_BIND_OK);
      check(decoded.value == UINT64_MAX);
      check_equal(tbe_typed_serialize(codec, "WideRecord", &TEST_WIDE_ENUM_TYPE, &value, "json",
                                      &encoded_json, &json_len, &error),
                   DATA_BIND_OK);
      check_equal(encoded_json, "{\"value\":18446744073709551615}");
    }
    tbe_typed_serialized_free(encoded_json);
    tbe_typed_serialized_free(wire);
    tbe_typed_clear(&TEST_WIDE_ENUM_TYPE, &decoded);
    tbe_typed_clear(&TEST_WIDE_ENUM_TYPE, &value);
    data_bind_free(codec);
  }

  it("combines uint64 flags above INT64_MAX") {
    static const char schema[] =
        "flags WideFlags <uint64> { Low = 1; High = 9223372036854775808; } "
        "message WideFlagsRecord { WideFlags value; }";
    static const char json[] = "{\"value\":[\"Low\",\"High\"]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TbeTypedType flags_type = TEST_WIDE_ENUM_TYPE;
    TestWideEnum value;

    flags_type.name = "WideFlagsRecord";
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&flags_type, &value, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(tbe_typed_parse(codec, "WideFlagsRecord", &flags_type, "json", json,
                                   sizeof(json) - 1, 0, &value, &error),
                   DATA_BIND_OK);
      check(value.value == (UINT64_C(1) | (UINT64_C(1) << 63)));
    }
    tbe_typed_clear(&flags_type, &value);
    data_bind_free(codec);
  }

  it("rejects finite doubles outside the float32 range") {
    static const char schema[] = "message FloatRecord { float value; }";
    static const char json[] = "{\"value\":3.5e38}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestFloat32 value;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_FLOAT32_TYPE, &value, &error), DATA_BIND_OK);
    value.value = 1.0f;
    if (codec != NULL) {
      check_equal(tbe_typed_parse(codec, "FloatRecord", &TEST_FLOAT32_TYPE, "json", json,
                                   sizeof(json) - 1, 0, &value, &error),
                   DATA_BIND_ERR_TYPE_MISMATCH);
      check_within(value.value, 1.0f, 0.0f);
    }
    tbe_typed_clear(&TEST_FLOAT32_TYPE, &value);
    data_bind_free(codec);
  }

  it("rejects invalid UTF-8 bytes before creating JSON strings") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestBytes bytes;
    json_value_t *json;

    check_equal(tbe_typed_init(&TEST_BYTES_TYPE, &bytes, &error), DATA_BIND_OK);
    check_equal(vec_resize((vec_t *)&bytes.value, 2), STL_OK);
    if (tbe_bytes_t_data(&bytes.value) != NULL) {
      tbe_bytes_t_data(&bytes.value)[0] = UINT8_C(0xc3);
      tbe_bytes_t_data(&bytes.value)[1] = UINT8_C(0x28);
      json = tbe_typed_to_json(&TEST_BYTES_TYPE, &bytes, &error);
      check_null(json);
      check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
      tbe_typed_json_free(&json);
    }
    tbe_typed_clear(&TEST_BYTES_TYPE, &bytes);
  }

  it("converts dynamic values transactionally into initialized owning structs") {
    static const char schema[] =
        "message Text { string text; } message Number { uint32 text; }";
    static const char valid_json[] = "{\"text\":\"replacement\"}";
    static const char invalid_json[] = "{\"text\":7}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *valid = NULL;
    DataBindValue *invalid = NULL;
    TestText text;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_TEXT_TYPE, &text, &error), DATA_BIND_OK);
    text.text = tstr_dup("stable");
    check_not_null(text.text);
    if (codec != NULL && text.text != NULL) {
      check_equal(data_bind_parse_json(codec, "Text", valid_json, sizeof(valid_json) - 1,
                                        &valid, &error),
                   DATA_BIND_OK);
      check_equal(data_bind_parse_json(codec, "Number", invalid_json, sizeof(invalid_json) - 1,
                                        &invalid, &error),
                   DATA_BIND_OK);
      check_equal(tbe_typed_from_value(&TEST_TEXT_TYPE, valid, &text, &error), DATA_BIND_OK);
      check_equal(text.text, "replacement");
      check_equal(tbe_typed_from_value(&TEST_TEXT_TYPE, invalid, &text, &error),
                   DATA_BIND_ERR_TYPE_MISMATCH);
      check_equal(text.text, "replacement");
    }
    data_bind_value_free(invalid);
    data_bind_value_free(valid);
    tbe_typed_clear(&TEST_TEXT_TYPE, &text);
    data_bind_free(codec);
  }

  it("rejects invalid UTF-8 strings non-finite numbers and invalid map keys in JSON") {
    static const char invalid_utf8[] = "\xC3\x28";
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestText text;
    TestFloat32 number;
    TestFloat64 double_number;
    MacroCollections collections;
    json_value_t *json = NULL;

    check_equal(tbe_typed_init(&TEST_TEXT_TYPE, &text, &error), DATA_BIND_OK);
    text.text = tstr_dup_len(invalid_utf8, sizeof(invalid_utf8) - 1u);
    check_not_null(text.text);
    if (text.text != NULL) {
      json = tbe_typed_to_json(&TEST_TEXT_TYPE, &text, &error);
      check_null(json);
      check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    }
    tbe_typed_json_free(&json);
    tbe_typed_clear(&TEST_TEXT_TYPE, &text);

    check_equal(tbe_typed_init(&TEST_FLOAT32_TYPE, &number, &error), DATA_BIND_OK);
    number.value = NAN;
    json = tbe_typed_to_json(&TEST_FLOAT32_TYPE, &number, &error);
    check_null(json);
    check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    tbe_typed_json_free(&json);
    tbe_typed_clear(&TEST_FLOAT32_TYPE, &number);

    check_equal(tbe_typed_init(&TEST_FLOAT64_TYPE, &double_number, &error), DATA_BIND_OK);
    double_number.value = INFINITY;
    json = tbe_typed_to_json(&TEST_FLOAT64_TYPE, &double_number, &error);
    check_null(json);
    check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    tbe_typed_json_free(&json);
    tbe_typed_clear(&TEST_FLOAT64_TYPE, &double_number);

    check_equal(TBE_TYPED_BIND_INIT(MACRO_COLLECTIONS_BINDING, &collections, &error),
                 DATA_BIND_OK);
    {
      MacroChildMapEntry entry = {0};
      int push_status;
      entry.key = tstr_dup_len(invalid_utf8, sizeof(invalid_utf8) - 1u);
      check_not_null(entry.key);
      if (entry.key != NULL) {
        push_status = macro_child_map_vec_t_push(&collections.children_by_name, entry);
        check_equal(push_status, SALTS_OK);
        if (push_status != SALTS_OK) tstr_free(entry.key);
      }
    }
    check_equal(macro_child_map_vec_t_size(&collections.children_by_name), 1u);
    if (macro_child_map_vec_t_size(&collections.children_by_name) == 1u) {
      MacroChildMapEntry *entry = macro_child_map_vec_t_at(&collections.children_by_name, 0u);
      check_not_null(entry->key);
      if (entry->key != NULL) {
        json = tbe_typed_to_json(&MACRO_COLLECTIONS_BINDING, &collections, &error);
        check_null(json);
        check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
      }
      tbe_typed_json_free(&json);
    }
    TBE_TYPED_BIND_CLEAR(MACRO_COLLECTIONS_BINDING, &collections);
  }

  it("validates the fixed wire layout before writing output") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    TbeTypedField fields[3];
    TbeTypedType invalid = TEST_PACKET_TYPE;
    TestPacket packet;
    uint8_t output[32];
    uint8_t expected[32];
    size_t out_len = 99;

    memcpy(fields, TEST_PACKET_FIELDS, sizeof(fields));
    fields[1].wire_offset = TEST_PACKET_TYPE.fixed_block_size;
    invalid.fields = fields;
    memset(output, 0xa5, sizeof(output));
    memcpy(expected, output, sizeof(output));
    memset(&packet, 0, sizeof(packet));

    check_equal(tbe_typed_serialize_binary_into(&invalid, &packet, output, sizeof(output),
                                                 &out_len, &error),
                 DATA_BIND_ERR_SCHEMA);
    check_equal(out_len, 0u);
    check_equal(output, expected, sizeof(output));
  }

  it("keeps variable collections compatible through the typed struct API") {
    static const char schema[] = "message Values { list<uint32> values; }";
    static const uint8_t wire[] = {2, 0, 0, 0, 7, 0, 0, 0, 9, 0, 0, 0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestValues values;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_VALUES_TYPE, &values, &error), DATA_BIND_OK);
    if (codec != NULL) {
      const uint32_t *items;
      check_equal(tbe_typed_validate_schema(codec, "Values", &TEST_VALUES_TYPE, &error),
                   DATA_BIND_OK);
      check_equal(tbe_typed_parse(codec, "Values", &TEST_VALUES_TYPE, "bin", wire, sizeof(wire),
                                   0, &values, &error),
                   DATA_BIND_OK);
      check_equal(test_u32_vec_t_size(&values.values), 2u);
      items = test_u32_vec_t_data_const(&values.values);
      if (test_u32_vec_t_size(&values.values) == 2u) {
        check_equal(items[0], 7u);
        check_equal(items[1], 9u);
      }
    }
    tbe_typed_clear(&TEST_VALUES_TYPE, &values);
    data_bind_free(codec);
  }

  it("matches and decodes fixed array schema fields") {
    static const char schema[] = "message FixedValues { uint16[2] values; }";
    static const uint8_t wire[] = {0x34, 0x12, 0xcd, 0xab};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestFixedValues values;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_FIXED_VALUES_TYPE, &values, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(tbe_typed_validate_schema(codec, "FixedValues", &TEST_FIXED_VALUES_TYPE,
                                             &error),
                   DATA_BIND_OK);
      check_equal(tbe_typed_parse(codec, "FixedValues", &TEST_FIXED_VALUES_TYPE, "bin", wire,
                                   sizeof(wire), 0, &values, &error),
                   DATA_BIND_OK);
      check_equal(values.values[0], UINT16_C(0x1234));
      check_equal(values.values[1], UINT16_C(0xabcd));
    }
    tbe_typed_clear(&TEST_FIXED_VALUES_TYPE, &values);
    data_bind_free(codec);
  }

  it("directly decodes a variable-data-only record with a zero fixed block") {
    static const uint8_t wire[] = {3, 0, 0, 0, 'a', 'b', 'c'};
    DataBindError error = DATA_BIND_ERROR_INIT;
    TestText text;

    check_equal(tbe_typed_init(&TEST_TEXT_TYPE, &text, &error), DATA_BIND_OK);
    check_equal(tbe_typed_parse_binary(&TEST_TEXT_TYPE, wire, sizeof(wire), &text, &error),
                 DATA_BIND_OK);
    check_equal(text.text, "abc");
    tbe_typed_clear(&TEST_TEXT_TYPE, &text);
  }

  it("serializes typed records with schema field mappings") {
    static const char schema[] =
        "message Text { [name(displayText), alias(oldText)] string text; }";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    TestText text;
    char *json = NULL;
    size_t json_len = 0;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(tbe_typed_init(&TEST_TEXT_TYPE, &text, &error), DATA_BIND_OK);
    text.text = tstr_dup("mapped");
    check_not_null(text.text);
    if (codec != NULL && text.text != NULL) {
      check_equal(tbe_typed_serialize(codec, "Text", &TEST_TEXT_TYPE, &text, "json", &json,
                                      &json_len, &error),
                   DATA_BIND_OK);
      check_equal(json, "{\"displayText\":\"mapped\"}");
      check_equal(json_len, strlen(json));
    }
    tbe_typed_serialized_free(json);
    tbe_typed_clear(&TEST_TEXT_TYPE, &text);
    data_bind_free(codec);
  }

  it("binds an existing C struct through header-only macros") {
    static const char schema[] =
        "message MacroOrder { "
        "[name(orderId), alias(legacyId)] uint32 id; "
        "optional string note; "
        "list<uint32> values; "
        "}";
    static const char json[] = "{\"legacyId\":42,\"note\":\"macro\",\"values\":[7,9]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    MacroOrder order;
    const uint32_t *values;
    char *mapped = NULL;
    size_t mapped_len = 0;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    check_equal(TBE_TYPED_BIND_INIT(MACRO_ORDER_BINDING, &order, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(TBE_TYPED_BIND_PARSE(codec, MACRO_ORDER_BINDING, "json", json,
                                        sizeof(json) - 1, 0, &order, &error),
                   DATA_BIND_OK);
      check_equal(order.order_id, 42u);
      check_equal(order.note, "macro");
      check_equal(order.presence[0], 1u);
      check_equal(macro_u32_vec_t_size(&order.values), 2u);
      values = macro_u32_vec_t_data_const(&order.values);
      if (macro_u32_vec_t_size(&order.values) == 2u) {
        check_equal(values[0], 7u);
        check_equal(values[1], 9u);
      }
      check_equal(TBE_TYPED_BIND_SERIALIZE(codec, MACRO_ORDER_BINDING, &order, "json", &mapped,
                                           &mapped_len, &error),
                   DATA_BIND_OK);
      check_equal(mapped, "{\"orderId\":42,\"note\":\"macro\",\"values\":[7,9]}");
      check_equal(mapped_len, strlen(mapped));
    }
    tbe_typed_serialized_free(mapped);
    TBE_TYPED_BIND_CLEAR(MACRO_ORDER_BINDING, &order);
    data_bind_free(codec);
  }

  it("rejects an optional macro field without a presence bitmap") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    InvalidMacroOrder order;

    check_equal(TBE_TYPED_BIND_INIT(INVALID_MACRO_ORDER_BINDING, &order, &error),
                 DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "presence bitmap");
  }

  it("validates all composite macro field families") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    MacroCollections collections;

    check_equal(tbe_typed_validate_descriptor(&MACRO_COLLECTIONS_BINDING, &error),
                 DATA_BIND_OK);
    check_equal(TBE_TYPED_BIND_INIT(MACRO_COLLECTIONS_BINDING, &collections, &error),
                 DATA_BIND_OK);
    check_equal(macro_child_vec_t_size(&collections.children), 0u);
    check_equal(macro_child_vec_t_size(&collections.unique_children), 0u);
    check_equal(macro_child_map_vec_t_size(&collections.children_by_name), 0u);
    TBE_TYPED_BIND_CLEAR(MACRO_COLLECTIONS_BINDING, &collections);
  }

  it("uses explicit macro metadata for direct binary layout") {
    static const uint8_t expected[] = {0x78, 0x56, 0x34, 0x12};
    DataBindError error = DATA_BIND_ERROR_INIT;
    MacroWire wire = {UINT32_C(0x12345678)};
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;

    check_equal(tbe_typed_serialize_binary(&MACRO_WIRE_BINDING, &wire, &encoded, &encoded_len,
                                            &error),
                 DATA_BIND_OK);
    check_equal(encoded_len, sizeof(expected));
    check_equal(encoded, expected, sizeof(expected));
    tbe_typed_serialized_free(encoded);
  }

  it("queries binary capacity and reports a distinct short-buffer status") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    MacroWire wire = {UINT32_C(0x12345678)};
    uint8_t output[4] = {0};
    size_t required = 0;

    check_equal(tbe_typed_serialize_binary_into(&MACRO_WIRE_BINDING, &wire, NULL, 0, &required,
                                                 &error),
                 DATA_BIND_ERR_BUFFER_TOO_SMALL);
    check_equal(required, sizeof(output));
    check_equal(tbe_typed_serialize_binary_into(&MACRO_WIRE_BINDING, &wire, output,
                                                 sizeof(output), &required, &error),
                 DATA_BIND_OK);
    check_equal(required, sizeof(output));
  }

  it("validates versioned descriptors and enum-based format APIs") {
    static const char schema[] = "message MacroWire { uint32 id; }";
    static const char json[] = "{\"id\":7}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    MacroWire wire = {0};
    char *encoded = NULL;
    size_t encoded_len = 0;
    TbeTypedDescriptor incompatible = MACRO_WIRE_DESCRIPTOR;

    check_equal(tbe_typed_descriptor_validate(&MACRO_WIRE_DESCRIPTOR, &error),
                 DATA_BIND_OK);
    incompatible.abi_version++;
    check_equal(tbe_typed_descriptor_validate(&incompatible, &error), DATA_BIND_ERR_SCHEMA);
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error),
                 DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(tbe_typed_descriptor_parse(codec, "MacroWire",
                                              &MACRO_WIRE_DESCRIPTOR,
                                              DATA_BIND_FORMAT_JSON, json, sizeof(json) - 1u, 0,
                                              &wire, &error),
                   DATA_BIND_OK);
      check_equal(wire.id, 7u);
      check_equal(tbe_typed_descriptor_serialize(codec, "MacroWire",
                                                  &MACRO_WIRE_DESCRIPTOR, &wire,
                                                  DATA_BIND_FORMAT_JSON, &encoded, &encoded_len,
                                                  &error),
                   DATA_BIND_OK);
      check_equal(encoded, json);
    }
    tbe_typed_serialized_free(encoded);
    data_bind_free(codec);
  }
}
