#include "data_bind_native.h"

#include <cmeta/method.h>
#include <cstl/typed.h>
#include <salts_cmeta_data.h>
#include <tstr.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  CONTAINER_WORKSPACE_BYTES = 8192,
  CONTAINER_MAX_DEPTH = 12,
  CONTAINER_MAX_ITEMS = 128,
  CONTAINER_MAX_OWNED_BYTES = 256,
  CONTAINER_MAX_TOKENS = 64,
  CONTAINER_SENTINEL = 0xa5
};

typedef struct ContainerWorkspace {
  _Alignas(64) unsigned char bytes[CONTAINER_WORKSPACE_BYTES];
} ContainerWorkspace;

typedef struct TokenSink {
  cserde_token tokens[CONTAINER_MAX_TOKENS];
  size_t count;
} TokenSink;

typedef struct NativeContainerTokenSource {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} NativeContainerTokenSource;

typed(Vec, NativeIntVec, int);
typed(Set, NativeIntSet, int);
typed(Map, NativeIntLongMap, int, long);

typedef struct NativeTextRecord {
  tstr text;
} NativeTextRecord;

static cmeta_data_desc NATIVE_TEXT_RECORD_DATA;
CMETA_DEFINE_DATA_TRAITS(
    native_text_record, &NATIVE_TEXT_RECORD_DATA);

static const cmeta_type_identity NATIVE_TEXT_RECORD_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.databind.NativeTextRecord");
static const cmeta_type_desc NATIVE_TEXT_RECORD_TYPE = {
    "NativeTextRecord", sizeof(NativeTextRecord), _Alignof(NativeTextRecord),
    CMETA_T_OBJECT, NULL, &cmeta_traits_native_text_record,
    &NATIVE_TEXT_RECORD_ID};
static cmeta_field_desc NATIVE_TEXT_RECORD_LAYOUT_FIELDS[1];
static cmeta_data_field_desc NATIVE_TEXT_RECORD_FIELDS[1];
static const cmeta_struct_desc NATIVE_TEXT_RECORD_LAYOUT = {
    "NativeTextRecord", sizeof(NativeTextRecord), _Alignof(NativeTextRecord),
    NATIVE_TEXT_RECORD_LAYOUT_FIELDS, 1u};
static const cmeta_data_struct_shape NATIVE_TEXT_RECORD_SHAPE = {
    &NATIVE_TEXT_RECORD_LAYOUT, NATIVE_TEXT_RECORD_FIELDS, 1u};

typed(Vec, NativeTextRecordVec, NativeTextRecord,
      &NATIVE_TEXT_RECORD_TYPE, &NATIVE_TEXT_RECORD_DATA);

typed(Set, NativeTextSet, tstr,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF);
typed(Map, NativeTextRecordMap, tstr, NativeTextRecord,
      SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF,
      &NATIVE_TEXT_RECORD_TYPE, &NATIVE_TEXT_RECORD_DATA);

enum { COMPOSITE_FIELD_COUNT = 4, INLINE_BYTES_EXTENT = 4 };
typedef unsigned char InlineBytes[INLINE_BYTES_EXTENT];
CMETA_DEFINE_FIXED_BYTES(inline_bytes, InlineBytes, INLINE_BYTES_EXTENT,
                        "test.databind.InlineBytes", "InlineBytes");

typedef struct NativeComposite {
  NativeTextRecord child;
  NativeTextRecordVec children;
  NativeTextSet unique_names;
  NativeTextRecordMap children_by_name;
  uint8_t presence;
  uint8_t nulls;
} NativeComposite;

static const cmeta_type_identity COMPOSITE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.databind.NativeComposite");
static const cmeta_type_desc COMPOSITE_TYPE = {
    "NativeComposite", sizeof(NativeComposite), _Alignof(NativeComposite),
    CMETA_T_OBJECT, NULL, NULL, &COMPOSITE_ID};
static cmeta_field_desc composite_layout_fields[COMPOSITE_FIELD_COUNT];
static cmeta_data_field_desc composite_fields[COMPOSITE_FIELD_COUNT];
static const cmeta_struct_desc COMPOSITE_LAYOUT = {
    "NativeComposite", sizeof(NativeComposite), _Alignof(NativeComposite),
    composite_layout_fields, COMPOSITE_FIELD_COUNT};
static const cmeta_data_struct_shape COMPOSITE_SHAPE = {
    &COMPOSITE_LAYOUT, composite_fields, COMPOSITE_FIELD_COUNT};
static const cmeta_data_desc COMPOSITE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.databind.NativeComposite.data",
    .display_name = "NativeComposite", .kind = CMETA_DATA_STRUCT,
    .storage_type = &COMPOSITE_TYPE, .shape = &COMPOSITE_SHAPE};

static void composite_field(size_t index, const char *name, size_t offset,
                            const cmeta_data_desc *data) {
  composite_layout_fields[index] = (cmeta_field_desc){
      name, data->storage_type->name, offset, data->storage_type->size,
      data->storage_type->align, data->storage_type, NULL};
  composite_fields[index] = (cmeta_data_field_desc){name, name, offset, data};
}

static ContainerWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static size_t element_callback_calls;
static const char COMPOSITE_TEXT[] = "alpha";
static const char COMPOSITE_KEY[] = "key";

static cserde_status sink_write(void *context, const cserde_token *token) {
  TokenSink *sink = (TokenSink *)context;
  if (sink == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  if (sink->count == CONTAINER_MAX_TOKENS) return CSERDE_LIMIT_EXCEEDED;
  sink->tokens[sink->count++] = *token;
  return CSERDE_OK;
}

static cserde_status sink_finish(void *context) {
  return context != NULL ? CSERDE_OK : CSERDE_INVALID_ARGUMENT;
}

static const cserde_writer_ops SINK_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    sink_write, sink_finish};

static cserde_status source_next(void *context, cserde_token *out) {
  NativeContainerTokenSource *source = (NativeContainerTokenSource *)context;
  if (source == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (source->index == source->count) return CSERDE_DONE;
  *out = source->tokens[source->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops SOURCE_OPS = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
    source_next};

static void reset_native(void) {
  memset(&workspace, 0, sizeof(workspace));
  options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
  diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  options.workspace = workspace.bytes;
  options.workspace_bytes = sizeof(workspace.bytes);
  options.max_depth = CONTAINER_MAX_DEPTH;
  options.max_items = CONTAINER_MAX_ITEMS;
  options.max_owned_bytes = CONTAINER_MAX_OWNED_BYTES;
  element_callback_calls = 0u;

  NATIVE_TEXT_RECORD_LAYOUT_FIELDS[0] = (cmeta_field_desc){
      .name = "text",
      .type_name = "tstr",
      .offset = offsetof(NativeTextRecord, text),
      .size = sizeof(tstr),
      .align = _Alignof(tstr),
      .type = salts_tstr_cmeta_data.storage_type,
      .declared_type = NULL};
  NATIVE_TEXT_RECORD_FIELDS[0] = (cmeta_data_field_desc){
      .stable_id = "test.databind.NativeTextRecord.text",
      .name = "text",
      .offset = offsetof(NativeTextRecord, text),
      .value = &salts_tstr_cmeta_data};
  NATIVE_TEXT_RECORD_DATA = (cmeta_data_desc){
      .struct_size = sizeof(cmeta_data_desc),
      .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.databind.NativeTextRecord.data",
      .display_name = "NativeTextRecord",
      .kind = CMETA_DATA_STRUCT,
      .storage_type = &NATIVE_TEXT_RECORD_TYPE,
      .shape = &NATIVE_TEXT_RECORD_SHAPE};
  composite_field(0u, "child", offsetof(NativeComposite, child),
                  &NATIVE_TEXT_RECORD_DATA);
  composite_field(1u, "children", offsetof(NativeComposite, children),
                  &NativeTextRecordVec_collection_data);
  composite_field(2u, "unique_names", offsetof(NativeComposite, unique_names),
                  &NativeTextSet_collection_data);
  composite_field(3u, "children_by_name", offsetof(NativeComposite, children_by_name),
                  &NativeTextRecordMap_map_data);
}

static void open_writer(TokenSink *sink, cserde_writer *writer) {
  memset(sink, 0, sizeof(*sink));
  memset(writer, 0, sizeof(*writer));
  check_equal(cserde_writer_init(writer, &SINK_OPS, sink), CSERDE_OK);
}

static void open_reader(const TokenSink *sink, NativeContainerTokenSource *source,
                        cserde_reader *reader) {
  memset(source, 0, sizeof(*source));
  source->tokens = sink->tokens;
  source->count = sink->count;
  memset(reader, 0, sizeof(*reader));
  check_equal(cserde_reader_init(reader, &SOURCE_OPS, source), CSERDE_OK);
}

static DataBindStatus native_init(const cmeta_data_desc *data, void *value) {
  return data_bind_native_init(
      &options, data, value, data->storage_type->size, &diagnostic);
}

static DataBindStatus native_clear(const cmeta_data_desc *data, void *value) {
  return data_bind_native_clear(
      &options, data, value, data->storage_type->size, &diagnostic);
}

static DataBindStatus roundtrip(
    const cmeta_data_desc *data, const void *source, void *destination,
    TokenSink *sink) {
  cserde_writer writer;
  cserde_reader reader;
  NativeContainerTokenSource token_source;
  DataBindStatus status;

  open_writer(sink, &writer);
  status = data_bind_native_encode(
      &options, data, source, data->storage_type->size,
      &writer, &diagnostic);
  if (status != DATA_BIND_OK) {
    (void)printf("native container encode failed: status=%d path=%s message=%s\\n",
                 (int)status, diagnostic.error.path, diagnostic.error.message);
    return status;
  }

  open_reader(sink, &token_source, &reader);
  status = data_bind_native_decode(
      &options, data, &reader, destination, data->storage_type->size,
      &diagnostic);
  if (status != DATA_BIND_OK)
    (void)printf("native container decode failed: status=%d path=%s message=%s\\n",
                 (int)status, diagnostic.error.path, diagnostic.error.message);
  return status;
}

static const cmeta_data_desc *counting_element(const void *object) {
  (void)object;
  ++element_callback_calls;
  return &cmeta_data_int;
}

/* Single-threaded fixtures own bounded containers and independent copied text.
 * Sink views borrow source owners until decode completes. Clear requires no
 * active iterator and restores the complete envelope, including state bytes. */
static void populate_composite(NativeComposite *object) {
  NativeTextRecord value = {0};
  tstr key = NULL;
  check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &value.text,
              (const unsigned char *)COMPOSITE_TEXT,
              sizeof(COMPOSITE_TEXT) - 1u, sizeof(COMPOSITE_TEXT) - 1u), CMETA_OK);
  check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &key,
              (const unsigned char *)COMPOSITE_KEY,
              sizeof(COMPOSITE_KEY) - 1u, sizeof(COMPOSITE_KEY) - 1u), CMETA_OK);
  check_true(cmeta_data_trait_copy_construct(&NATIVE_TEXT_RECORD_DATA,
                                            &object->child, &value));
  check_equal(NativeTextRecordVec_init(&object->children, CONTAINER_MAX_ITEMS), STL_OK);
  check_equal(NativeTextSet_init(&object->unique_names, CONTAINER_MAX_ITEMS), STL_OK);
  check_equal(NativeTextRecordMap_init(&object->children_by_name, CONTAINER_MAX_ITEMS), STL_OK);
  check_equal(NativeTextRecordVec_push(&object->children, value), STL_OK);
  check_equal(NativeTextSet_add(&object->unique_names, key), STL_OK);
  check_equal(NativeTextRecordMap_put(&object->children_by_name, key, value), STL_OK);
  cmeta_data_value_destroy(&NATIVE_TEXT_RECORD_DATA, &value);
  cmeta_data_value_destroy(&salts_tstr_cmeta_data, &key);
}

static void check_composite_zero(const NativeComposite *object) {
  check_null(object->child.text);
  check_equal(NativeTextRecordVec_size(&object->children), (size_t)0u);
  check_equal(NativeTextSet_size(&object->unique_names), (size_t)0u);
  check_equal(NativeTextRecordMap_size(&object->children_by_name), (size_t)0u);
  check_equal(object->presence, 0u);
  check_equal(object->nulls, 0u);
}

spec("DataBind canonical CSTL native containers") {
  (void)ttest_config__;

  before_each() {
    reset_native();
  }

  it("rejects missing composite ownership providers before touching poisoned storage") {
    NativeComposite object;
    unsigned char before[sizeof(object)];
    cmeta_data_collection_ops ops = NativeTextRecordVec_collection_ops;
    cmeta_data_desc missing_owner = NativeTextRecordVec_collection_data;
    ops.collector = NULL;
    missing_owner.collection_ops = &ops;
    composite_fields[1].value = &missing_owner;
    memset(&object, CONTAINER_SENTINEL, sizeof(object));
    memcpy(before, &object, sizeof(before));
    check_equal(native_init(&COMPOSITE_DATA, &object), DATA_BIND_ERR_SCHEMA);
    check_equal(memcmp(&object, before, sizeof(before)), 0);
    check_equal(native_clear(&COMPOSITE_DATA, &object), DATA_BIND_ERR_SCHEMA);
    check_equal(memcmp(&object, before, sizeof(before)), 0);
  }

  it("releases populated composite owners, resets state and permits reuse") {
    NativeComposite object;
    memset(&object, CONTAINER_SENTINEL, sizeof(object));
    check_equal(native_init(&COMPOSITE_DATA, &object), DATA_BIND_OK);
    check_composite_zero(&object);
    populate_composite(&object);
    object.presence = UINT8_MAX;
    object.nulls = UINT8_MAX;
    check_equal(native_clear(&COMPOSITE_DATA, &object), DATA_BIND_OK);
    check_composite_zero(&object);
    check_equal(native_clear(&COMPOSITE_DATA, &object), DATA_BIND_OK);
    populate_composite(&object);
    check_equal(native_clear(&COMPOSITE_DATA, &object), DATA_BIND_OK);
    check_composite_zero(&object);
  }

  it("copies composite owners independently of temporary and source owners") {
    NativeComposite source, destination;
    TokenSink sink;
    const NativeTextRecord *value;
    tstr key = NULL;
    check_equal(native_init(&COMPOSITE_DATA, &source), DATA_BIND_OK);
    check_equal(native_init(&COMPOSITE_DATA, &destination), DATA_BIND_OK);
    populate_composite(&source);
    check_equal(roundtrip(&COMPOSITE_DATA, &source, &destination, &sink), DATA_BIND_OK);
    check_true(source.child.text != destination.child.text);
    value = NativeTextRecordVec_at_const(&destination.children, 0u);
    check_not_null(value);
    check_true(value->text != source.child.text);
    check_true(value->text != destination.child.text);
    check_equal(native_clear(&COMPOSITE_DATA, &source), DATA_BIND_OK);
    check_equal(memcmp(value->text, COMPOSITE_TEXT, sizeof(COMPOSITE_TEXT) - 1u), 0);
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &key,
                (const unsigned char *)COMPOSITE_KEY,
                sizeof(COMPOSITE_KEY) - 1u, sizeof(COMPOSITE_KEY) - 1u), CMETA_OK);
    check_true(NativeTextSet_contains(&destination.unique_names, key));
    value = NativeTextRecordMap_get_const(&destination.children_by_name, key);
    check_not_null(value);
    check_equal(memcmp(value->text, COMPOSITE_TEXT, sizeof(COMPOSITE_TEXT) - 1u), 0);
    check_equal(memcmp(destination.child.text, COMPOSITE_TEXT, sizeof(COMPOSITE_TEXT) - 1u), 0);
    cmeta_data_value_destroy(&salts_tstr_cmeta_data, &key);
    check_equal(native_clear(&COMPOSITE_DATA, &destination), DATA_BIND_OK);
  }

  it("cleans partially decoded composite owners on an owned byte limit") {
    NativeComposite source, destination;
    TokenSink sink;
    cserde_writer writer;
    cserde_reader reader;
    NativeContainerTokenSource token_source;
    /* Child + list + set + map key fit; the map value exceeds this budget. */
    const size_t partial_owned_bytes = 16u;
    check_equal(native_init(&COMPOSITE_DATA, &source), DATA_BIND_OK);
    check_equal(native_init(&COMPOSITE_DATA, &destination), DATA_BIND_OK);
    populate_composite(&source);
    open_writer(&sink, &writer);
    check_equal(data_bind_native_encode(&options, &COMPOSITE_DATA, &source,
                sizeof(source), &writer, &diagnostic), DATA_BIND_OK);
    open_reader(&sink, &token_source, &reader);
    options.max_owned_bytes = partial_owned_bytes;
    check_equal(data_bind_native_decode(&options, &COMPOSITE_DATA, &reader,
                &destination, sizeof(destination), &diagnostic), DATA_BIND_ERR_LIMIT);
    check_contains(diagnostic.error.path, "children_by_name.text");
    check_true(token_source.index > 1u);
    check_composite_zero(&destination);
    check_equal(native_clear(&COMPOSITE_DATA, &destination), DATA_BIND_OK);
    options.max_owned_bytes = CONTAINER_MAX_OWNED_BYTES;
    check_equal(roundtrip(&COMPOSITE_DATA, &source, &destination, &sink), DATA_BIND_OK);
    check_equal(native_clear(&COMPOSITE_DATA, &destination), DATA_BIND_OK);
    check_equal(native_clear(&COMPOSITE_DATA, &source), DATA_BIND_OK);
  }

  it("uses exact fixed byte lifecycle while rejecting missing native buffer semantics") {
    InlineBytes source = {1u, 2u, 3u, 4u};
    InlineBytes destination;
    InlineBytes before;
    memset(destination, CONTAINER_SENTINEL, sizeof(destination));
    memcpy(before, destination, sizeof(before));
    check_true(cmeta_data_desc_valid(&inline_bytes_cmeta_data));
    check_equal(native_init(&inline_bytes_cmeta_data, destination), DATA_BIND_ERR_SCHEMA);
    check_equal(native_clear(&inline_bytes_cmeta_data, destination), DATA_BIND_ERR_SCHEMA);
    check_equal(memcmp(destination, before, sizeof(before)), 0);
    check_equal(cmeta_data_value_init_zero(&inline_bytes_cmeta_data, destination), CMETA_OK);
    check_true(inline_bytes_cmeta_is_zero(destination));
    check_equal(cmeta_data_fixed_copy(&inline_bytes_cmeta_data, destination,
                                      source, sizeof(source)), CMETA_OK);
    check_equal(memcmp(destination, source, sizeof(source)), 0);
    check_equal(cmeta_data_value_restore_zero(&inline_bytes_cmeta_data, destination), CMETA_OK);
    check_true(inline_bytes_cmeta_is_zero(destination));
    check_equal(cmeta_data_value_restore_zero(&inline_bytes_cmeta_data, destination), CMETA_OK);
    check_true(inline_bytes_cmeta_is_zero(destination));
  }

  it("measures collection graphs only from static member metadata") {
    cmeta_data_collection_ops ops = NativeIntVec_collection_ops;
    cmeta_data_desc data = NativeIntVec_collection_data;
    DataBindNativeRequirements requirements =
        (DataBindNativeRequirements)DATA_BIND_NATIVE_REQUIREMENTS_INIT;

    ops.element = counting_element;
    data.collection_ops = &ops;
    check_equal(
        data_bind_native_measure(
            &options, &data, &requirements, &diagnostic),
        DATA_BIND_OK);
    check_equal(element_callback_calls, (size_t)0u);
    check_equal(requirements.descriptor_depth, (size_t)2u);
    check_equal(requirements.descriptor_nodes, (size_t)2u);
    check_equal(requirements.container_depth, (size_t)1u);
    check_equal(requirements.field_tracking_bytes, (size_t)0u);
    check_true(requirements.decode_bytes > requirements.staging_bytes);
    check_true(requirements.decode_bytes <= sizeof(workspace.bytes));
  }

  it("consumes canonical typed receiver method reflection and generic owners") {
    const cmeta_receiver_method_set *set;
    const cmeta_receiver_method *method;
    const cmeta_param_desc *receiver;
    const cmeta_type_desc *one_int[] = {&cmeta_type_int};
    const cmeta_type_desc *map_args[] = {&cmeta_type_int, &cmeta_type_long};
    cmeta_generic_desc map_owner = stl_map_generic_desc;
    cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;

    set = NativeIntVec_receiver_method_set();
    check_true(cmeta_receiver_method_set_valid(set));
    check_true(cmeta_generic_desc_equal(set->owner, &stl_vec_generic_desc));
    method = cmeta_receiver_method_find(set, "push");
    check_not_null(method);
    check_true(method->function == NativeIntVec_push_function());
    check_true(method->abi == NativeIntVec_push_function_abi());
    receiver = cmeta_function_receiver(method->function);
    check_not_null(receiver);
    check_true((receiver->flags & CMETA_PARAM_RECEIVER) != 0u);
    check_true(cmeta_type_equal(receiver->type->pointee, &NativeIntVec_cmeta_type));
    check_equal(
        cmeta_receiver_method_resolve(
            set, &NativeIntVec_cmeta_type, &stl_vec_generic_desc, "push",
            one_int, 1u, &resolution),
        CMETA_RECEIVER_RESOLVE_OK);

    set = NativeIntSet_receiver_method_set();
    check_true(cmeta_receiver_method_set_valid(set));
    check_true(cmeta_generic_desc_equal(set->owner, &stl_set_generic_desc));
    method = cmeta_receiver_method_find(set, "add");
    check_not_null(method);
    check_true(method->function == NativeIntSet_add_function());
    check_true(method->abi == NativeIntSet_add_function_abi());
    resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
    check_equal(
        cmeta_receiver_method_resolve(
            set, &NativeIntSet_cmeta_type, &stl_set_generic_desc, "add",
            one_int, 1u, &resolution),
        CMETA_RECEIVER_RESOLVE_OK);

    set = NativeIntLongMap_receiver_method_set();
    check_true(cmeta_receiver_method_set_valid(set));
    check_true(cmeta_generic_desc_equal(set->owner, &stl_map_generic_desc));
    check_true(cmeta_generic_desc_equal(set->owner, &map_owner));
    method = cmeta_receiver_method_find(set, "put");
    check_not_null(method);
    check_true(method->function == NativeIntLongMap_put_function());
    check_true(method->abi == NativeIntLongMap_put_function_abi());
    resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
    check_equal(
        cmeta_receiver_method_resolve(
            set, &NativeIntLongMap_cmeta_type, &map_owner, "put",
            map_args, 2u, &resolution),
        CMETA_RECEIVER_RESOLVE_OK);

    resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
    check_equal(
        cmeta_receiver_method_resolve(
            set, &NativeIntLongMap_cmeta_type, &stl_vec_generic_desc, "put",
            map_args, 2u, &resolution),
        CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH);
    check_null(resolution.method);
  }

  it("round-trips typed Vec<int> through array tokens") {
    NativeIntVec source = {0};
    NativeIntVec destination = {0};
    TokenSink sink;
    const int *value;

    check_equal(native_init(&NativeIntVec_collection_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(NativeIntVec_init(&source, CONTAINER_MAX_ITEMS), STL_OK);
    check_equal(NativeIntVec_push(&source, 3), STL_OK);
    check_equal(NativeIntVec_push(&source, 5), STL_OK);

    check_equal(roundtrip(
                    &NativeIntVec_collection_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_equal(sink.count, (size_t)4u);
    check_true(sink.tokens[0].kind == CSERDE_ARRAY_BEGIN);
    check_true(sink.tokens[1].kind == CSERDE_SINT);
    check_true(sink.tokens[2].kind == CSERDE_SINT);
    check_true(sink.tokens[3].kind == CSERDE_ARRAY_END);
    check_equal(NativeIntVec_size(&source), (size_t)2u);
    check_equal(NativeIntVec_size(&destination), (size_t)2u);
    value = NativeIntVec_at_const(&destination, 0u);
    check_not_null(value);
    check_equal(*value, 3);
    value = NativeIntVec_at_const(&destination, 1u);
    check_not_null(value);
    check_equal(*value, 5);

    check_equal(native_clear(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeIntVec_collection_data, &source),
                DATA_BIND_OK);
    check_equal(NativeIntVec_size(&destination), (size_t)0u);
    check_equal(NativeIntVec_size(&source), (size_t)0u);
  }

  it("round-trips typed Set<int> through canonical collection semantics") {
    NativeIntSet source = {0};
    NativeIntSet destination = {0};
    TokenSink sink;

    check_equal(native_init(&NativeIntSet_collection_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeIntSet_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(NativeIntSet_init(&source, CONTAINER_MAX_ITEMS), STL_OK);
    check_equal(NativeIntSet_add(&source, 5), STL_OK);
    check_equal(NativeIntSet_add(&source, 3), STL_OK);

    check_equal(roundtrip(
                    &NativeIntSet_collection_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_equal(NativeIntSet_size(&source), (size_t)2u);
    check_equal(NativeIntSet_size(&destination), (size_t)2u);
    check_true(NativeIntSet_contains(&destination, 3));
    check_true(NativeIntSet_contains(&destination, 5));

    check_equal(native_clear(&NativeIntSet_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeIntSet_collection_data, &source),
                DATA_BIND_OK);
  }

  it("round-trips typed Map<int,long> without raw map storage knowledge") {
    NativeIntLongMap source = {0};
    NativeIntLongMap destination = {0};
    TokenSink sink;
    const long *value;

    check_equal(native_init(&NativeIntLongMap_map_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeIntLongMap_map_data, &destination),
                DATA_BIND_OK);
    check_equal(NativeIntLongMap_init(&source, CONTAINER_MAX_ITEMS), STL_OK);
    check_equal(NativeIntLongMap_put(&source, 2, 20L), STL_OK);
    check_equal(NativeIntLongMap_put(&source, 1, 10L), STL_OK);

    check_equal(roundtrip(
                    &NativeIntLongMap_map_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_true(sink.count >= (size_t)6u);
    check_true(sink.tokens[0].kind == CSERDE_MAP_BEGIN);
    check_true(sink.tokens[sink.count - 1u].kind == CSERDE_MAP_END);
    check_equal(NativeIntLongMap_size(&destination), (size_t)2u);
    value = NativeIntLongMap_get_const(&destination, 1);
    check_not_null(value);
    check_equal(*value, 10L);
    value = NativeIntLongMap_get_const(&destination, 2);
    check_not_null(value);
    check_equal(*value, 20L);

    check_equal(native_clear(&NativeIntLongMap_map_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeIntLongMap_map_data, &source),
                DATA_BIND_OK);
  }

  it("uses Salts managed lifecycle inside an explicit generated record Vec") {
    NativeTextRecordVec source = {0};
    NativeTextRecordVec destination = {0};
    NativeTextRecord first = {0};
    NativeTextRecord second = {0};
    TokenSink sink;
    const NativeTextRecord *value;

    check_true(cmeta_data_value_traits_supported(&NATIVE_TEXT_RECORD_DATA));
    check_equal(native_init(&NativeTextRecordVec_collection_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeTextRecordVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(cmeta_data_value_init_zero(&NATIVE_TEXT_RECORD_DATA, &first),
                CMETA_OK);
    check_equal(cmeta_data_value_init_zero(&NATIVE_TEXT_RECORD_DATA, &second),
                CMETA_OK);
    check_equal(NativeTextRecordVec_init(&source, CONTAINER_MAX_ITEMS), STL_OK);
    check_equal(cmeta_data_buffer_assign(
                    &salts_tstr_cmeta_data, &first.text,
                    (const unsigned char *)"alpha", 5u, 5u),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &salts_tstr_cmeta_data, &second.text,
                    (const unsigned char *)"beta", 4u, 4u),
                CMETA_OK);

    check_equal(NativeTextRecordVec_push(&source, first), STL_OK);
    check_equal(NativeTextRecordVec_push(&source, second), STL_OK);
    check_equal(cmeta_data_value_restore_zero(
                    &NATIVE_TEXT_RECORD_DATA, &first), CMETA_OK);
    check_equal(cmeta_data_value_restore_zero(
                    &NATIVE_TEXT_RECORD_DATA, &second), CMETA_OK);

    check_equal(roundtrip(
                    &NativeTextRecordVec_collection_data,
                    &source, &destination, &sink),
                DATA_BIND_OK);
    check_equal(NativeTextRecordVec_size(&destination), (size_t)2u);
    value = NativeTextRecordVec_at_const(&destination, 0u);
    check_not_null(value);
    check_equal(tstr_len(value->text), (size_t)5u);
    check_equal(memcmp(value->text, "alpha", 5u), 0);
    value = NativeTextRecordVec_at_const(&destination, 1u);
    check_not_null(value);
    check_equal(tstr_len(value->text), (size_t)4u);
    check_equal(memcmp(value->text, "beta", 4u), 0);

    check_equal(native_clear(
                    &NativeTextRecordVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(
                    &NativeTextRecordVec_collection_data, &source),
                DATA_BIND_OK);
  }

  it("aborts a partially decoded Vec and leaves destination empty") {
    NativeIntVec destination = {0};
    cserde_token tokens[4] = {0};
    NativeContainerTokenSource source = {0};
    cserde_reader reader = {0};

    tokens[0].kind = CSERDE_ARRAY_BEGIN;
    tokens[1].kind = CSERDE_SINT;
    tokens[1].value.sint = 7;
    tokens[2].kind = CSERDE_STRING;
    tokens[2].value.slice.data = (const unsigned char *)"bad";
    tokens[2].value.slice.size = 3u;
    tokens[2].value.slice.lifetime = CSERDE_VIEW_STABLE;
    tokens[3].kind = CSERDE_ARRAY_END;

    check_equal(native_init(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
    source.tokens = tokens;
    source.count = 4u;
    check_equal(cserde_reader_init(&reader, &SOURCE_OPS, &source), CSERDE_OK);
    check_equal(
        data_bind_native_decode(
            &options, &NativeIntVec_collection_data, &reader,
            &destination, sizeof(destination), &diagnostic),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(NativeIntVec_size(&destination), (size_t)0u);
    check_equal(native_clear(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
  }
}
