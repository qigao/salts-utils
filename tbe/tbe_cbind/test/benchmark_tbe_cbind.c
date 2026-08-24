#include <tbe_cbind/tbe_cbind.h>

#include "tbe_cbind_benchmark.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TBE_CBIND_BENCHMARK_SCHEMA_PATH
  #error "TBE_CBIND_BENCHMARK_SCHEMA_PATH must identify the generator input"
#endif

enum {
  TBE_CBIND_CREATE_SAMPLES = 100,
  TBE_CBIND_DECODE_SAMPLES = 10000,
  TBE_CBIND_WARMUP_SAMPLES = 256,
  TBE_CBIND_MAX_DEPTH = 2,
  TBE_CBIND_MAX_BUFFER_BYTES = 1
};

static const cmeta_type_identity tbe_cbind_bench_header_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.tbe-cbind.header");
static const cmeta_type_desc tbe_cbind_bench_header_type = {"TbeCBindBenchHeader_t",
                                                            sizeof(TbeCBindBenchHeader_t),
                                                            _Alignof(TbeCBindBenchHeader_t),
                                                            CMETA_T_OBJECT,
                                                            NULL,
                                                            NULL,
                                                            &tbe_cbind_bench_header_identity};
static const cmeta_field_desc tbe_cbind_bench_header_layout_fields[] = {
    {"sequence", "int", offsetof(TbeCBindBenchHeader_t, sequence), sizeof(int32_t),
     _Alignof(int32_t), &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_bench_header_layout = {
    "TbeCBindBenchHeader_t", sizeof(TbeCBindBenchHeader_t), _Alignof(TbeCBindBenchHeader_t),
    tbe_cbind_bench_header_layout_fields, 1u};
static const cmeta_data_field_desc tbe_cbind_bench_header_data_fields[] = {
    {"benchmark.tbe-cbind.header.sequence", "sequence", offsetof(TbeCBindBenchHeader_t, sequence),
     &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_bench_header_shape = {
    &tbe_cbind_bench_header_layout, tbe_cbind_bench_header_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_bench_header_data = {sizeof(cmeta_data_desc),
                                                            CMETA_DATA_DESC_ABI_VERSION,
                                                            "benchmark.tbe-cbind.header.data",
                                                            "TbeCBindBenchHeader_t native storage",
                                                            CMETA_DATA_STRUCT,
                                                            &tbe_cbind_bench_header_type,
                                                            &tbe_cbind_bench_header_shape,
                                                            NULL};

static const cmeta_type_identity tbe_cbind_bench_envelope_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.tbe-cbind.envelope");
static const cmeta_type_desc tbe_cbind_bench_envelope_type = {"TbeCBindBenchEnvelope_t",
                                                              sizeof(TbeCBindBenchEnvelope_t),
                                                              _Alignof(TbeCBindBenchEnvelope_t),
                                                              CMETA_T_OBJECT,
                                                              NULL,
                                                              NULL,
                                                              &tbe_cbind_bench_envelope_identity};
static const cmeta_field_desc tbe_cbind_bench_envelope_layout_fields[] = {
    {"header", "TbeCBindBenchHeader_t", offsetof(TbeCBindBenchEnvelope_t, header),
     sizeof(TbeCBindBenchHeader_t), _Alignof(TbeCBindBenchHeader_t), &tbe_cbind_bench_header_type,
     NULL},
    {"event_id", "int", offsetof(TbeCBindBenchEnvelope_t, event_id), sizeof(int32_t),
     _Alignof(int32_t), &cmeta_type_int, NULL},
    {"score", "double", offsetof(TbeCBindBenchEnvelope_t, score), sizeof(double), _Alignof(double),
     &cmeta_type_double, NULL}};
static const cmeta_struct_desc tbe_cbind_bench_envelope_layout = {
    "TbeCBindBenchEnvelope_t", sizeof(TbeCBindBenchEnvelope_t), _Alignof(TbeCBindBenchEnvelope_t),
    tbe_cbind_bench_envelope_layout_fields, 3u};
static const cmeta_data_field_desc tbe_cbind_bench_envelope_data_fields[] = {
    {"benchmark.tbe-cbind.envelope.header", "header", offsetof(TbeCBindBenchEnvelope_t, header),
     &tbe_cbind_bench_header_data},
    {"benchmark.tbe-cbind.envelope.event-id", "event_id",
     offsetof(TbeCBindBenchEnvelope_t, event_id), &cmeta_data_int},
    {"benchmark.tbe-cbind.envelope.score", "score", offsetof(TbeCBindBenchEnvelope_t, score),
     &cmeta_data_double}};
static const cmeta_data_struct_shape tbe_cbind_bench_envelope_shape = {
    &tbe_cbind_bench_envelope_layout, tbe_cbind_bench_envelope_data_fields, 3u};
static const cmeta_data_desc tbe_cbind_bench_envelope_data = {
    sizeof(cmeta_data_desc),
    CMETA_DATA_DESC_ABI_VERSION,
    "benchmark.tbe-cbind.envelope.data",
    "TbeCBindBenchEnvelope_t native storage",
    CMETA_DATA_STRUCT,
    &tbe_cbind_bench_envelope_type,
    &tbe_cbind_bench_envelope_shape,
    NULL};

#define TBE_CBIND_BENCH_MAP_BEGIN {.kind = CSERDE_MAP_BEGIN}
#define TBE_CBIND_BENCH_MAP_END {.kind = CSERDE_MAP_END}
#define TBE_CBIND_BENCH_KEY(text_)                                                                 \
  {                                                                                                \
    .kind = CSERDE_STRING, .value.slice = {                                                        \
      (const unsigned char *)(text_),                                                              \
      sizeof(text_) - 1u,                                                                          \
      CSERDE_VIEW_STABLE                                                                           \
    }                                                                                              \
  }
#define TBE_CBIND_BENCH_SINT(value_) {.kind = CSERDE_SINT, .value.sint = (value_)}
#define TBE_CBIND_BENCH_FLOAT(value_) {.kind = CSERDE_FLOAT, .value.floating = (value_)}

static const cserde_token tbe_cbind_bench_tokens[] = {
    TBE_CBIND_BENCH_MAP_BEGIN,       TBE_CBIND_BENCH_KEY("header"), TBE_CBIND_BENCH_MAP_BEGIN,
    TBE_CBIND_BENCH_KEY("sequence"), TBE_CBIND_BENCH_SINT(-17),     TBE_CBIND_BENCH_MAP_END,
    TBE_CBIND_BENCH_KEY("eventId"),  TBE_CBIND_BENCH_SINT(42),      TBE_CBIND_BENCH_KEY("score"),
    TBE_CBIND_BENCH_FLOAT(3.5),      TBE_CBIND_BENCH_MAP_END};

typedef struct tbe_cbind_bench_reader_state {
  size_t position;
} tbe_cbind_bench_reader_state;

static cserde_status tbe_cbind_bench_reader_next(void *opaque, cserde_token *out) {
  tbe_cbind_bench_reader_state *state = (tbe_cbind_bench_reader_state *)opaque;
  if (state == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  *out = tbe_cbind_bench_tokens[state->position % (sizeof(tbe_cbind_bench_tokens) /
                                                   sizeof(tbe_cbind_bench_tokens[0]))];
  ++state->position;
  return CSERDE_OK;
}

static const cserde_reader_ops tbe_cbind_bench_reader_ops = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, tbe_cbind_bench_reader_next};

static char *g_tbe_cbind_schema_text;
static size_t g_tbe_cbind_schema_size;
static tbe_cbind_plan *g_tbe_cbind_decode_plan;
static tbe_cbind_plan_error g_tbe_cbind_decode_error;
static tbe_cbind_plan *g_tbe_cbind_creation_plans[TBE_CBIND_CREATE_SAMPLES];
static tbe_cbind_plan_error g_tbe_cbind_creation_errors[TBE_CBIND_CREATE_SAMPLES];
static TbeCBindBenchEnvelope_t g_tbe_cbind_direct_outputs[TBE_CBIND_DECODE_SAMPLES];
static TbeCBindBenchEnvelope_t g_tbe_cbind_runtime_outputs[TBE_CBIND_DECODE_SAMPLES];
static cbind_error g_tbe_cbind_direct_errors[TBE_CBIND_DECODE_SAMPLES];
static cbind_error g_tbe_cbind_runtime_errors[TBE_CBIND_DECODE_SAMPLES];
static tbe_cbind_bench_reader_state g_tbe_cbind_direct_reader_state;
static tbe_cbind_bench_reader_state g_tbe_cbind_runtime_reader_state;
static cserde_reader g_tbe_cbind_direct_reader;
static cserde_reader g_tbe_cbind_runtime_reader;
static unsigned char g_tbe_cbind_direct_scratch[2];
static unsigned char g_tbe_cbind_runtime_scratch[2];
static cbind_context g_tbe_cbind_direct_context;
static cbind_context g_tbe_cbind_runtime_context;
static tbe_cbind_plan_options g_tbe_cbind_options;
static volatile size_t g_tbe_cbind_sink;
static size_t g_tbe_cbind_failures;

static int tbe_cbind_bench_output_matches(const TbeCBindBenchEnvelope_t *out) {
  return out->header.sequence == -17 && out->event_id == 42 && out->score == 3.5;
}

static int tbe_cbind_bench_output_is_zero(const TbeCBindBenchEnvelope_t *out) {
  return out->header.sequence == 0 && out->event_id == 0 && out->score == 0.0;
}

static void tbe_cbind_bench_reset_path(tbe_cbind_bench_reader_state *state, unsigned char *scratch,
                                       size_t scratch_size) {
  state->position = 0u;
  memset(scratch, 0, scratch_size);
}

static size_t tbe_cbind_bench_warm_direct(void) {
  size_t failures = 0u;
  size_t index;
  for (index = 0u; index < TBE_CBIND_WARMUP_SAMPLES; ++index) {
    TbeCBindBenchEnvelope_t out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    if (TbeCBindBenchEnvelope_from_cserde(&g_tbe_cbind_direct_context, &g_tbe_cbind_direct_reader,
                                          &out, &error) != CBIND_OK ||
        !tbe_cbind_bench_output_matches(&out))
      ++failures;
    g_tbe_cbind_sink += (size_t)(out.event_id + out.header.sequence + (int)out.score);
    out = (TbeCBindBenchEnvelope_t){0};
    if (!tbe_cbind_bench_output_is_zero(&out)) ++failures;
  }
  tbe_cbind_bench_reset_path(&g_tbe_cbind_direct_reader_state, g_tbe_cbind_direct_scratch,
                             sizeof(g_tbe_cbind_direct_scratch));
  return failures;
}

static size_t tbe_cbind_bench_warm_runtime(void) {
  size_t failures = 0u;
  size_t index;
  for (index = 0u; index < TBE_CBIND_WARMUP_SAMPLES; ++index) {
    TbeCBindBenchEnvelope_t out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    if (tbe_cbind_plan_decode(g_tbe_cbind_decode_plan, &g_tbe_cbind_runtime_context,
                              &g_tbe_cbind_runtime_reader, &out, &error) != CBIND_OK ||
        !tbe_cbind_bench_output_matches(&out))
      ++failures;
    g_tbe_cbind_sink += (size_t)(out.event_id + out.header.sequence + (int)out.score);
    out = (TbeCBindBenchEnvelope_t){0};
    if (!tbe_cbind_bench_output_is_zero(&out)) ++failures;
  }
  tbe_cbind_bench_reset_path(&g_tbe_cbind_runtime_reader_state, g_tbe_cbind_runtime_scratch,
                             sizeof(g_tbe_cbind_runtime_scratch));
  return failures;
}

static tbe_cbind_plan *tbe_cbind_bench_create_plan(void) {
  tbe_cbind_plan *plan = NULL;
  tbe_cbind_plan_error_init(&g_tbe_cbind_decode_error);
  if (tbe_cbind_plan_create_from_text(g_tbe_cbind_schema_text, g_tbe_cbind_schema_size,
                                      "TbeCBindBenchEnvelope", sizeof("TbeCBindBenchEnvelope") - 1u,
                                      &tbe_cbind_bench_envelope_data, &g_tbe_cbind_options, &plan,
                                      &g_tbe_cbind_decode_error) != TBE_CBIND_OK) {
    fprintf(stderr, "benchmark plan error: status=%d phase=%d path=%s message=%s\n",
            (int)g_tbe_cbind_decode_error.status, (int)g_tbe_cbind_decode_error.phase,
            g_tbe_cbind_decode_error.path, g_tbe_cbind_decode_error.message);
    ++g_tbe_cbind_failures;
  }
  return plan;
}

spec("TbeCBind performance baselines") {
  before_all() {
    size_t index;
    g_tbe_cbind_schema_text =
        tt_read_file(TBE_CBIND_BENCHMARK_SCHEMA_PATH, &g_tbe_cbind_schema_size);
    check_not_null(g_tbe_cbind_schema_text);
    tbe_cbind_plan_options_init(&g_tbe_cbind_options);
    g_tbe_cbind_decode_plan = tbe_cbind_bench_create_plan();
    check_not_null(g_tbe_cbind_decode_plan);
    check_true(cmeta_data_desc_valid(TbeCBindBenchEnvelope_cbind_data()));

    g_tbe_cbind_direct_context = (cbind_context)CBIND_CONTEXT_WITH_BUFFERS_INIT(
        g_tbe_cbind_direct_scratch, sizeof(g_tbe_cbind_direct_scratch), TBE_CBIND_MAX_DEPTH, 0u,
        TBE_CBIND_MAX_BUFFER_BYTES);
    g_tbe_cbind_runtime_context = (cbind_context)CBIND_CONTEXT_WITH_BUFFERS_INIT(
        g_tbe_cbind_runtime_scratch, sizeof(g_tbe_cbind_runtime_scratch), TBE_CBIND_MAX_DEPTH, 0u,
        TBE_CBIND_MAX_BUFFER_BYTES);
    check_equal(cserde_reader_init(&g_tbe_cbind_direct_reader, &tbe_cbind_bench_reader_ops,
                                   &g_tbe_cbind_direct_reader_state),
                CSERDE_OK);
    check_equal(cserde_reader_init(&g_tbe_cbind_runtime_reader, &tbe_cbind_bench_reader_ops,
                                   &g_tbe_cbind_runtime_reader_state),
                CSERDE_OK);
    for (index = 0u; index < TBE_CBIND_CREATE_SAMPLES; ++index)
      tbe_cbind_plan_error_init(&g_tbe_cbind_creation_errors[index]);
    for (index = 0u; index < TBE_CBIND_DECODE_SAMPLES; ++index) {
      g_tbe_cbind_direct_errors[index] = (cbind_error)CBIND_ERROR_INIT;
      g_tbe_cbind_runtime_errors[index] = (cbind_error)CBIND_ERROR_INIT;
      check_true(tbe_cbind_bench_output_is_zero(&g_tbe_cbind_direct_outputs[index]));
      check_true(tbe_cbind_bench_output_is_zero(&g_tbe_cbind_runtime_outputs[index]));
    }
  }

  after_all() {
    size_t index;
    for (index = 0u; index < TBE_CBIND_CREATE_SAMPLES; ++index) {
      tbe_cbind_plan_destroy(g_tbe_cbind_creation_plans[index]);
      g_tbe_cbind_creation_plans[index] = NULL;
    }
    tbe_cbind_plan_destroy(g_tbe_cbind_decode_plan);
    g_tbe_cbind_decode_plan = NULL;
    free(g_tbe_cbind_schema_text);
    g_tbe_cbind_schema_text = NULL;
    g_tbe_cbind_schema_size = 0u;
  }

  bench("runtime schema plan creation") {
    size_t creation_index = 0u;
    benchmark_batch("runtime TBE schema plan creation", TBE_CBIND_CREATE_SAMPLES) {
      tbe_cbind_plan *plan = NULL;
      tbe_cbind_status status = tbe_cbind_plan_create_from_text(
          g_tbe_cbind_schema_text, g_tbe_cbind_schema_size, "TbeCBindBenchEnvelope",
          sizeof("TbeCBindBenchEnvelope") - 1u, &tbe_cbind_bench_envelope_data,
          &g_tbe_cbind_options, &plan, &g_tbe_cbind_creation_errors[creation_index]);
      if (status == TBE_CBIND_OK && plan != NULL) {
        g_tbe_cbind_creation_plans[creation_index] = plan;
        g_tbe_cbind_sink += tbe_cbind_plan_shape(plan) != NULL;
      } else {
        ++g_tbe_cbind_failures;
      }
      ++creation_index;
    }
    check_equal(creation_index, (size_t)TBE_CBIND_CREATE_SAMPLES);
    check_equal(g_tbe_cbind_failures, (size_t)0u);
  }

  bench("repeated decode") {
    size_t direct_index = 0u;
    size_t runtime_index = 0u;
    size_t index;

    check_equal(tbe_cbind_bench_warm_direct(), (size_t)0u);
    benchmark_batch("generated sidecar direct CBind decode", TBE_CBIND_DECODE_SAMPLES) {
      TbeCBindBenchEnvelope_t *out = &g_tbe_cbind_direct_outputs[direct_index];
      cbind_status status =
          TbeCBindBenchEnvelope_from_cserde(&g_tbe_cbind_direct_context, &g_tbe_cbind_direct_reader,
                                            out, &g_tbe_cbind_direct_errors[direct_index]);
      if (status == CBIND_OK) {
        g_tbe_cbind_sink += (size_t)(out->event_id + out->header.sequence + (int)out->score);
      } else {
        ++g_tbe_cbind_failures;
      }
      ++direct_index;
    }
    for (index = 0u; index < TBE_CBIND_DECODE_SAMPLES; ++index) {
      check_true(tbe_cbind_bench_output_matches(&g_tbe_cbind_direct_outputs[index]));
      g_tbe_cbind_direct_outputs[index] = (TbeCBindBenchEnvelope_t){0};
      check_true(tbe_cbind_bench_output_is_zero(&g_tbe_cbind_direct_outputs[index]));
    }

    check_equal(tbe_cbind_bench_warm_runtime(), (size_t)0u);
    benchmark_batch("runtime TbeCBind plan decode", TBE_CBIND_DECODE_SAMPLES) {
      TbeCBindBenchEnvelope_t *out = &g_tbe_cbind_runtime_outputs[runtime_index];
      cbind_status status = tbe_cbind_plan_decode(
          g_tbe_cbind_decode_plan, &g_tbe_cbind_runtime_context, &g_tbe_cbind_runtime_reader, out,
          &g_tbe_cbind_runtime_errors[runtime_index]);
      if (status == CBIND_OK) {
        g_tbe_cbind_sink += (size_t)(out->event_id + out->header.sequence + (int)out->score);
      } else {
        ++g_tbe_cbind_failures;
      }
      ++runtime_index;
    }
    for (index = 0u; index < TBE_CBIND_DECODE_SAMPLES; ++index) {
      check_true(tbe_cbind_bench_output_matches(&g_tbe_cbind_runtime_outputs[index]));
      g_tbe_cbind_runtime_outputs[index] = (TbeCBindBenchEnvelope_t){0};
      check_true(tbe_cbind_bench_output_is_zero(&g_tbe_cbind_runtime_outputs[index]));
    }

    check_equal(direct_index, (size_t)TBE_CBIND_DECODE_SAMPLES);
    check_equal(runtime_index, (size_t)TBE_CBIND_DECODE_SAMPLES);
    check_equal(g_tbe_cbind_direct_reader_state.position,
                (size_t)TBE_CBIND_DECODE_SAMPLES *
                    (sizeof(tbe_cbind_bench_tokens) / sizeof(tbe_cbind_bench_tokens[0])));
    check_equal(g_tbe_cbind_runtime_reader_state.position,
                g_tbe_cbind_direct_reader_state.position);
    check_equal(g_tbe_cbind_failures, (size_t)0u);
    check_greater(g_tbe_cbind_sink, (size_t)0u);
  }
}

#undef TBE_CBIND_BENCH_FLOAT
#undef TBE_CBIND_BENCH_SINT
#undef TBE_CBIND_BENCH_KEY
#undef TBE_CBIND_BENCH_MAP_END
#undef TBE_CBIND_BENCH_MAP_BEGIN
