#include "tbe_typed.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TBE_TYPED_BENCH_SAMPLES 10000U

typedef struct TypedBenchOrder {
  uint32_t id;
  tstr_t symbol;
  double price;
} TypedBenchOrder;

TBE_TYPED_DEFINE_STRUCT(
    TYPED_BENCH_ORDER, TypedBenchOrder, "Order",
    TBE_TYPED_FIELD(TypedBenchOrder, id, "id", TBE_TYPED_U32, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(TypedBenchOrder, symbol, "symbol", TBE_TYPED_STRING, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(TypedBenchOrder, price, "price", TBE_TYPED_F64, TBE_TYPED_REQUIRED));

static const char TYPED_BENCH_SCHEMA[] =
    "message Order { "
    "[name(orderId), alias(legacyId)] uint32 id; "
    "string symbol; "
    "double price; "
    "}";
static const char TYPED_BENCH_JSON[] =
    "{\"legacyId\":42,\"symbol\":\"TURBO\",\"price\":123.5}";
static const char TYPED_BENCH_OUTPUT[] =
    "{\"orderId\":42,\"symbol\":\"TURBO\",\"price\":123.5}";

static DataBind *g_typed_bench_codec;
static TypedBenchOrder g_typed_bench_order;
static DataBindError g_typed_bench_error = DATA_BIND_ERROR_INIT;
static size_t g_typed_bench_sink;
static size_t g_typed_bench_failures;

spec("DataBind typed benchmarks") {
  before_all() {
    check_int_eq(data_bind_create_from_text(TYPED_BENCH_SCHEMA, sizeof(TYPED_BENCH_SCHEMA) - 1u,
                                            &g_typed_bench_codec, &g_typed_bench_error),
                 DATA_BIND_OK);
    check_not_null(g_typed_bench_codec);
    check_int_eq(TBE_TYPED_BIND_INIT(TYPED_BENCH_ORDER, &g_typed_bench_order,
                                     &g_typed_bench_error),
                 DATA_BIND_OK);
    if (g_typed_bench_codec != NULL) {
      check_int_eq(TBE_TYPED_BIND_PARSE(g_typed_bench_codec, TYPED_BENCH_ORDER, "json",
                                        TYPED_BENCH_JSON, sizeof(TYPED_BENCH_JSON) - 1u, 0u,
                                        &g_typed_bench_order, &g_typed_bench_error),
                   DATA_BIND_OK);
    }
  }

  after_all() {
    TBE_TYPED_BIND_CLEAR(TYPED_BENCH_ORDER, &g_typed_bench_order);
    data_bind_free(g_typed_bench_codec);
    g_typed_bench_codec = NULL;
  }

  bench("typed JSON conversion") {
    benchmark_bytes("typed struct from JSON", TBE_TYPED_BENCH_SAMPLES,
                    sizeof(TYPED_BENCH_JSON) - 1u) {
      DataBindStatus status =
          TBE_TYPED_BIND_PARSE(g_typed_bench_codec, TYPED_BENCH_ORDER, "json",
                               TYPED_BENCH_JSON, sizeof(TYPED_BENCH_JSON) - 1u, 0u,
                               &g_typed_bench_order, &g_typed_bench_error);
      if (status == DATA_BIND_OK)
        g_typed_bench_sink += g_typed_bench_order.id;
      else
        ++g_typed_bench_failures;
    }

    benchmark_bytes("typed struct to JSON", TBE_TYPED_BENCH_SAMPLES,
                    sizeof(TYPED_BENCH_OUTPUT) - 1u) {
      char *output = NULL;
      size_t output_len = 0;
      DataBindStatus status = TBE_TYPED_BIND_SERIALIZE(
          g_typed_bench_codec, TYPED_BENCH_ORDER, &g_typed_bench_order, "json", &output,
          &output_len, &g_typed_bench_error);
      if (status == DATA_BIND_OK) {
        g_typed_bench_sink += output_len;
        tbe_typed_serialized_free(output);
      } else {
        ++g_typed_bench_failures;
      }
    }

    check_size_eq(g_typed_bench_failures, 0u);
    check_size_gt(g_typed_bench_sink, 0u);
  }
}
