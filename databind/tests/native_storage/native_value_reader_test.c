#include "data_bind_value_reader.h"
#include "data_bind_message_plan.h"
#include "native_test_alignment.h"

#include <cstl/byte_buffer.h>
#include <cstl/typed.h>
#include <salts_cmeta_data.h>
#include <tinytest.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { VALUE_FIELDS = 3, VALUE_WORKSPACE = 4096, VALUE_DEPTH = 8,
       VALUE_ITEMS = 64, VALUE_OWNED_BYTES = 128, LIST_FAILURE_MAX_ITEMS = 2 };
typedef struct ValueRecord { uint32_t id; tstr text; stl_byte_buffer blob; } ValueRecord;
static const cmeta_type_identity VALUE_ID = CMETA_TYPE_ID_ATOM_INIT("test.value-reader.Record");
static const cmeta_type_desc VALUE_TYPE = {
    .name = "ValueRecord", .size = sizeof(ValueRecord), .align = _Alignof(ValueRecord),
    .kind = CMETA_T_OBJECT, .identity = &VALUE_ID};
static cmeta_field_desc layout_fields[] = {
    {"id", "uint32_t", offsetof(ValueRecord, id), sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"text", "tstr", offsetof(ValueRecord, text), sizeof(tstr), _Alignof(tstr), NULL, NULL},
    {"blob", "stl_byte_buffer", offsetof(ValueRecord, blob), sizeof(stl_byte_buffer), _Alignof(stl_byte_buffer), NULL, NULL}};
static const cmeta_struct_desc VALUE_LAYOUT = {
    "ValueRecord", sizeof(ValueRecord), _Alignof(ValueRecord), layout_fields, VALUE_FIELDS};
static cmeta_data_field_desc value_fields[] = {
    {"test.value-reader.id", "id", offsetof(ValueRecord, id), &cmeta_data_uint32},
    {"test.value-reader.text", "text", offsetof(ValueRecord, text), NULL},
    {"test.value-reader.blob", "blob", offsetof(ValueRecord, blob), NULL}};
static const cmeta_data_struct_shape VALUE_SHAPE = {&VALUE_LAYOUT, value_fields, VALUE_FIELDS};
static const cmeta_data_desc VALUE_DATA = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.value-reader.Record.data", .display_name = "ValueRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &VALUE_TYPE, .shape = &VALUE_SHAPE};
static const DataBindNativeTypeBinding VALUE_BINDING =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("Record", &VALUE_DATA);

typed(Vec, ValueList, uint32_t, &cmeta_type_uint32, &cmeta_data_uint32);
typedef struct ValueListRecord { ValueList values; } ValueListRecord;
static const cmeta_type_identity LIST_ID = CMETA_TYPE_ID_ATOM_INIT("test.value-reader.Values");
static const cmeta_type_desc LIST_TYPE = {
    .name = "ValueListRecord", .size = sizeof(ValueListRecord), .align = _Alignof(ValueListRecord),
    .kind = CMETA_T_OBJECT, .identity = &LIST_ID};
static const cmeta_field_desc list_layout_fields[] = {
    {"values", "ValueList", offsetof(ValueListRecord, values), sizeof(ValueList),
     _Alignof(ValueList), &ValueList_cmeta_type, NULL}};
static const cmeta_struct_desc LIST_LAYOUT = {
    "ValueListRecord", sizeof(ValueListRecord), _Alignof(ValueListRecord), list_layout_fields, 1u};
static const cmeta_data_field_desc list_fields[] = {
    {"test.value-reader.Values.values", "values", offsetof(ValueListRecord, values),
     &ValueList_collection_data}};
static const cmeta_data_struct_shape LIST_SHAPE = {&LIST_LAYOUT, list_fields, 1u};
static const cmeta_data_desc LIST_DATA = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.value-reader.Values.data", .display_name = "ValueListRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &LIST_TYPE, .shape = &LIST_SHAPE};
static const DataBindNativeTypeBinding LIST_BINDING =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("Values", &LIST_DATA);
/* Exact wire from the historical dynamic-to-typed list regression. */
static const uint8_t LIST_WIRE[] = {2, 0, 0, 0, 7, 0, 0, 0, 9, 0, 0, 0};
static const char SCHEMA[] =
    "message Values { list<uint32> values; }"
    "message Record { uint32 id; [alias(oldText)] string text; bytes blob; }"
    "message Broken { uint32 id; string text; string blob; }"
    "message Tokens { int64 signed_value; uint64 unsigned_value; bool flag; double real;"
    " uuid uid; duration span; list<uint32> values; set<uint32> unique_values;"
    " map<string,string> labels; nullable string nil; }"
    "message Domains { datetime at; date day; time clock; duration span; decimal price; bigint count; money cost; }"
    "message Child { uint32 id; } message Nested { list<Child> children; }";
static const char SEED[] = "{\"id\":1,\"text\":\"stable\",\"blob\":\"seed\"}";
static const char TOKENS[] =
    "{\"signed_value\":-9223372036854775808,\"unsigned_value\":18446744073709551615,"
    "\"flag\":true,\"real\":1.5,\"nil\":null,\"uid\":\"123e4567-e89b-12d3-a456-426614174000\","
    "\"values\":[3,4],\"unique_values\":[3,3,4],\"labels\":{\"-7\":\"minus\",\"8\":\"plus\"},\"span\":\"25ms\"}";
static DataBind *codec;
static DataBindMessagePlan *plan;
static DataBindMessagePlan *list_plan;
static DataBindError error;
static DataBindMessagePlanDiagnostic diagnostic;
static DataBindNativeOptions native_options;
static DataBindValueReaderLimits limits;
static union { DataBindNativeTestAlignment alignment; unsigned char bytes[VALUE_WORKSPACE]; } workspace;
static ValueRecord published;
static ValueListRecord list_published;

static DataBindValue *parse(const char *type, const char *json) {
  DataBindValue *root = NULL;
  DataBindStatus status = data_bind_parse_json(codec, type, json, strlen(json), &root, &error);
  check(status == DATA_BIND_OK, "%s", error.message);
  return root;
}
static cserde_reader *open_value(const DataBindValue *value) {
  cserde_reader *reader = NULL;
  check_equal(data_bind_value_reader_open(value, &limits, &reader, &error), DATA_BIND_OK);
  return reader;
}
static cserde_token next_kind(cserde_reader *reader, cserde_token_kind kind) {
  cserde_token token = {0};
  check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
  check_equal(token.kind, kind);
  return token;
}
static void end_reader(cserde_reader *reader) {
  cserde_token token;
  check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
  check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
  data_bind_value_reader_close(reader);
}
static cserde_status drain(const DataBindValue *value) {
  cserde_reader *reader = open_value(value);
  cserde_token token;
  cserde_status status;
  do { status = cserde_reader_next(reader, &token); } while (status == CSERDE_OK);
  check_equal(cserde_reader_next(reader, &token), status);
  data_bind_value_reader_close(reader);
  return status;
}
/* The caller owns both staging and publication. Closing the borrowed reader
 * precedes moving provider-owned buffers; failure never alters the old owner. */
static DataBindStatus replace(const DataBindValue *root) {
  ValueRecord staging;
  cserde_reader *reader = NULL;
  if (cmeta_data_value_init_zero(&VALUE_DATA, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  DataBindStatus status = data_bind_value_reader_open(root, &limits, &reader, &error);
  if (status == DATA_BIND_OK)
    status = data_bind_message_plan_decode_native(plan, &native_options, reader,
                                                 &staging, sizeof(staging), &diagnostic);
  data_bind_value_reader_close(reader);
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(&VALUE_DATA, &published);
    cmeta_data_trait_move_construct(&VALUE_DATA, &published, &staging);
  } else {
    check_equal(staging.id, 0u);
    check_null(staging.text);
    check_equal(stl_byte_buffer_size(&staging.blob), 0u);
  }
  cmeta_data_value_destroy(&VALUE_DATA, &staging);
  return status;
}

static DataBindStatus replace_list(const DataBindValue *root) {
  ValueListRecord staging;
  cserde_reader *reader = NULL;
  if (cmeta_data_value_init_zero(&LIST_DATA, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  DataBindStatus status = data_bind_value_reader_open(root, &limits, &reader, &error);
  if (status == DATA_BIND_OK)
    status = data_bind_message_plan_decode_native(list_plan, &native_options, reader,
                                                 &staging, sizeof(staging), &diagnostic);
  data_bind_value_reader_close(reader);
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(&LIST_DATA, &list_published);
    cmeta_data_trait_move_construct(&LIST_DATA, &list_published, &staging);
  } else {
    check_equal(ValueList_size(&staging.values), 0u);
  }
  cmeta_data_value_destroy(&LIST_DATA, &staging);
  return status;
}

spec("DataBind borrowed canonical dynamic value reader") {
  before_all() {
    layout_fields[1].type = salts_tstr_cmeta_data.storage_type;
    layout_fields[2].type = stl_byte_buffer_cmeta_data.storage_type;
    value_fields[1].value = &salts_tstr_cmeta_data;
    value_fields[2].value = &stl_byte_buffer_cmeta_data;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindStatus create_status = data_bind_create_from_text(SCHEMA, sizeof(SCHEMA) - 1u, &codec, &error);
    check(create_status == DATA_BIND_OK, "%s", error.message);
    DataBindStatus status = data_bind_message_plan_compile(codec, "Record", &VALUE_BINDING, &plan, &diagnostic);
    check(status == DATA_BIND_OK, "%s", diagnostic.message);
    status = data_bind_message_plan_compile(codec, "Values", &LIST_BINDING, &list_plan, &diagnostic);
    check(status == DATA_BIND_OK, "%s", diagnostic.message);
  }
  before_each() {
    limits = (DataBindValueReaderLimits)DATA_BIND_VALUE_READER_LIMITS_INIT;
    native_options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    native_options.workspace = workspace.bytes;
    native_options.workspace_bytes = sizeof(workspace.bytes);
    native_options.max_depth = VALUE_DEPTH;
    native_options.max_items = VALUE_ITEMS;
    native_options.max_owned_bytes = VALUE_OWNED_BYTES;
    check_equal(cmeta_data_value_init_zero(&VALUE_DATA, &published), CMETA_OK);
    check_equal(cmeta_data_value_init_zero(&LIST_DATA, &list_published), CMETA_OK);
    DataBindValue *root = parse("Record", SEED);
    check_equal(replace(root), DATA_BIND_OK);
    data_bind_value_free(root);
  }
  after_each() {
    cmeta_data_value_destroy(&LIST_DATA, &list_published);
    cmeta_data_value_destroy(&VALUE_DATA, &published);
  }
  after_all() {
    data_bind_message_plan_free(list_plan);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("preserves legacy Binary list wire values through canonical owner publication") {
    DataBindValue *root = NULL;
    check_equal(data_bind_parse(codec, "Values", LIST_WIRE, sizeof(LIST_WIRE), &root, &error), DATA_BIND_OK);
    check_equal(replace_list(root), DATA_BIND_OK);
    data_bind_value_free(root);
    check_equal(ValueList_size(&list_published.values), 2u);
    check_equal(*ValueList_at_const(&list_published.values, 0u), 7u);
    check_equal(*ValueList_at_const(&list_published.values, 1u), 9u);
  }
  it("retains list ownership on bounded conversion failure and permits retry") {
    DataBindValue *root = NULL;
    check_equal(data_bind_parse(codec, "Values", LIST_WIRE, sizeof(LIST_WIRE), &root, &error), DATA_BIND_OK);
    check_equal(replace_list(root), DATA_BIND_OK);
    const uint32_t *owner = ValueList_data_const(&list_published.values);
    native_options.max_items = LIST_FAILURE_MAX_ITEMS;
    check_equal(replace_list(root), DATA_BIND_ERR_LIMIT);
    check(ValueList_data_const(&list_published.values) == owner);
    check_equal(ValueList_size(&list_published.values), 2u);
    native_options.max_items = VALUE_ITEMS;
    check_equal(replace_list(root), DATA_BIND_OK);
    data_bind_value_free(root);
    check_equal(*ValueList_at_const(&list_published.values, 1u), 9u);
  }

  it("replaces both owners and preserves embedded NUL after source destruction") {
    DataBindValue *root = parse("Record", "{\"id\":7,\"oldText\":\"A\\u0000B\",\"blob\":\"C\\u0000D\"}");
    const char *source_text = NULL;
    const uint8_t *source_bytes = NULL;
    size_t text_size = 0u, byte_size = 0u;
    check_equal(data_bind_value_get_string(data_bind_value_get(root, "text"), &source_text, &text_size), DATA_BIND_OK);
    check_equal(data_bind_value_get_bytes(data_bind_value_get(root, "blob"), &source_bytes, &byte_size), DATA_BIND_OK);
    check_equal(replace(root), DATA_BIND_OK);
    check(published.text != source_text);
    check(stl_byte_buffer_data_const(&published.blob) != source_bytes);
    data_bind_value_free(root);
    check_equal(published.id, 7u);
    check_equal(tstr_len(published.text), 3u);
    check_equal(published.text, "A\0B", 3u);
    check_equal(stl_byte_buffer_size(&published.blob), 3u);
    check_equal(stl_byte_buffer_data_const(&published.blob), "C\0D", 3u);
  }
  it("rolls back staged text when a later bytes field has the wrong type") {
    DataBindValue *root = parse("Broken", "{\"id\":7,\"text\":\"replacement\",\"blob\":\"wrong type\"}");
    tstr owner = published.text;
    const unsigned char *blob_owner = stl_byte_buffer_data_const(&published.blob);
    check_equal(replace(root), DATA_BIND_ERR_TYPE_MISMATCH);
    check(published.text == owner);
    check(stl_byte_buffer_data_const(&published.blob) == blob_owner);
    check_equal(published.text, "stable");
    check_equal(published.id, 1u);
    data_bind_value_free(root);
  }
  it("preserves owners after reader budget exhaustion and allows a fresh retry") {
    DataBindValue *root = parse("Record", SEED);
    tstr owner = published.text;
    limits.max_items = VALUE_FIELDS;
    check_equal(replace(root), DATA_BIND_ERR_LIMIT);
    check(published.text == owner);
    limits.max_items = VALUE_FIELDS + 1u;
    check_equal(replace(root), DATA_BIND_OK);
    data_bind_value_free(root);
  }
  it("preserves owners after native allocation budget rejection") {
    DataBindValue *root = parse("Record", SEED);
    tstr owner = published.text;
    native_options.max_owned_bytes = 1u;
    check_equal(replace(root), DATA_BIND_ERR_LIMIT);
    check(published.text == owner);
    native_options.max_owned_bytes = VALUE_OWNED_BYTES;
    check_equal(replace(root), DATA_BIND_OK);
    data_bind_value_free(root);
  }
  it("releases old owners when empty values replace them") {
    DataBindValue *root = parse("Record", "{\"id\":2,\"text\":\"\",\"blob\":\"\"}");
    check_equal(replace(root), DATA_BIND_OK);
    data_bind_value_free(root);
    check_equal(tstr_len(published.text), 0u);
    check_equal(stl_byte_buffer_size(&published.blob), 0u);
  }
  it("keeps exact integer signedness and full 64 bit extremes") {
    DataBindValue *root = parse("Tokens", TOKENS);
    cserde_reader *reader = open_value(data_bind_value_get(root, "signed_value"));
    check_equal(next_kind(reader, CSERDE_SINT).value.sint, INT64_MIN);
    end_reader(reader);
    reader = open_value(data_bind_value_get(root, "unsigned_value"));
    check_equal(next_kind(reader, CSERDE_UINT).value.uint, UINT64_MAX);
    end_reader(reader);
    reader = open_value(data_bind_value_get(root, "flag"));
    check_true(next_kind(reader, CSERDE_BOOL).value.boolean);
    end_reader(reader);
    reader = open_value(data_bind_value_get(root, "real"));
    check_equal(next_kind(reader, CSERDE_FLOAT).value.floating, 1.5);
    end_reader(reader);
    reader = open_value(data_bind_value_get(root, "nil"));
    next_kind(reader, CSERDE_NULL);
    end_reader(reader);
    data_bind_value_free(root);
  }
  it("emits UUID as canonical bytes without a text round trip") {
    DataBindValue *root = parse("Tokens", TOKENS);
    const DataBindValue *value = data_bind_value_get(root, "uid");
    uint8_t expected[DATA_BIND_UUID_SIZE];
    check_equal(data_bind_value_get_uuid(value, expected), DATA_BIND_OK);
    cserde_reader *reader = open_value(value);
    cserde_token token = next_kind(reader, CSERDE_BYTES);
    check_equal(token.value.slice.size, sizeof(expected));
    check_equal(token.value.slice.data, expected, sizeof(expected));
    check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);
    end_reader(reader);
    data_bind_value_free(root);
  }
  it("preserves list order and ordered set uniqueness") {
    DataBindValue *root = parse("Tokens", TOKENS);
    const char *names[] = {"values", "unique_values"};
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
      cserde_reader *reader = open_value(data_bind_value_get(root, names[i]));
      next_kind(reader, CSERDE_ARRAY_BEGIN);
      check_equal(next_kind(reader, CSERDE_SINT).value.sint, 3);
      check_equal(next_kind(reader, CSERDE_SINT).value.sint, 4);
      next_kind(reader, CSERDE_ARRAY_END);
      end_reader(reader);
    }
    data_bind_value_free(root);
  }
  it("preserves stored map key tokens and insertion order") {
    DataBindValue *root = parse("Tokens", TOKENS);
    cserde_reader *reader = open_value(data_bind_value_get(root, "labels"));
    next_kind(reader, CSERDE_MAP_BEGIN);
    cserde_token key = next_kind(reader, CSERDE_STRING);
    check_equal(key.value.slice.size, 2u);
    check_equal(key.value.slice.data, "-7", key.value.slice.size);
    cserde_token token = next_kind(reader, CSERDE_STRING);
    check_equal(token.value.slice.data, "minus", token.value.slice.size);
    key = next_kind(reader, CSERDE_STRING);
    check_equal(key.value.slice.size, 1u);
    check_equal(key.value.slice.data, "8", key.value.slice.size);
    token = next_kind(reader, CSERDE_STRING);
    check_equal(token.value.slice.data, "plus", token.value.slice.size);
    next_kind(reader, CSERDE_MAP_END);
    end_reader(reader);
    data_bind_value_free(root);
  }
  it("bounds nested containers at the exact depth boundary") {
    DataBindValue *root = parse("Nested", "{\"children\":[{\"id\":1}]}");
    limits.max_depth = 2u;
    check_equal(drain(root), CSERDE_LIMIT_EXCEEDED);
    limits.max_depth = 3u;
    check_equal(drain(root), CSERDE_DONE);
    data_bind_value_free(root);
  }
  it("counts typed map keys as nodes but excludes end tokens") {
    DataBindValue *root = parse("Tokens", TOKENS);
    limits.max_items = 4u;
    check_equal(drain(data_bind_value_get(root, "labels")), CSERDE_LIMIT_EXCEEDED);
    limits.max_items = 5u;
    check_equal(drain(data_bind_value_get(root, "labels")), CSERDE_DONE);
    data_bind_value_free(root);
  }
  it("bounds names and slices cumulatively at the exact byte boundary") {
    DataBindValue *root = parse("Record", SEED);
    const size_t view_bytes = strlen("idtextblob") + strlen("stable") + strlen("seed");
    limits.max_view_bytes = view_bytes - 1u;
    check_equal(drain(root), CSERDE_LIMIT_EXCEEDED);
    limits.max_view_bytes = view_bytes;
    check_equal(drain(root), CSERDE_DONE);
    limits.max_view_bytes = 1u;
    check_equal(drain(root), CSERDE_LIMIT_EXCEEDED);
    data_bind_value_free(root);
  }
  it("admits scalar roots at zero depth and empty slices at zero bytes") {
    DataBindValue *root = parse("Record", "{\"id\":0,\"text\":\"\",\"blob\":\"\"}");
    limits.max_depth = 0u;
    limits.max_view_bytes = 0u;
    check_equal(drain(data_bind_value_get(root, "text")), CSERDE_DONE);
    check_equal(drain(data_bind_value_get(root, "blob")), CSERDE_DONE);
    check_equal(drain(root), CSERDE_LIMIT_EXCEEDED);
    data_bind_value_free(root);
  }
  it("rejects extended domains with a sticky unsupported result") {
    DataBindValue *root = parse("Domains",
        "{\"at\":\"Sat, 04 Mar 2006 13:27:54 GMT\",\"day\":\"2026-07-15\","
        "\"clock\":\"09:30:05.123\",\"span\":\"1h2m3s\",\"price\":\"12.34\","
        "\"count\":\"123456789012345678901234567890\","
        "\"cost\":{\"amount\":\"12.34\",\"currency\":\"USD\"}}");
    for (size_t i = 0u; i < data_bind_value_field_count(root); ++i)
      check_equal(drain(data_bind_value_field_at(root, i)), CSERDE_UNSUPPORTED);
    data_bind_value_free(root);
  }
  it("emits balanced empty containers and permits bounded skipping") {
    DataBindValue *root = parse("Nested", "{\"children\":[]}");
    cserde_reader *reader = open_value(root);
    next_kind(reader, CSERDE_MAP_BEGIN);
    cserde_token key = next_kind(reader, CSERDE_STRING);
    check_equal(key.value.slice.size, strlen("children"));
    check_equal(key.value.slice.data, "children", key.value.slice.size);
    next_kind(reader, CSERDE_ARRAY_BEGIN);
    next_kind(reader, CSERDE_ARRAY_END);
    next_kind(reader, CSERDE_MAP_END);
    end_reader(reader);
    data_bind_value_free(root);
    root = parse("Nested", "{\"children\":[{\"id\":1},{\"id\":2}]}");
    reader = open_value(root);
    check_equal(cserde_reader_skip_value(reader, VALUE_DEPTH), CSERDE_OK);
    end_reader(reader);
    data_bind_value_free(root);
  }
  it("borrows source storage until close without requiring the codec") {
    DataBind *local_codec = NULL;
    DataBindValue *root = NULL;
    static const char schema[] = "message Text { string text; }";
    static const char json[] = "{\"text\":\"borrowed\"}";
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u, &local_codec, &error), DATA_BIND_OK);
    check_equal(data_bind_parse_json(local_codec, "Text", json, sizeof(json) - 1u, &root, &error), DATA_BIND_OK);
    cserde_reader *reader = open_value(data_bind_value_get(root, "text"));
    data_bind_free(local_codec);
    cserde_token token = next_kind(reader, CSERDE_STRING);
    const char *borrowed = NULL;
    size_t size = 0u;
    check_equal(data_bind_value_get_string(data_bind_value_get(root, "text"), &borrowed, &size), DATA_BIND_OK);
    check(token.value.slice.data == (const unsigned char *)borrowed);
    check_equal(token.value.slice.size, size);
    check_equal(token.value.slice.data, "borrowed", size);
    end_reader(reader);
    /* Closing the reader leaves the caller's root alive. */
    check_equal(data_bind_value_get_string(data_bind_value_get(root, "text"), &borrowed, &size), DATA_BIND_OK);
    data_bind_value_free(root);
  }
  it("validates arguments ABI and allocation arithmetic before publication") {
    DataBindValue *root = parse("Record", SEED);
    cserde_reader *reader = NULL;
    check_equal(data_bind_value_reader_open(NULL, &limits, &reader, &error), DATA_BIND_ERR_INVALID_ARG);
    check_null(reader);
    check_equal(data_bind_value_reader_open(root, &limits, NULL, &error), DATA_BIND_ERR_INVALID_ARG);
    limits.size = sizeof(limits.size);
    check_equal(data_bind_value_reader_open(root, &limits, &reader, &error), DATA_BIND_ERR_INVALID_ARG);
    limits = (DataBindValueReaderLimits)DATA_BIND_VALUE_READER_LIMITS_INIT;
    ++limits.abi_version;
    check_equal(data_bind_value_reader_open(root, &limits, &reader, &error), DATA_BIND_ERR_INVALID_ARG);
    limits = (DataBindValueReaderLimits)DATA_BIND_VALUE_READER_LIMITS_INIT;
    limits.max_items = 0u;
    check_equal(data_bind_value_reader_open(root, &limits, &reader, &error), DATA_BIND_ERR_INVALID_ARG);
    limits.max_items = 1u;
    limits.max_depth = SIZE_MAX;
    check_equal(data_bind_value_reader_open(root, &limits, &reader, &error), DATA_BIND_ERR_LIMIT);
    check_null(reader);
    check_equal(data_bind_value_reader_open(root, NULL, &reader, NULL), DATA_BIND_OK);
    data_bind_value_reader_close(reader);
    data_bind_value_reader_close(NULL);
    data_bind_value_free(root);
  }
}
