#include "data_bind_native.h"
#include "data_bind_format_provider.h"
#include "native_test_alignment.h"
#include "tinytest.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
  SCALAR_WORKSPACE_BYTES = 4096,
  SCALAR_OUTPUT_BYTES = 128,
  SCALAR_MAX_DEPTH = 4,
  SCALAR_MAX_ITEMS = 16,
  SCALAR_MAX_OWNED_BYTES = 64
};

typedef union ScalarStorage {
  int8_t i8;
  uint8_t u8;
  int16_t i16;
  uint16_t u16;
  int32_t i32;
  uint32_t u32;
  int64_t i64;
  uint64_t u64;
  float f32;
  double f64;
  bool boolean;
  salts_uuid_t uuid;
} ScalarStorage;

typedef union ScalarWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[SCALAR_WORKSPACE_BYTES];
} ScalarWorkspace;

typedef struct ScalarOutput {
  char bytes[SCALAR_OUTPUT_BYTES];
  size_t size;
} ScalarOutput;

static ScalarWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static DataBindError error;

static const cmeta_type_identity SCALAR_RECORD_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.ScalarStorage");

static DataBindStatus scalar_plan_status(const cmeta_data_desc *value) {
  const cmeta_type_desc root_type = {
      "ScalarStorage", sizeof(ScalarStorage), _Alignof(ScalarStorage),
      CMETA_T_OBJECT, NULL, NULL, &SCALAR_RECORD_ID};
  const cmeta_field_desc layout_field = {
      "value", value && value->storage_type ? value->storage_type->name : "invalid",
      0u, value && value->storage_type ? value->storage_type->size : 0u,
      value && value->storage_type ? value->storage_type->align : 1u,
      value ? value->storage_type : NULL, NULL};
  const cmeta_struct_desc layout = {
      "ScalarStorage", sizeof(ScalarStorage), _Alignof(ScalarStorage),
      &layout_field, 1u};
  const cmeta_data_field_desc data_field = {
      "test.ScalarStorage.value", "value", 0u, value};
  const cmeta_data_struct_shape shape = {&layout, &data_field, 1u};
  const cmeta_data_desc root_data = {
      sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
      "test.ScalarStorage.data", "ScalarStorage", CMETA_DATA_STRUCT,
      &root_type, &shape, NULL, NULL, NULL};
  DataBindNativePlan *plan = NULL;
  DataBindStatus status = data_bind_native_plan_compile(&options, &root_data, &plan, &diagnostic);
  data_bind_native_plan_free(plan);
  return status;
}

static void expect_scalar(const cmeta_data_desc *data) {
  check_equal(scalar_plan_status(data), DATA_BIND_OK);
}

/* These helpers own only provider leases and temporary scalar storage. The
 * caller owns the published value. No view survives reader close, and one
 * thread exclusively owns the fixed workspace throughout each operation. */
static DataBindStatus replace_scalar(const cmeta_data_desc *data, const char *json,
                                     ScalarStorage *published) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  ScalarStorage staging;
  DataBindStatus status, close_status;
  if (cmeta_data_value_init_zero(data, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  status = data_bind_format_reader_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), json, strlen(json),
      SCALAR_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_native_decode(&options, data, lease.reader, &staging,
                                  sizeof(staging), &diagnostic);
cleanup:
  close_status = data_bind_format_reader_close(&lease);
  if (status == DATA_BIND_OK) status = close_status;
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(data, published);
    cmeta_data_trait_move_construct(data, published, &staging);
  }
  cmeta_data_value_destroy(data, &staging);
  return status;
}

static int scalar_write(const void *bytes, size_t size, void *context) {
  ScalarOutput *output = (ScalarOutput *)context;
  if (size >= sizeof(output->bytes) - output->size) return -1;
  memcpy(output->bytes + output->size, bytes, size);
  output->size += size;
  output->bytes[output->size] = '\0';
  return 0;
}

static DataBindStatus encode_scalar(const cmeta_data_desc *data,
                                    const ScalarStorage *source, ScalarOutput *output) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindStatus status, close_status;
  output->size = 0;
  output->bytes[0] = '\0';
  status = data_bind_format_writer_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), scalar_write, output,
      SCALAR_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_native_encode(&options, data, source, sizeof(*source),
                                  lease.writer, &diagnostic);
  close_status = data_bind_format_writer_close(&lease, &error);
  return status == DATA_BIND_OK ? close_status : status;
}

spec("DataBind native scalar contracts") {
  before_each() {
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = SCALAR_MAX_DEPTH;
    options.max_items = SCALAR_MAX_ITEMS;
    options.max_owned_bytes = SCALAR_MAX_OWNED_BYTES;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
  }

  it("admits fixed-width integers and floats inside a canonical Struct") {
    expect_scalar(&cmeta_data_int8);
    expect_scalar(&cmeta_data_uint8);
    expect_scalar(&cmeta_data_int16);
    expect_scalar(&cmeta_data_uint16);
    expect_scalar(&cmeta_data_int32);
    expect_scalar(&cmeta_data_uint32);
    expect_scalar(&cmeta_data_int64);
    expect_scalar(&cmeta_data_uint64);
    expect_scalar(&cmeta_data_float);
    expect_scalar(&cmeta_data_double);
  }

  it("matches copied scalar descriptors by semantic identity") {
    cmeta_data_desc data = cmeta_data_int32;
    cmeta_type_desc type = *data.storage_type;
    cmeta_type_identity identity = *type.identity;
    type.identity = &identity;
    data.storage_type = &type;

    check_true(&data != &cmeta_data_int32);
    check_true(cmeta_type_equal(data.storage_type, cmeta_data_int32.storage_type));
    expect_scalar(&data);
    ScalarStorage value;
    ScalarOutput output;
    check_equal(cmeta_data_value_init_zero(&data, &value), CMETA_OK);
    check_equal(replace_scalar(&data, "-2147483648", &value), DATA_BIND_OK);
    check_equal(value.i32, INT32_MIN);
    check_equal(encode_scalar(&data, &value, &output), DATA_BIND_OK);
    check_equal(output.bytes, "-2147483648");
    cmeta_data_value_destroy(&data, &value);
  }

  it("admits canonical Bool and UUID providers without container impostors") {
    static const cmeta_data_kind containers[] = {
        CMETA_DATA_SEQUENCE, CMETA_DATA_SET, CMETA_DATA_MAP};
    expect_scalar(&cmeta_data_bool);
    expect_scalar(&salts_bool8_cmeta_data);
    expect_scalar(&salts_uuid_cmeta_data);
    for (size_t i = 0; i < sizeof(containers) / sizeof(containers[0]); ++i) {
      cmeta_data_desc malformed = cmeta_data_int32;
      malformed.kind = containers[i];
      check_equal(scalar_plan_status(&malformed), DATA_BIND_ERR_SCHEMA);
    }
    check_equal(scalar_plan_status(NULL), DATA_BIND_ERR_SCHEMA);
  }

  it("round-trips integer bounds and finite floats through the JSON provider") {
    /* Imported provider addresses are resolved at runtime on Windows. */
    const struct {
      const cmeta_data_desc *data;
      const char *json;
    } cases[] = {
        {&cmeta_data_int8, "-128"}, {&cmeta_data_int8, "127"},
        {&cmeta_data_uint8, "255"},
        {&cmeta_data_int16, "-32768"}, {&cmeta_data_int16, "32767"},
        {&cmeta_data_uint16, "65535"},
        {&cmeta_data_int32, "-2147483648"}, {&cmeta_data_int32, "2147483647"},
        {&cmeta_data_uint32, "4294967295"},
        {&cmeta_data_int64, "-9223372036854775808"},
        {&cmeta_data_int64, "9223372036854775807"},
        {&cmeta_data_uint64, "18446744073709551615"},
        {&cmeta_data_float, "1.25"}, {&cmeta_data_double, "2.5"},
        {&cmeta_data_bool, "true"}, {&salts_bool8_cmeta_data, "false"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      ScalarStorage value;
      ScalarOutput output;
      info("type=%s input=%s", cases[i].data->display_name, cases[i].json);
      check_equal(cmeta_data_value_init_zero(cases[i].data, &value), CMETA_OK);
      check_equal(replace_scalar(cases[i].data, cases[i].json, &value), DATA_BIND_OK);
      DataBindStatus status = encode_scalar(cases[i].data, &value, &output);
      info("encode: %s", diagnostic.error.message);
      check_equal(status, DATA_BIND_OK);
      check_equal(output.bytes, cases[i].json);
      cmeta_data_value_destroy(cases[i].data, &value);
    }
  }

  it("decodes UUID text and restores its canonical fixed storage") {
    static const char json[] = "\"00112233-4455-6677-8899-aabbccddeeff\"";
    static const unsigned char expected[SALTS_UUID_SIZE] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    ScalarStorage value;
    check_equal(cmeta_data_value_init_zero(&salts_uuid_cmeta_data, &value), CMETA_OK);
    check_equal(replace_scalar(&salts_uuid_cmeta_data, json, &value), DATA_BIND_OK);
    check_equal(value.uuid.bytes, expected, sizeof(expected));
    cmeta_data_value_destroy(&salts_uuid_cmeta_data, &value);
    check_true(salts_uuid_cmeta_fixed_ops.is_zero(&value.uuid));
    cmeta_data_value_destroy(&salts_uuid_cmeta_data, &value);
    check_equal(replace_scalar(&salts_uuid_cmeta_data, json, &value), DATA_BIND_OK);
    check_equal(value.uuid.bytes, expected, sizeof(expected));
    cmeta_data_value_destroy(&salts_uuid_cmeta_data, &value);
  }

  it("preserves a published float on positive and negative float32 overflow") {
    ScalarStorage value = {.f32 = 1.0f};
    check_equal(replace_scalar(&cmeta_data_float, "3.5e38", &value), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.f32, 1.0f);
    check_equal(replace_scalar(&cmeta_data_float, "-3.5e38", &value), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.f32, 1.0f);
    check_equal(replace_scalar(&cmeta_data_float, "1.25", &value), DATA_BIND_OK);
    check_equal(value.f32, 1.25f);
    cmeta_data_value_destroy(&cmeta_data_float, &value);
  }

  it("rejects non-finite JSON output without modifying the native scalar") {
    ScalarStorage value = {.f32 = NAN};
    ScalarOutput output;
    check_equal(encode_scalar(&cmeta_data_float, &value, &output), DATA_BIND_ERR_SCHEMA);
    check_equal(diagnostic.source_status, CSERDE_UNSUPPORTED);
    check_true(isnan(value.f32));
    check_equal(output.size, 0u);
    cmeta_data_value_destroy(&cmeta_data_float, &value);
    value.f64 = INFINITY;
    check_equal(encode_scalar(&cmeta_data_double, &value, &output), DATA_BIND_ERR_SCHEMA);
    check_equal(diagnostic.source_status, CSERDE_UNSUPPORTED);
    check_true(isinf(value.f64));
    check_greater(value.f64, 0.0);
    check_equal(output.size, 0u);
    cmeta_data_value_destroy(&cmeta_data_double, &value);
    check_equal(replace_scalar(&cmeta_data_double, "2.5", &value), DATA_BIND_OK);
    check_equal(encode_scalar(&cmeta_data_double, &value, &output), DATA_BIND_OK);
    check_equal(output.bytes, "2.5");
    cmeta_data_value_destroy(&cmeta_data_double, &value);
  }
}
