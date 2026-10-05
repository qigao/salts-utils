/* #99 canonical enum-bits direct-reader contract.
 * No legacy enum adapter, CBind delegation, or local decoder. */
#include "data_bind_native.h"
#include "data_bind_format_provider.h"
#include "data_bind_message_plan.h"
#include "native_test_alignment.h"
#include "reader_probe.h"

#include <cmeta/struct.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  ENUM_WORKSPACE_BYTES = 4096,
  ENUM_MAX_DEPTH = 8,
  ENUM_MAX_ITEMS = 64,
  ENUM_OUTPUT_BYTES = 64,
  ENUM_WIDE_BITS = 64,
  ENUM_HIGH_BIT = 63
};

typedef union EnumWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[ENUM_WORKSPACE_BYTES];
} EnumWorkspace;

typedef struct CanonicalEnumBox {
  uint64_t bits;
  bool engaged;
} CanonicalEnumBox;

static bool canonical_assign_fails;
static size_t canonical_assign_calls;
static size_t canonical_restore_calls;

static bool canonical_enum_is_zero(const void *object) {
  const CanonicalEnumBox *value = (const CanonicalEnumBox *)object;
  return value != NULL && !value->engaged && value->bits == 0u;
}

static cmeta_status canonical_enum_read(const void *object, uint64_t *out) {
  const CanonicalEnumBox *value = (const CanonicalEnumBox *)object;
  if (value == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  if (!value->engaged) return CMETA_CALLBACK_ERROR;
  *out = value->bits;
  return CMETA_OK;
}

static cmeta_status canonical_enum_assign(void *object, uint64_t bits) {
  CanonicalEnumBox *value = (CanonicalEnumBox *)object;
  if (value == NULL) return CMETA_INVALID_ARGUMENT;
  ++canonical_assign_calls;
  value->bits = bits;
  value->engaged = true;
  return canonical_assign_fails ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void canonical_enum_restore(void *object) {
  CanonicalEnumBox *value = (CanonicalEnumBox *)object;
  if (value == NULL) return;
  ++canonical_restore_calls;
  value->bits = 0u;
  value->engaged = false;
}

static cmeta_status canonical_enum_init_zero(void *object) {
  if (object == NULL) return CMETA_INVALID_ARGUMENT;
  canonical_enum_restore(object);
  return CMETA_OK;
}

static void canonical_enum_move(void *destination, void *source) {
  CanonicalEnumBox *to = (CanonicalEnumBox *)destination;
  CanonicalEnumBox *from = (CanonicalEnumBox *)source;
  if (to == NULL || from == NULL || to == from) return;
  *to = *from;
  canonical_enum_restore(from);
}

static const cmeta_type_identity canonical_enum_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-enum.Canonical");
static const cmeta_type_desc canonical_enum_storage = {
    .name = "CanonicalEnumBox",
    .size = sizeof(CanonicalEnumBox),
    .align = _Alignof(CanonicalEnumBox),
    .kind = CMETA_T_OBJECT,
    .identity = &canonical_enum_identity};
static const cmeta_data_construct_ops canonical_enum_construct_ops = {
    .struct_size = sizeof(cmeta_data_construct_ops),
    .abi_version = CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    .storage_type = &canonical_enum_storage,
    .init_zero = canonical_enum_init_zero,
    .restore_zero = canonical_enum_restore,
    .move = canonical_enum_move};

static const cmeta_enum_bits_item signed_items[] = {
    {0u, "ZERO", "zero"},
    {1u, "READY", "ready"},
    {255u, "NEGATIVE", "negative"}};
static const cmeta_enum_domain signed_domain = {
    sizeof(cmeta_enum_domain), CMETA_ENUM_DOMAIN_ABI_VERSION,
    CMETA_ENUM_SIGNED, 8u, CMETA_ENUM_ORDINARY,
    signed_items, sizeof(signed_items) / sizeof(signed_items[0]), 0u};
static const cmeta_data_enum_bits_ops signed_ops = {
    sizeof(cmeta_data_enum_bits_ops), CMETA_DATA_ENUM_BITS_OPS_ABI_VERSION,
    &canonical_enum_storage, &signed_domain,
    canonical_enum_is_zero, canonical_enum_read,
    canonical_enum_assign, canonical_enum_restore};
static const cmeta_data_desc signed_enum_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-enum.Signed.data",
    .display_name = "SignedEnum",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &canonical_enum_storage,
    .enum_bits_ops = &signed_ops,
    .construct_ops = &canonical_enum_construct_ops};

static const cmeta_enum_bits_item flag_items[] = {
    {1u, "READ", "read"},
    {4u, "WRITE", "write"}};
static const cmeta_enum_domain flag_domain = {
    sizeof(cmeta_enum_domain), CMETA_ENUM_DOMAIN_ABI_VERSION,
    CMETA_ENUM_UNSIGNED, 8u, CMETA_ENUM_FLAGS,
    flag_items, sizeof(flag_items) / sizeof(flag_items[0]), 5u};
static const cmeta_data_enum_bits_ops flag_ops = {
    sizeof(cmeta_data_enum_bits_ops), CMETA_DATA_ENUM_BITS_OPS_ABI_VERSION,
    &canonical_enum_storage, &flag_domain,
    canonical_enum_is_zero, canonical_enum_read,
    canonical_enum_assign, canonical_enum_restore};
static const cmeta_data_desc flag_enum_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-enum.Flags.data",
    .display_name = "FlagEnum",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &canonical_enum_storage,
    .enum_bits_ops = &flag_ops,
    .construct_ops = &canonical_enum_construct_ops};

static const uint64_t WIDE_FLAGS_MASK = UINT64_C(1) | (UINT64_C(1) << ENUM_HIGH_BIT);
static const cmeta_enum_bits_item wide_items[] = {{UINT64_MAX, "Max", "maximum"}};
static const cmeta_enum_bits_item wide_flag_items[] = {
    {UINT64_C(1), "Low", "Low"}, {UINT64_C(1) << ENUM_HIGH_BIT, "High", "High"}};
static cmeta_enum_domain wide_domain, wide_flag_domain;
static cmeta_data_enum_bits_ops wide_ops, wide_flag_ops;
static cmeta_data_desc wide_enum_data, wide_flag_data;

typedef struct WideFlagsRecord {
  CanonicalEnumBox value;
  uint32_t marker;
} WideFlagsRecord;
static const cmeta_type_identity wide_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-enum.WideFlagsRecord");
static const cmeta_type_desc wide_record_type = {
    .name = "WideFlagsRecord", .size = sizeof(WideFlagsRecord),
    .align = _Alignof(WideFlagsRecord), .kind = CMETA_T_OBJECT,
    .identity = &wide_record_identity};
static const cmeta_field_desc wide_record_layout_fields[] = {
    {"value", "CanonicalEnumBox", offsetof(WideFlagsRecord, value),
     sizeof(CanonicalEnumBox), _Alignof(CanonicalEnumBox), &canonical_enum_storage, NULL},
    {"marker", "uint32_t", offsetof(WideFlagsRecord, marker),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc wide_record_layout = {
    "WideFlagsRecord", sizeof(WideFlagsRecord), _Alignof(WideFlagsRecord),
    wide_record_layout_fields, 2u};
static const cmeta_data_field_desc wide_record_fields[] = {
    {"test.native-enum.WideFlagsRecord.value", "value", offsetof(WideFlagsRecord, value), &wide_flag_data},
    {"test.native-enum.WideFlagsRecord.marker", "marker", offsetof(WideFlagsRecord, marker), &cmeta_data_uint32}};
static const cmeta_data_struct_shape wide_record_shape = {
    &wide_record_layout, wide_record_fields, 2u};
static const cmeta_data_desc wide_record_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-enum.WideFlagsRecord.data", .display_name = "WideFlagsRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &wide_record_type, .shape = &wide_record_shape};
static const DataBindNativeTypeBinding wide_record_binding =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("WideFlagsRecord", &wide_record_data);
static DataBind *message_codec;
static DataBindMessagePlan *message_plan;
static WideFlagsRecord message_value;

typedef struct EnumRow {
  int id;
  CanonicalEnumBox state;
} EnumRow;

static const cmeta_type_identity enum_row_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-enum.Row");
static const cmeta_type_desc enum_row_type = {
    .name = "EnumRow",
    .size = sizeof(EnumRow),
    .align = _Alignof(EnumRow),
    .kind = CMETA_T_OBJECT,
    .identity = &enum_row_identity};
static const cmeta_field_desc enum_row_layout_fields[] = {
    {.name = "id", .type_name = "int", .offset = offsetof(EnumRow, id),
     .size = sizeof(int), .align = _Alignof(int), .type = &cmeta_type_int},
    {.name = "state", .type_name = "CanonicalEnumBox",
     .offset = offsetof(EnumRow, state),
     .size = sizeof(CanonicalEnumBox),
     .align = _Alignof(CanonicalEnumBox),
     .type = &canonical_enum_storage}};
static const cmeta_struct_desc enum_row_layout = {
    "EnumRow", sizeof(EnumRow), _Alignof(EnumRow),
    enum_row_layout_fields,
    sizeof(enum_row_layout_fields) / sizeof(enum_row_layout_fields[0])};
static const cmeta_data_field_desc enum_row_fields[] = {
    {"test.native-enum.Row.id", "id", offsetof(EnumRow, id), &cmeta_data_int},
    {"test.native-enum.Row.state", "state", offsetof(EnumRow, state), &signed_enum_data}};
static const cmeta_data_struct_shape enum_row_shape = {
    &enum_row_layout, enum_row_fields,
    sizeof(enum_row_fields) / sizeof(enum_row_fields[0])};
static const cmeta_data_desc enum_row_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-enum.Row.data",
    .display_name = "EnumRow",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &enum_row_type,
    .shape = &enum_row_shape};

typedef struct LegacyEnumBox {
  int64_t value;
  bool engaged;
} LegacyEnumBox;

static bool legacy_enum_is_zero(const void *object) {
  const LegacyEnumBox *value = (const LegacyEnumBox *)object;
  return value != NULL && !value->engaged && value->value == 0;
}

static cmeta_status legacy_enum_read(const void *object, int64_t *out) {
  const LegacyEnumBox *value = (const LegacyEnumBox *)object;
  if (value == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  if (!value->engaged) return CMETA_CALLBACK_ERROR;
  *out = value->value;
  return CMETA_OK;
}

static cmeta_status legacy_enum_assign(void *object, int64_t value_) {
  LegacyEnumBox *value = (LegacyEnumBox *)object;
  if (value == NULL) return CMETA_INVALID_ARGUMENT;
  value->value = value_;
  value->engaged = true;
  return CMETA_OK;
}

static void legacy_enum_restore(void *object) {
  LegacyEnumBox *value = (LegacyEnumBox *)object;
  if (value == NULL) return;
  value->value = 0;
  value->engaged = false;
}

static const cmeta_type_identity legacy_enum_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-enum.Legacy");
static const cmeta_type_desc legacy_enum_storage = {
    .name = "LegacyEnumBox",
    .size = sizeof(LegacyEnumBox),
    .align = _Alignof(LegacyEnumBox),
    .kind = CMETA_T_OBJECT,
    .identity = &legacy_enum_identity};
static const cmeta_enum_item_desc legacy_items[] = {
    {0, "ZERO", "zero"}, {1, "READY", "ready"}};
static const cmeta_enum_desc legacy_meta = {
    "LegacyEnum", legacy_items,
    sizeof(legacy_items) / sizeof(legacy_items[0])};
static const cmeta_data_enum_shape legacy_shape = {&legacy_meta};
static const cmeta_data_enum_ops legacy_ops = {
    sizeof(cmeta_data_enum_ops), CMETA_DATA_ENUM_OPS_ABI_VERSION,
    &legacy_enum_storage,
    legacy_enum_is_zero, legacy_enum_read,
    legacy_enum_assign, legacy_enum_restore};
static const cmeta_data_desc legacy_enum_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-enum.Legacy.data",
    .display_name = "LegacyEnum",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &legacy_enum_storage,
    .shape = &legacy_shape,
    .enum_ops = &legacy_ops};

static EnumWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static NativeReaderProbe probe;
static cserde_reader reader;

static void open_enum_source(const NativeReaderProbeStep *steps, size_t count) {
  check_equal(native_reader_probe_open(&probe, steps, count, &reader), CSERDE_OK);
  check_equal(probe.calls, 0u);
}

static DataBindStatus decode_enum(const cmeta_data_desc *shape, void *destination,
                                  size_t bytes) {
  return data_bind_native_decode(&options, shape, &reader,
                                 destination, bytes, &diagnostic);
}

static NativeReaderProbeStep enum_string(const char *text) {
  return native_reader_probe_slice(
      CSERDE_STRING, (const unsigned char *)text, strlen(text),
      CSERDE_VIEW_TRANSIENT);
}

static uint64_t read_enum_bits(const cmeta_data_desc *shape,
                               const CanonicalEnumBox *value) {
  uint64_t bits = UINT64_MAX;
  check_equal(cmeta_data_enum_read_bits(shape, value, &bits), CMETA_OK);
  return bits;
}

typedef struct EnumOutput {
  char bytes[ENUM_OUTPUT_BYTES];
  size_t size;
} EnumOutput;

/* The fixture owns one workspace on one thread. Decode retains no token view;
 * a successful close precedes publication and provider-owned move/cleanup. */
static DataBindStatus replace_enum_json(const cmeta_data_desc *data, const char *json,
                                       CanonicalEnumBox *published) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  CanonicalEnumBox staging;
  DataBindStatus status, close_status;
  if (cmeta_data_value_init_zero(data, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  status = data_bind_format_reader_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
                                       json, strlen(json), ENUM_MAX_DEPTH, &lease, &error);
  if (status == DATA_BIND_OK)
    status = data_bind_native_decode(&options, data, lease.reader, &staging,
                                    sizeof(staging), &diagnostic);
  close_status = data_bind_format_reader_close(&lease);
  if (status == DATA_BIND_OK) status = close_status;
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(data, published);
    cmeta_data_trait_move_construct(data, published, &staging);
  }
  cmeta_data_value_destroy(data, &staging);
  return status;
}

static int enum_write(const void *bytes, size_t size, void *context) {
  EnumOutput *output = (EnumOutput *)context;
  if (size >= sizeof(output->bytes) - output->size) return -1;
  memcpy(output->bytes + output->size, bytes, size);
  output->size += size;
  output->bytes[output->size] = '\0';
  return 0;
}

static DataBindStatus encode_enum_json(const cmeta_data_desc *data,
                                      const CanonicalEnumBox *value, EnumOutput *output) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status, close_status;
  output->size = 0u;
  output->bytes[0] = '\0';
  status = data_bind_format_writer_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
                                       enum_write, output, ENUM_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_native_encode(&options, data, value, sizeof(*value),
                                  lease.writer, &diagnostic);
  close_status = data_bind_format_writer_close(&lease, &error);
  return status == DATA_BIND_OK ? close_status : status;
}

spec("DataBind canonical enum-bits native reader") {
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = ENUM_MAX_DEPTH;
    options.max_items = ENUM_MAX_ITEMS;
    options.max_owned_bytes = 64u;
    wide_domain = signed_domain;
    wide_domain.signedness = CMETA_ENUM_UNSIGNED;
    wide_domain.bits = ENUM_WIDE_BITS;
    wide_domain.items = wide_items;
    wide_domain.count = sizeof(wide_items) / sizeof(wide_items[0]);
    wide_ops = signed_ops;
    wide_ops.domain = &wide_domain;
    wide_enum_data = signed_enum_data;
    wide_enum_data.stable_id = "test.native-enum.Wide.data";
    wide_enum_data.enum_bits_ops = &wide_ops;
    wide_flag_domain = flag_domain;
    wide_flag_domain.bits = ENUM_WIDE_BITS;
    wide_flag_domain.items = wide_flag_items;
    wide_flag_domain.count = sizeof(wide_flag_items) / sizeof(wide_flag_items[0]);
    wide_flag_domain.declared_mask = WIDE_FLAGS_MASK;
    wide_flag_ops = flag_ops;
    wide_flag_ops.domain = &wide_flag_domain;
    wide_flag_data = flag_enum_data;
    wide_flag_data.stable_id = "test.native-enum.WideFlags.data";
    wide_flag_data.enum_bits_ops = &wide_flag_ops;
    canonical_assign_fails = false;
    canonical_assign_calls = 0u;
    canonical_restore_calls = 0u;
    check_true(cmeta_data_desc_valid(&signed_enum_data));
    check_true(cmeta_data_desc_valid(&flag_enum_data));
    check_true(cmeta_data_desc_valid(&enum_row_data));
    check_true(cmeta_data_desc_valid(&legacy_enum_data));
  }

  after_each() {
    if (message_codec != NULL) {
      cmeta_data_value_destroy(&wide_record_data, &message_value);
      data_bind_message_plan_free(message_plan);
      data_bind_free(message_codec);
      message_plan = NULL;
      message_codec = NULL;
    }
  }

  it("initializes and clears canonical enum provider state") {
    CanonicalEnumBox value = {77u, true};
    check_equal(data_bind_native_init(&options, &signed_enum_data, &value,
                                      sizeof(value), &diagnostic), DATA_BIND_OK);
    check_true(canonical_enum_is_zero(&value));
    check_true(canonical_restore_calls >= 1u);
    check_equal(cmeta_data_enum_assign_bits(&signed_enum_data, &value, 1u), CMETA_OK);
    check_equal(read_enum_bits(&signed_enum_data, &value), UINT64_C(1));
    check_equal(data_bind_native_clear(&options, &signed_enum_data, &value,
                                       sizeof(value), &diagnostic), DATA_BIND_OK);
    check_true(canonical_enum_is_zero(&value));
  }

  it("decodes a signed CSerde integer into width-bit canonical enum bits") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(-1)};
    CanonicalEnumBox value = {0};
    open_enum_source(steps, 1u);
    check_equal(decode_enum(&signed_enum_data, &value, sizeof(value)), DATA_BIND_OK);
    check_equal(read_enum_bits(&signed_enum_data, &value), UINT64_C(255));
    check_equal(probe.calls, 1u);
    canonical_enum_restore(&value);
  }

  it("decodes canonical enum symbol and text strings") {
    const NativeReaderProbeStep symbol[] = {enum_string("READY")};
    const NativeReaderProbeStep text[] = {enum_string("negative")};
    CanonicalEnumBox value = {0};
    open_enum_source(symbol, 1u);
    check_equal(decode_enum(&signed_enum_data, &value, sizeof(value)), DATA_BIND_OK);
    check_equal(read_enum_bits(&signed_enum_data, &value), UINT64_C(1));
    canonical_enum_restore(&value);
    reader = (cserde_reader){0};
    open_enum_source(text, 1u);
    check_equal(decode_enum(&signed_enum_data, &value, sizeof(value)), DATA_BIND_OK);
    check_equal(read_enum_bits(&signed_enum_data, &value), UINT64_C(255));
    canonical_enum_restore(&value);
  }

  it("decodes an unsigned flags value through the canonical domain") {
    NativeReaderProbeStep step = native_reader_probe_token(CSERDE_UINT);
    CanonicalEnumBox value = {0};
    step.token.value.uint = 5u;
    open_enum_source(&step, 1u);
    check_equal(decode_enum(&flag_enum_data, &value, sizeof(value)), DATA_BIND_OK);
    check_equal(read_enum_bits(&flag_enum_data, &value), UINT64_C(5));
    canonical_enum_restore(&value);
  }

  it("decodes a canonical enum field inside a Struct") {
    static const unsigned char state_key[] = "state";
    static const unsigned char id_key[] = "id";
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN),
        native_reader_probe_slice(CSERDE_STRING, state_key, sizeof(state_key) - 1u,
                                  CSERDE_VIEW_STABLE),
        enum_string("READY"),
        native_reader_probe_slice(CSERDE_STRING, id_key, sizeof(id_key) - 1u,
                                  CSERDE_VIEW_STABLE),
        native_reader_probe_sint(7),
        native_reader_probe_token(CSERDE_MAP_END)};
    EnumRow row = {0};
    open_enum_source(steps, sizeof(steps) / sizeof(steps[0]));
    check_equal(decode_enum(&enum_row_data, &row, sizeof(row)), DATA_BIND_OK);
    check_equal(row.id, 7);
    check_equal(read_enum_bits(&signed_enum_data, &row.state), UINT64_C(1));
    check_equal(probe.calls, 6u);
    canonical_enum_restore(&row.state);
  }

  it("rejects undeclared canonical enum bits without publishing") {
    NativeReaderProbeStep step = native_reader_probe_token(CSERDE_UINT);
    CanonicalEnumBox value = {0};
    step.token.value.uint = 2u;
    open_enum_source(&step, 1u);
    check_equal(decode_enum(&signed_enum_data, &value, sizeof(value)),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(canonical_enum_is_zero(&value));
    check_equal(diagnostic.endpoint_status, CSERDE_OK);
  }

  it("rejects an unknown enum string without calling the provider") {
    const NativeReaderProbeStep steps[] = {enum_string("MISSING")};
    CanonicalEnumBox value = {0};
    open_enum_source(steps, 1u);
    check_equal(decode_enum(&signed_enum_data, &value, sizeof(value)),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(canonical_enum_is_zero(&value));
    check_equal(canonical_assign_calls, 0u);
  }

  it("rolls provider assignment failure back to semantic zero") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(1)};
    CanonicalEnumBox value = {0};
    canonical_assign_fails = true;
    open_enum_source(steps, 1u);
    check_equal(decode_enum(&signed_enum_data, &value, sizeof(value)),
                DATA_BIND_ERR_RUNTIME);
    check_true(canonical_enum_is_zero(&value));
    check_equal(canonical_assign_calls, 1u);
    check_true(canonical_restore_calls >= 1u);
  }

  it("round-trips UINT64_MAX and high flags through the JSON provider") {
    CanonicalEnumBox value = {0};
    EnumOutput output;
    check_equal(replace_enum_json(&wide_enum_data, "\"Max\"", &value), DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_enum_data, &value), UINT64_MAX);
    check_equal(encode_enum_json(&wide_enum_data, &value, &output), DATA_BIND_OK);
    check_equal(output.bytes, "18446744073709551615");
    cmeta_data_value_destroy(&wide_enum_data, &value);
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",\"High\"]", &value), DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_flag_data, &value), WIDE_FLAGS_MASK);
    check_equal(encode_enum_json(&wide_flag_data, &value, &output), DATA_BIND_OK);
    check_equal(output.bytes, "9223372036854775809");
    cmeta_data_value_destroy(&wide_flag_data, &value);
    check_true(canonical_enum_is_zero(&value));
  }

  it("binds a flags array and its following field through MessagePlan") {
    static const char schema[] =
        "flags WideFlags <uint64> { Low = 1; High = 9223372036854775808; } "
        "message WideFlagsRecord { WideFlags value; uint32 marker; }";
    static const char json[] = "{\"value\":[\"Low\",\"High\"],\"marker\":7}";
    DataBindMessagePlanDiagnostic message_diagnostic = DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus status, close_status;
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u,
                                          &message_codec, &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_compile(message_codec, "WideFlagsRecord",
                                               &wide_record_binding, &message_plan,
                                               &message_diagnostic), DATA_BIND_OK);
    check_equal(data_bind_format_reader_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
                                             json, sizeof(json) - 1u, ENUM_MAX_DEPTH,
                                             &lease, &error), DATA_BIND_OK);
    status = data_bind_message_plan_decode_native(message_plan, &options, lease.reader,
                                                  &message_value, sizeof(message_value),
                                                  &message_diagnostic);
    close_status = data_bind_format_reader_close(&lease);
    check_equal(status, DATA_BIND_OK);
    check_equal(close_status, DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_flag_data, &message_value.value), WIDE_FLAGS_MASK);
    check_equal(message_value.marker, 7u);
    cmeta_data_value_destroy(&wide_record_data, &message_value);

    /* The array members contribute to the whole-message budget, so a later
     * field cannot bypass the limit by entering a separate native decode. */
    options.max_items = 4u;
    check_equal(data_bind_format_reader_open(data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
                                             json, sizeof(json) - 1u, ENUM_MAX_DEPTH,
                                             &lease, &error), DATA_BIND_OK);
    status = data_bind_message_plan_decode_native(message_plan, &options, lease.reader,
                                                  &message_value, sizeof(message_value),
                                                  &message_diagnostic);
    close_status = data_bind_format_reader_close(&lease);
    check_equal(status, DATA_BIND_ERR_LIMIT);
    check_equal(close_status, DATA_BIND_OK);
    check_true(canonical_enum_is_zero(&message_value.value));
    check_equal(message_value.marker, 0u);
  }

  it("combines nested flags once and consumes exactly one value") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN), enum_string("Low"),
        native_reader_probe_token(CSERDE_ARRAY_BEGIN), enum_string("High"),
        native_reader_probe_sint(1), native_reader_probe_token(CSERDE_ARRAY_END),
        native_reader_probe_token(CSERDE_ARRAY_END), native_reader_probe_sint(7)};
    CanonicalEnumBox value = {0};
    cserde_token next = {0};
    open_enum_source(steps, sizeof(steps) / sizeof(steps[0]));
    check_equal(decode_enum(&wide_flag_data, &value, sizeof(value)), DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_flag_data, &value), WIDE_FLAGS_MASK);
    check_equal(canonical_assign_calls, 1u);
    check_equal(probe.calls, 7u);
    check_equal(cserde_reader_next(&reader, &next), CSERDE_OK);
    check_equal(next.value.sint, INT64_C(7));
    cmeta_data_value_destroy(&wide_flag_data, &value);
  }

  it("accepts empty flags and rejects arrays for ordinary enums") {
    CanonicalEnumBox value = {0};
    check_equal(replace_enum_json(&wide_flag_data, "[]", &value), DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_flag_data, &value), UINT64_C(0));
    cmeta_data_value_destroy(&wide_flag_data, &value);
    canonical_assign_calls = 0;
    check_equal(replace_enum_json(&wide_enum_data, "[]", &value), DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(canonical_enum_is_zero(&value));
    check_equal(canonical_assign_calls, 0u);
  }

  it("keeps published flags after a late invalid member and permits reuse") {
    CanonicalEnumBox value = {0};
    check_equal(replace_enum_json(&wide_flag_data, "\"High\"", &value), DATA_BIND_OK);
    canonical_assign_calls = 0;
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",\"MISSING\"]", &value),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(read_enum_bits(&wide_flag_data, &value), UINT64_C(1) << ENUM_HIGH_BIT);
    check_equal(canonical_assign_calls, 0u);
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",2]", &value),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(read_enum_bits(&wide_flag_data, &value), UINT64_C(1) << ENUM_HIGH_BIT);
    check_equal(canonical_assign_calls, 0u);
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",\"High\"]", &value), DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_flag_data, &value), WIDE_FLAGS_MASK);
    cmeta_data_value_destroy(&wide_flag_data, &value);
  }

  it("enforces item and nesting budgets before assigning flags") {
    CanonicalEnumBox value = {0};
    options.max_items = 2u;
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",\"High\"]", &value), DATA_BIND_ERR_LIMIT);
    check_true(canonical_enum_is_zero(&value));
    check_equal(canonical_assign_calls, 0u);
    options.max_items = ENUM_MAX_ITEMS;
    options.max_depth = 2u;
    check_equal(replace_enum_json(&wide_flag_data, "[[\"Low\"]]", &value), DATA_BIND_ERR_LIMIT);
    check_true(canonical_enum_is_zero(&value));
    check_equal(canonical_assign_calls, 0u);
    options.max_items = 3u;
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",\"High\"]", &value), DATA_BIND_OK);
    check_equal(read_enum_bits(&wide_flag_data, &value), WIDE_FLAGS_MASK);
    cmeta_data_value_destroy(&wide_flag_data, &value);
  }

  it("does not assign a truncated flags array or a failing source") {
    const NativeReaderProbeStep truncated[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN), enum_string("Low")};
    const NativeReaderProbeStep failed[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN), enum_string("Low"),
        native_reader_probe_error(CSERDE_SOURCE_ERROR)};
    CanonicalEnumBox value = {0};
    open_enum_source(truncated, sizeof(truncated) / sizeof(truncated[0]));
    check_equal(decode_enum(&wide_flag_data, &value, sizeof(value)), DATA_BIND_ERR_PARSE);
    check_equal(canonical_assign_calls, 0u);
    check_true(canonical_enum_is_zero(&value));
    reader = (cserde_reader){0};
    open_enum_source(failed, sizeof(failed) / sizeof(failed[0]));
    check_equal(decode_enum(&wide_flag_data, &value, sizeof(value)), DATA_BIND_ERR_IO);
    check_equal(diagnostic.endpoint_status, CSERDE_SOURCE_ERROR);
    check_equal(canonical_assign_calls, 0u);
    check_true(canonical_enum_is_zero(&value));
  }

  it("rolls back a flags provider failure before publishing a replacement") {
    CanonicalEnumBox value = {0};
    check_equal(replace_enum_json(&wide_flag_data, "\"High\"", &value), DATA_BIND_OK);
    canonical_assign_fails = true;
    canonical_assign_calls = 0;
    check_equal(replace_enum_json(&wide_flag_data, "[\"Low\",\"High\"]", &value), DATA_BIND_ERR_RUNTIME);
    check_equal(canonical_assign_calls, 1u);
    check_equal(read_enum_bits(&wide_flag_data, &value), UINT64_C(1) << ENUM_HIGH_BIT);
    cmeta_data_value_destroy(&wide_flag_data, &value);
  }

  it("rejects legacy enum_ops before consuming input") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(1)};
    LegacyEnumBox value = {0};
    open_enum_source(steps, 1u);
    check_equal(decode_enum(&legacy_enum_data, &value, sizeof(value)),
                DATA_BIND_ERR_SCHEMA);
    check_equal(probe.calls, 0u);
    check_true(legacy_enum_is_zero(&value));
  }
}
