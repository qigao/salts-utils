#include "data_bind_message_plan.h"
#include "data_bind_format_provider.h"
#include "data_bind_projection_plan.h"
#include "tinytest.h"

#include <cmeta/data_reflect.h>
#include <cmeta_cmeta_data.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  NATIVE_BENCH_SAMPLES = 10000,
  NATIVE_BENCH_WORKSPACE_BYTES = 4096,
  NATIVE_BENCH_OUTPUT_BYTES = 128,
  NATIVE_BENCH_MAX_DEPTH = 4,
  NATIVE_BENCH_MAX_ITEMS = 16,
  NATIVE_BENCH_MAX_OWNED_BYTES = 64
};

typedef struct NativeBenchOrder {
  uint32_t id;
  tstr symbol;
  double price;
} NativeBenchOrder;
cmeta_reflect_value(NativeBenchOrder, "benchmark.native.Order.data",
    cmeta_data_field_id(uint32_t, id, "benchmark.native.Order.id",
        &cmeta_data_uint32, &cmeta_type_uint32)
    cmeta_data_field_id(tstr, symbol, "benchmark.native.Order.symbol",
        SALTS_TSTR_CMETA_DATA_REF, SALTS_TSTR_CMETA_TYPE_REF)
    cmeta_data_field_id(double, price, "benchmark.native.Order.price",
        &cmeta_data_double, &cmeta_type_double)
);
static const DataBindNativeTypeBinding ORDER_BINDING =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("Order", cmeta_reflected_data(NativeBenchOrder));
static const char ORDER_SCHEMA[] =
    "message Order { [name(orderId), alias(legacyId)] uint32 id; double price; string symbol; }";
static const char ORDER_JSON[] =
    "{\"legacyId\":42,\"symbol\":\"TURBO\",\"price\":123.5}";
static const char ORDER_OUTPUT[] =
    "{\"orderId\":42,\"price\":123.5,\"symbol\":\"TURBO\"}";

typedef union NativeBenchWorkspace {
  uint64_t integer_alignment;
  void *pointer_alignment;
  unsigned char bytes[NATIVE_BENCH_WORKSPACE_BYTES];
} NativeBenchWorkspace;

typedef struct NativeBenchOutput {
  char bytes[NATIVE_BENCH_OUTPUT_BYTES];
  size_t size;
  size_t capacity;
} NativeBenchOutput;

/* One thread owns the workspace, object and leases. Plans borrow the immutable
 * codec/metadata until teardown; no token view survives a provider close. */
static DataBind *codec;
static DataBindMessagePlan *message_plan;
static DataBindFormatPlan *format_plan;
static DataBindNativeOptions options;
static DataBindMessagePlanDiagnostic diagnostic;
static DataBindError error = DATA_BIND_ERROR_INIT;
static NativeBenchWorkspace workspace;
static NativeBenchOrder order;

static int native_bench_write(const void *data, size_t size, void *context) {
  NativeBenchOutput *out = (NativeBenchOutput *)context;
  if (size > out->capacity - out->size) return -1;
  memcpy(out->bytes + out->size, data, size);
  out->size += size;
  return 0;
}

static DataBindStatus native_bench_replace(const char *input, size_t size) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindFormatCanonicalReader canonical = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
  NativeBenchOrder staging;
  DataBindStatus status, close_status;
  if (cmeta_data_value_init_zero(cmeta_reflected_data(NativeBenchOrder), &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  status = data_bind_format_reader_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), input, size,
      NATIVE_BENCH_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_format_canonical_reader_init(format_plan, lease.reader, &canonical, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_decode_native(
      message_plan, &options, data_bind_format_canonical_reader_reader(&canonical),
      &staging, sizeof(staging), &diagnostic);
cleanup:
  close_status = data_bind_format_reader_close(&lease);
  if (status == DATA_BIND_OK) status = close_status;
  if (status == DATA_BIND_OK) {
    /* Supported CMeta lifecycle makes publication no-fail. A rejected input
     * leaves the old value intact; a successful replacement releases it once. */
    cmeta_data_value_destroy(cmeta_reflected_data(NativeBenchOrder), &order);
    cmeta_data_trait_move_construct(cmeta_reflected_data(NativeBenchOrder), &order, &staging);
  }
  cmeta_data_value_destroy(cmeta_reflected_data(NativeBenchOrder), &staging);
  return status;
}

static DataBindStatus native_bench_encode(NativeBenchOutput *out) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindFormatCanonicalWriter canonical = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
  DataBindStatus status, close_status;
  out->size = 0;
  status = data_bind_format_writer_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), native_bench_write,
      out, NATIVE_BENCH_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_format_canonical_writer_init(format_plan, lease.writer, &canonical, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_encode_native(
      message_plan, &options, &order, sizeof(order),
      data_bind_format_canonical_writer_writer(&canonical), &diagnostic);
  if (status == DATA_BIND_OK && cserde_writer_finish(
          data_bind_format_canonical_writer_writer(&canonical)) != CSERDE_OK)
    status = DATA_BIND_ERR_RUNTIME;
cleanup:
  close_status = data_bind_format_writer_close(&lease, &error);
  return status == DATA_BIND_OK ? close_status : status;
}

spec("DataBind native JSON benchmark") {
  before_all() {
    check_true(cmeta_data_desc_valid(cmeta_reflected_data(NativeBenchOrder)));
    check_true(cmeta_data_value_move_supported(cmeta_reflected_data(NativeBenchOrder)));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = NATIVE_BENCH_MAX_DEPTH;
    options.max_items = NATIVE_BENCH_MAX_ITEMS;
    options.max_owned_bytes = NATIVE_BENCH_MAX_OWNED_BYTES;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindStatus status = data_bind_create_from_text(
        ORDER_SCHEMA, sizeof(ORDER_SCHEMA) - 1u, &codec, &error);
    info("schema: %s", error.message);
    check_equal(status, DATA_BIND_OK);
    check_equal(data_bind_message_plan_compile(codec, "Order", &ORDER_BINDING,
                                               &message_plan, &diagnostic), DATA_BIND_OK);
    check_equal(data_bind_format_plan_compile(codec, "Order", DATA_BIND_FORMAT_JSON,
                                              &format_plan, &error), DATA_BIND_OK);
  }
  before_each() {
    check_equal(cmeta_data_value_init_zero(cmeta_reflected_data(NativeBenchOrder), &order), CMETA_OK);
    DataBindStatus status = native_bench_replace(ORDER_JSON, sizeof(ORDER_JSON) - 1u);
    info("decode: %s; provider: %s", diagnostic.message, error.message);
    check_equal(status, DATA_BIND_OK);
  }
  after_each() {
    cmeta_data_value_destroy(cmeta_reflected_data(NativeBenchOrder), &order);
    check_true(cmeta_tstr_cmeta_buffer_ops.is_zero(&order.symbol));
  }
  after_all() {
    data_bind_format_plan_free(format_plan);
    data_bind_message_plan_free(message_plan);
    data_bind_free(codec);
  }

  it("owns decoded text after reader close and writes the external field name") {
    NativeBenchOutput out = {.capacity = NATIVE_BENCH_OUTPUT_BYTES};
    check_equal(order.id, 42u);
    check_equal(order.symbol, "TURBO");
    check_equal(order.price, 123.5);
    check_equal(native_bench_encode(&out), DATA_BIND_OK);
    check_equal(out.size, sizeof(ORDER_OUTPUT) - 1u);
    check_equal(memcmp(out.bytes, ORDER_OUTPUT, out.size), 0);
  }

  it("preserves published storage when decoding fails after an owned string") {
    static const char rejected[] =
        "{\"legacyId\":7,\"symbol\":\"staged\",\"price\":\"invalid\"}";
    tstr published = order.symbol;
    check_not_equal(native_bench_replace(rejected, sizeof(rejected) - 1u), DATA_BIND_OK);
    check_true(order.symbol == published);
    check_equal(order.symbol, "TURBO");
    check_equal(order.id, 42u);
    check_equal(order.price, 123.5);
    check_equal(native_bench_replace(ORDER_JSON, sizeof(ORDER_JSON) - 1u), DATA_BIND_OK);
  }

  it("reports a full sink without consuming native ownership and can encode again") {
    NativeBenchOutput out = {.capacity = 0};
    tstr published = order.symbol;
    check_not_equal(native_bench_encode(&out), DATA_BIND_OK);
    check_equal(out.size, 0u);
    check_true(order.symbol == published);
    out.capacity = sizeof(out.bytes);
    check_equal(native_bench_encode(&out), DATA_BIND_OK);
    check_equal(out.size, sizeof(ORDER_OUTPUT) - 1u);
    check_equal(memcmp(out.bytes, ORDER_OUTPUT, out.size), 0);
  }

  bench("native JSON conversion with precompiled plans") {
    size_t sink = 0, failures = 0;
    NativeBenchOutput out = {.capacity = NATIVE_BENCH_OUTPUT_BYTES};
    /* Each sample includes provider open/close. Decode also includes staged
     * publication and release of the previous value; encode uses a fixed sink.
     * Bytes count one complete input/output document, excluding the terminator. */
    benchmark_bytes("JSON to native replacement", NATIVE_BENCH_SAMPLES, sizeof(ORDER_JSON) - 1u) {
      if (native_bench_replace(ORDER_JSON, sizeof(ORDER_JSON) - 1u) == DATA_BIND_OK)
        sink += order.id;
      else
        ++failures;
    }
    benchmark_bytes("native to bounded JSON sink", NATIVE_BENCH_SAMPLES, sizeof(ORDER_OUTPUT) - 1u) {
      if (native_bench_encode(&out) == DATA_BIND_OK)
        sink += out.size;
      else
        ++failures;
    }
    check_equal(failures, 0u);
    check_greater(sink, 0u);
    check_equal(order.symbol, "TURBO");
    check_equal(out.size, sizeof(ORDER_OUTPUT) - 1u);
    check_equal(memcmp(out.bytes, ORDER_OUTPUT, out.size), 0);
  }
}
