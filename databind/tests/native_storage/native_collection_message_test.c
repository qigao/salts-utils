#include "data_bind_message_plan.h"
#include "data_bind_format_provider.h"
#include "data_bind_projection_plan.h"
#include "native_test_alignment.h"

#include <cstl/typed.h>
#include <cmeta_cmeta_data.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  LIST_FIELD_COUNT = 3,
  LIST_NOTE_INDEX = 2,
  LIST_WORKSPACE_BYTES = 8192,
  LIST_OUTPUT_BYTES = 256,
  LIST_MAX_DEPTH = 8,
  LIST_MAX_ITEMS = 32,
  LIST_MAX_OWNED_BYTES = 128,
  LIST_SIZE_LIMIT = 4
};

cmeta_type(Vec, NativeListValues, uint32_t, &cmeta_type_uint32, &cmeta_data_uint32);
cmeta_type(Map, NativeJsonKeys, tstr, uint32_t,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF,
      &cmeta_type_uint32, &cmeta_data_uint32);

typedef struct NativeListOrder {
  uint32_t id;
  NativeListValues values;
  tstr note;
  uint8_t presence;
} NativeListOrder;

static const cmeta_type_identity LIST_ORDER_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.native-list.Order");
static const cmeta_type_desc LIST_ORDER_TYPE = {
    "NativeListOrder", sizeof(NativeListOrder), _Alignof(NativeListOrder),
    CMETA_T_OBJECT, NULL, NULL, &LIST_ORDER_ID};
static cmeta_field_desc list_layout_fields[] = {
    {"id", "uint32_t", offsetof(NativeListOrder, id), sizeof(uint32_t),
     _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"values", "NativeListValues", offsetof(NativeListOrder, values), sizeof(NativeListValues),
     _Alignof(NativeListValues), &NativeListValues_cmeta_type, NULL},
    {"note", "tstr", offsetof(NativeListOrder, note), sizeof(tstr),
     _Alignof(tstr), NULL, NULL}};
static const cmeta_struct_desc LIST_LAYOUT = {
    "NativeListOrder", sizeof(NativeListOrder), _Alignof(NativeListOrder),
    list_layout_fields, LIST_FIELD_COUNT};
static cmeta_data_field_desc list_fields[] = {
    {"test.native-list.Order.id", "id", offsetof(NativeListOrder, id), &cmeta_data_uint32},
    {"test.native-list.Order.values", "values", offsetof(NativeListOrder, values),
     &NativeListValues_collection_data},
    {"test.native-list.Order.note", "note", offsetof(NativeListOrder, note), NULL}};
static const cmeta_data_struct_shape LIST_SHAPE = {
    &LIST_LAYOUT, list_fields, LIST_FIELD_COUNT};
static const cmeta_data_desc LIST_DATA = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-list.Order.data", .display_name = "NativeListOrder",
    .kind = CMETA_DATA_STRUCT, .storage_type = &LIST_ORDER_TYPE, .shape = &LIST_SHAPE};
static const DataBindNativeStateBinding LIST_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "note", offsetof(NativeListOrder, presence), 0u}};
static const DataBindNativeTypeBinding LIST_BINDING = {
    sizeof(DataBindNativeTypeBinding), DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "Order", &LIST_DATA, LIST_PRESENCE, 1u, NULL, 0u};
static const char LIST_SCHEMA[] =
    "message Order { [name(orderId), alias(legacyId)] uint32 id; "
    "@Size(min=0, max=4) list<uint32> values; optional string note; }";
static const char LIST_INPUT[] = "{\"legacyId\":42,\"note\":\"macro\",\"values\":[7,9]}";
static const char LIST_OUTPUT[] = "{\"orderId\":42,\"values\":[7,9],\"note\":\"macro\"}";

typedef union ListWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[LIST_WORKSPACE_BYTES];
} ListWorkspace;
typedef struct ListOutput {
  char bytes[LIST_OUTPUT_BYTES];
  size_t size;
  size_t capacity;
} ListOutput;

static DataBind *list_codec;
static DataBindMessagePlan *list_plan;
static DataBindFormatPlan *list_format;
static DataBindNativeOptions list_options;
static DataBindMessagePlanDiagnostic list_diagnostic;
static DataBindError list_error;
static ListWorkspace list_workspace;
static NativeListOrder list_published;
static NativeJsonKeys json_keys;

/* Single-threaded staging owns the Vec and copied text. Token views end at
 * reader close. Publication destroys old resources once and moves new owners;
 * the fixture separately transfers DataBind's presence bit outside CMeta. */
static DataBindStatus replace_list_json(const char *input, size_t size) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindFormatCanonicalReader canonical = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
  NativeListOrder staging;
  DataBindStatus status, close_status;
  if (cmeta_data_value_init_zero(&LIST_DATA, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  staging.presence = 0u;
  status = data_bind_format_reader_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
                                       input, size, LIST_MAX_DEPTH, &lease, &list_error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_format_canonical_reader_init(list_format, lease.reader, &canonical,
                                                &list_error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_decode_native(list_plan, &list_options,
      data_bind_format_canonical_reader_reader(&canonical), &staging, sizeof(staging),
      &list_diagnostic);
cleanup:
  close_status = data_bind_format_reader_close(&lease);
  if (status == DATA_BIND_OK) status = close_status;
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(&LIST_DATA, &list_published);
    cmeta_data_trait_move_construct(&LIST_DATA, &list_published, &staging);
    list_published.presence = staging.presence;
    staging.presence = 0u;
  }
  cmeta_data_value_destroy(&LIST_DATA, &staging);
  return status;
}

static int list_write(const void *data, size_t size, void *context) {
  ListOutput *out = (ListOutput *)context;
  if (size > out->capacity - out->size) return -1;
  memcpy(out->bytes + out->size, data, size);
  out->size += size;
  out->bytes[out->size] = '\0';
  return 0;
}

static DataBindStatus encode_list_json(ListOutput *out) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindFormatCanonicalWriter canonical = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
  DataBindStatus status, close_status;
  out->size = 0u;
  out->bytes[0] = '\0';
  status = data_bind_format_writer_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
      list_write, out, LIST_MAX_DEPTH, &lease, &list_error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_format_canonical_writer_init(list_format, lease.writer, &canonical,
                                                &list_error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_encode_native(list_plan, &list_options,
      &list_published, sizeof(list_published),
      data_bind_format_canonical_writer_writer(&canonical), &list_diagnostic);
  if (status == DATA_BIND_OK && cserde_writer_finish(
      data_bind_format_canonical_writer_writer(&canonical)) != CSERDE_OK)
    status = DATA_BIND_ERR_RUNTIME;
cleanup:
  close_status = data_bind_format_writer_close(&lease, &list_error);
  return status == DATA_BIND_OK ? close_status : status;
}

static DataBindStatus encode_keys_json(ListOutput *out) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus status, close_status;
  out->size = 0u;
  out->bytes[0] = '\0';
  status = data_bind_format_writer_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
      list_write, out, LIST_MAX_DEPTH, &lease, &list_error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_native_encode(&list_options, &NativeJsonKeys_map_data, &json_keys,
                                  sizeof(json_keys), lease.writer, &diagnostic);
  close_status = data_bind_format_writer_close(&lease, &list_error);
  return status == DATA_BIND_OK ? close_status : status;
}

spec("DataBind native builtin lists and owning JSON keys") {
  before_all() {
    list_layout_fields[LIST_NOTE_INDEX].type = cmeta_tstr_cmeta_data.storage_type;
    list_fields[LIST_NOTE_INDEX].value = &cmeta_tstr_cmeta_data;
    check_true(cmeta_data_desc_valid(&LIST_DATA));
    check_true(cmeta_data_value_move_supported(&LIST_DATA));
    list_error = (DataBindError)DATA_BIND_ERROR_INIT;
    list_diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_create_from_text(LIST_SCHEMA, sizeof(LIST_SCHEMA) - 1u,
                                         &list_codec, &list_error), DATA_BIND_OK);
    DataBindStatus status = data_bind_message_plan_compile(
        list_codec, "Order", &LIST_BINDING, &list_plan, &list_diagnostic);
    info("MessagePlan compile: %s: %s", list_diagnostic.schema_field, list_diagnostic.message);
    check_equal(status, DATA_BIND_OK);
    check_equal(data_bind_format_plan_compile(list_codec, "Order", DATA_BIND_FORMAT_JSON,
                                             &list_format, &list_error), DATA_BIND_OK);
  }
  before_each() {
    list_options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    list_options.workspace = list_workspace.bytes;
    list_options.workspace_bytes = sizeof(list_workspace.bytes);
    list_options.max_depth = LIST_MAX_DEPTH;
    list_options.max_items = LIST_MAX_ITEMS;
    list_options.max_owned_bytes = LIST_MAX_OWNED_BYTES;
    list_error = (DataBindError)DATA_BIND_ERROR_INIT;
    list_diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_equal(cmeta_data_value_init_zero(&LIST_DATA, &list_published), CMETA_OK);
    list_published.presence = 0u;
    check_equal(cmeta_data_value_init_zero(&NativeJsonKeys_map_data, &json_keys), CMETA_OK);
    check_equal(replace_list_json(LIST_INPUT, sizeof(LIST_INPUT) - 1u), DATA_BIND_OK);
  }
  after_each() {
    check_equal(cmeta_data_value_restore_zero(&NativeJsonKeys_map_data, &json_keys), CMETA_OK);
    check_equal(cmeta_data_value_restore_zero(&LIST_DATA, &list_published), CMETA_OK);
    list_published.presence = 0u;
    check_equal(NativeListValues_size(&list_published.values), 0u);
    check_true(cmeta_tstr_cmeta_buffer_ops.is_zero(&list_published.note));
  }
  after_all() {
    data_bind_format_plan_free(list_format);
    data_bind_message_plan_free(list_plan);
    data_bind_free(list_codec);
  }

  it("replaces the legacy macro JSON path with a canonical owned uint32 list") {
    ListOutput out = {.capacity = LIST_OUTPUT_BYTES - 1u};
    check_equal(list_published.id, 42u);
    check_equal(NativeListValues_size(&list_published.values), 2u);
    check_equal(*NativeListValues_at_const(&list_published.values, 0u), 7u);
    check_equal(*NativeListValues_at_const(&list_published.values, 1u), 9u);
    check_equal(list_published.note, "macro");
    check_equal(list_published.presence, 1u);
    check_equal(encode_list_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, LIST_OUTPUT);
  }

  it("releases omitted optional text while publishing an empty list") {
    static const char empty[] = "{\"legacyId\":3,\"values\":[]}";
    ListOutput out = {.capacity = LIST_OUTPUT_BYTES - 1u};
    check_equal(replace_list_json(empty, sizeof(empty) - 1u), DATA_BIND_OK);
    check_equal(NativeListValues_size(&list_published.values), 0u);
    check_true(cmeta_tstr_cmeta_buffer_ops.is_zero(&list_published.note));
    check_equal(list_published.presence, 0u);
    check_equal(encode_list_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, "{\"orderId\":3,\"values\":[]}");
    check_equal(replace_list_json(LIST_INPUT, sizeof(LIST_INPUT) - 1u), DATA_BIND_OK);
  }

  it("rolls back text and partially collected values after a bad element") {
    static const char bad[] = "{\"legacyId\":8,\"note\":\"staged\",\"values\":[1,\"bad\"]}";
    tstr note = list_published.note;
    const uint32_t *values = NativeListValues_data_const(&list_published.values);
    check_equal(replace_list_json(bad, sizeof(bad) - 1u), DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(list_published.note == note);
    check_true(NativeListValues_data_const(&list_published.values) == values);
    check_equal(list_published.id, 42u);
    check_equal(list_published.presence, 1u);
    check_equal(NativeListValues_size(&list_published.values), 2u);
  }

  it("rolls back an out-of-range uint32 element and a list Size violation") {
    static const char range[] = "{\"legacyId\":8,\"values\":[1,4294967296]}";
    static const char size[] = "{\"legacyId\":8,\"values\":[1,2,3,4,5]}";
    tstr note = list_published.note;
    check_equal(replace_list_json(range, sizeof(range) - 1u), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(replace_list_json(size, sizeof(size) - 1u), DATA_BIND_ERR_VALIDATION);
    check_true(list_published.note == note);
    check_equal(list_published.id, 42u);
    check_equal(NativeListValues_size(&list_published.values), 2u);
  }

  it("accepts the exact Size limit and reuses storage after complete release") {
    static const char limit[] = "{\"legacyId\":8,\"values\":[1,2,3,4]}";
    check_equal(replace_list_json(limit, sizeof(limit) - 1u), DATA_BIND_OK);
    check_equal(NativeListValues_size(&list_published.values), LIST_SIZE_LIMIT);
    check_equal(cmeta_data_value_restore_zero(&LIST_DATA, &list_published), CMETA_OK);
    list_published.presence = 0u;
    check_equal(NativeListValues_size(&list_published.values), 0u);
    check_true(cmeta_tstr_cmeta_buffer_ops.is_zero(&list_published.note));
    check_equal(replace_list_json(LIST_INPUT, sizeof(LIST_INPUT) - 1u), DATA_BIND_OK);
    check_equal(NativeListValues_size(&list_published.values), 2u);
    check_equal(list_published.note, "macro");
    check_equal(list_published.presence, 1u);
  }

  it("bounds whole-message items and owned bytes without replacing old owners") {
    tstr note = list_published.note;
    list_options.max_items = 1u;
    check_equal(replace_list_json(LIST_INPUT, sizeof(LIST_INPUT) - 1u), DATA_BIND_ERR_LIMIT);
    list_options.max_items = LIST_MAX_ITEMS;
    list_options.max_owned_bytes = 1u;
    check_equal(replace_list_json(LIST_INPUT, sizeof(LIST_INPUT) - 1u), DATA_BIND_ERR_LIMIT);
    check_true(list_published.note == note);
    check_equal(list_published.presence, 1u);
    list_options.max_owned_bytes = LIST_MAX_OWNED_BYTES;
    check_equal(replace_list_json(LIST_INPUT, sizeof(LIST_INPUT) - 1u), DATA_BIND_OK);
  }

  it("rejects optional native fields whose binding has no presence overlay") {
    DataBindNativeTypeBinding missing = LIST_BINDING;
    DataBindMessagePlan *rejected = NULL;
    missing.presence = NULL;
    missing.presence_count = 0u;
    check_equal(data_bind_message_plan_compile(list_codec, "Order", &missing,
                                              &rejected, &list_diagnostic), DATA_BIND_ERR_SCHEMA);
    check_null(rejected);
    data_bind_message_plan_free(rejected);
  }

  it("rejects mismatched element widths, signs, kinds and collection shapes") {
    static const char schema_pattern[] =
        "message Other { uint32 value; } "
        "message Order { uint32 id; %s values; optional string note; }";
    static const char *const types[] = {
        "list<uint16>", "list<int32>", "list<float64>", "list<string>",
        "list<Other>", "set<uint32>", "map<string,uint32>"};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
      char schema[LIST_OUTPUT_BYTES];
      DataBind *codec = NULL;
      DataBindMessagePlan *rejected = NULL;
      int size = snprintf(schema, sizeof(schema), schema_pattern, types[i]);
      check_true(size > 0 && (size_t)size < sizeof(schema));
      check_equal(data_bind_create_from_text(schema, (size_t)size, &codec, &list_error),
                  DATA_BIND_OK);
      check_equal(data_bind_message_plan_compile(codec, "Order", &LIST_BINDING,
                                                &rejected, &list_diagnostic),
                  DATA_BIND_ERR_TYPE_MISMATCH);
      check_equal(list_diagnostic.schema_field, "values");
      check_null(rejected);
      data_bind_message_plan_free(rejected);
      data_bind_free(codec);
    }
  }

  it("admits equivalent copied element descriptors through semantic identity") {
    cmeta_data_desc element = cmeta_data_uint32;
    cmeta_data_collection_ops ops = NativeListValues_collection_ops;
    cmeta_data_desc values = NativeListValues_collection_data;
    cmeta_data_field_desc fields[LIST_FIELD_COUNT];
    cmeta_data_struct_shape shape = LIST_SHAPE;
    cmeta_data_desc data = LIST_DATA;
    DataBindNativeTypeBinding binding = LIST_BINDING;
    DataBindMessagePlan *plan = NULL;
    memcpy(fields, list_fields, sizeof(fields));
    ops.element_data = &element;
    values.collection_ops = &ops;
    fields[1].value = &values;
    shape.fields = fields;
    data.shape = &shape;
    binding.data = &data;
    check_equal(data_bind_message_plan_compile(list_codec, "Order", &binding,
                                              &plan, &list_diagnostic), DATA_BIND_OK);
    check_not_null(plan);
    /* This plan borrows the local graph, so release it before the graph dies. */
    data_bind_message_plan_free(plan);
  }

  it("owns map keys independently and rejects invalid UTF-8 without releasing them") {
    static const char invalid[] = "\xc3\x28";
    ListOutput out = {.capacity = LIST_OUTPUT_BYTES - 1u};
    tstr key = tstr_dup_len(invalid, sizeof(invalid) - 1u);
    cmeta_data_map_borrow_cursor cursor = {0};
    const void *borrowed_key = NULL, *borrowed_value = NULL;
    check_not_null(key);
    check_equal(NativeJsonKeys_init(&json_keys, LIST_SIZE_LIMIT), STL_OK);
    check_equal(NativeJsonKeys_put(&json_keys, key, 7u), STL_OK);
    check_equal(cmeta_data_map_borrow_begin(&NativeJsonKeys_map_data, &json_keys, &cursor), CMETA_OK);
    cmeta_gen_status generated = cmeta_data_map_borrow_next(&cursor, &borrowed_key, &borrowed_value);
    check_true(generated == CMETA_GEN_VALUE || generated == CMETA_GEN_VALUE_AND_DONE);
    check_not_null(borrowed_key);
    check_not_null(borrowed_value);
    check_equal(*(const uint32_t *)borrowed_value, 7u);
    tstr retained = *(const tstr *)borrowed_key;
    check_true(retained != key);
    tstr_freep(&key);
    check_equal(encode_keys_json(&out), DATA_BIND_ERR_SCHEMA);
    check_equal(NativeJsonKeys_size(&json_keys), 1u);
    check_equal(memcmp(retained, invalid, sizeof(invalid) - 1u), 0);
    check_equal(cmeta_data_value_restore_zero(&NativeJsonKeys_map_data, &json_keys), CMETA_OK);
    check_equal(NativeJsonKeys_init(&json_keys, LIST_SIZE_LIMIT), STL_OK);
    key = tstr_dup("valid");
    check_not_null(key);
    check_equal(NativeJsonKeys_put(&json_keys, key, 9u), STL_OK);
    tstr_freep(&key);
    check_equal(encode_keys_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, "{\"valid\":9}");
  }
}
