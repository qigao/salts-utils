#include <tbe_cbind/tbe_cbind.h>

#include "cbind_sidecar.h"
#include "tinytest.h"
#include "turbo_cmeta_data.h"
#include "turbo_str.h"

#include <stddef.h>
#include <stdint.h>

enum {
  TBE_CBIND_CREATE_SAMPLES = 100,
  TBE_CBIND_DECODE_SAMPLES = 10000,
  TBE_CBIND_MAX_DEPTH = 2,
  TBE_CBIND_MAX_BUFFER_BYTES = 16
};

static const char TBE_CBIND_BENCH_SCHEMA[] =
    "composite CBindHeader { int32 sequence; } "
    "message CBindEnvelope { CBindHeader header; "
    "[name(eventId), c(event_id)] int32 id; string note; double score; }";

static const cmeta_data_buffer_shape tbe_cbind_bench_string_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc tbe_cbind_bench_string_data = {
    sizeof(cmeta_data_desc),       CMETA_DATA_DESC_ABI_VERSION, "benchmark.tbe-cbind.string",
    "benchmark owning string",     CMETA_DATA_STRING,           &turbo_tstr_cmeta_type,
    &tbe_cbind_bench_string_shape, &turbo_tstr_cmeta_buffer_ops};

static const cmeta_type_identity tbe_cbind_bench_header_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.tbe-cbind.header");
static const cmeta_type_desc tbe_cbind_bench_header_type = {"CBindHeader_t",
                                                            sizeof(CBindHeader_t),
                                                            _Alignof(CBindHeader_t),
                                                            CMETA_T_OBJECT,
                                                            NULL,
                                                            NULL,
                                                            &tbe_cbind_bench_header_identity};
static const cmeta_field_desc tbe_cbind_bench_header_layout_fields[] = {
    {"sequence", "int", offsetof(CBindHeader_t, sequence), sizeof(int), _Alignof(int),
     &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_bench_header_layout = {
    "CBindHeader_t", sizeof(CBindHeader_t), _Alignof(CBindHeader_t),
    tbe_cbind_bench_header_layout_fields, 1u};
static const cmeta_data_field_desc tbe_cbind_bench_header_data_fields[] = {
    {"benchmark.tbe-cbind.header.sequence", "sequence", offsetof(CBindHeader_t, sequence),
     &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_bench_header_shape = {
    &tbe_cbind_bench_header_layout, tbe_cbind_bench_header_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_bench_header_data = {sizeof(cmeta_data_desc),
                                                            CMETA_DATA_DESC_ABI_VERSION,
                                                            "benchmark.tbe-cbind.header.data",
                                                            "CBindHeader_t native storage",
                                                            CMETA_DATA_STRUCT,
                                                            &tbe_cbind_bench_header_type,
                                                            &tbe_cbind_bench_header_shape,
                                                            NULL};

static const cmeta_type_identity tbe_cbind_bench_envelope_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.tbe-cbind.envelope");
static const cmeta_type_desc tbe_cbind_bench_envelope_type = {"CBindEnvelope_t",
                                                              sizeof(CBindEnvelope_t),
                                                              _Alignof(CBindEnvelope_t),
                                                              CMETA_T_OBJECT,
                                                              NULL,
                                                              NULL,
                                                              &tbe_cbind_bench_envelope_identity};
static const cmeta_field_desc tbe_cbind_bench_envelope_layout_fields[] = {
    {"header", "CBindHeader_t", offsetof(CBindEnvelope_t, header), sizeof(CBindHeader_t),
     _Alignof(CBindHeader_t), &tbe_cbind_bench_header_type, NULL},
    {"event_id", "int", offsetof(CBindEnvelope_t, event_id), sizeof(int), _Alignof(int),
     &cmeta_type_int, NULL},
    {"note", "tstr", offsetof(CBindEnvelope_t, note), sizeof(tstr), _Alignof(tstr),
     &turbo_tstr_cmeta_type, NULL},
    {"score", "double", offsetof(CBindEnvelope_t, score), sizeof(double), _Alignof(double),
     &cmeta_type_double, NULL}};
static const cmeta_struct_desc tbe_cbind_bench_envelope_layout = {
    "CBindEnvelope_t", sizeof(CBindEnvelope_t), _Alignof(CBindEnvelope_t),
    tbe_cbind_bench_envelope_layout_fields, 4u};
static const cmeta_data_field_desc tbe_cbind_bench_envelope_data_fields[] = {
    {"benchmark.tbe-cbind.envelope.header", "header", offsetof(CBindEnvelope_t, header),
     &tbe_cbind_bench_header_data},
    {"benchmark.tbe-cbind.envelope.event-id", "event_id", offsetof(CBindEnvelope_t, event_id),
     &cmeta_data_int},
    {"benchmark.tbe-cbind.envelope.note", "note", offsetof(CBindEnvelope_t, note),
     &tbe_cbind_bench_string_data},
    {"benchmark.tbe-cbind.envelope.score", "score", offsetof(CBindEnvelope_t, score),
     &cmeta_data_double}};
static const cmeta_data_struct_shape tbe_cbind_bench_envelope_shape = {
    &tbe_cbind_bench_envelope_layout, tbe_cbind_bench_envelope_data_fields, 4u};
static const cmeta_data_desc tbe_cbind_bench_envelope_data = {sizeof(cmeta_data_desc),
                                                              CMETA_DATA_DESC_ABI_VERSION,
                                                              "benchmark.tbe-cbind.envelope.data",
                                                              "CBindEnvelope_t native storage",
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
#define TBE_CBIND_BENCH_STRING(text_)                                                              \
  {                                                                                                \
    .kind = CSERDE_STRING, .value.slice = {                                                        \
      (const unsigned char *)(text_),                                                              \
      sizeof(text_) - 1u,                                                                          \
      CSERDE_VIEW_STABLE                                                                           \
    }                                                                                              \
  }

static const cserde_token tbe_cbind_bench_tokens[] = {
    TBE_CBIND_BENCH_MAP_BEGIN,       TBE_CBIND_BENCH_KEY("header"), TBE_CBIND_BENCH_MAP_BEGIN,
    TBE_CBIND_BENCH_KEY("sequence"), TBE_CBIND_BENCH_SINT(-17),     TBE_CBIND_BENCH_MAP_END,
    TBE_CBIND_BENCH_KEY("eventId"),  TBE_CBIND_BENCH_SINT(42),      TBE_CBIND_BENCH_KEY("note"),
    TBE_CBIND_BENCH_STRING("owned"), TBE_CBIND_BENCH_KEY("score"),  TBE_CBIND_BENCH_FLOAT(3.5),
    TBE_CBIND_BENCH_MAP_END};

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

static tbe_cbind_plan *g_tbe_cbind_decode_plan;
static tbe_cbind_plan *g_tbe_cbind_creation_plans[TBE_CBIND_CREATE_SAMPLES];
static tbe_cbind_plan_error g_tbe_cbind_creation_errors[TBE_CBIND_CREATE_SAMPLES];
static CBindEnvelope_t g_tbe_cbind_direct_outputs[TBE_CBIND_DECODE_SAMPLES];
static CBindEnvelope_t g_tbe_cbind_runtime_outputs[TBE_CBIND_DECODE_SAMPLES];
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

static tbe_cbind_plan *tbe_cbind_bench_create_plan(void) {
  tbe_cbind_plan_error error;
  tbe_cbind_plan *plan = NULL;
  tbe_cbind_plan_error_init(&error);
  if (tbe_cbind_plan_create_from_text(TBE_CBIND_BENCH_SCHEMA, sizeof(TBE_CBIND_BENCH_SCHEMA) - 1u,
                                      "CBindEnvelope", sizeof("CBindEnvelope") - 1u,
                                      &tbe_cbind_bench_envelope_data, &g_tbe_cbind_options, &plan,
                                      &error) != TBE_CBIND_OK) {
    ++g_tbe_cbind_failures;
  }
  return plan;
}

spec("TbeCBind performance baselines") {
  before_all() {
    size_t index;
    tbe_cbind_plan_options_init(&g_tbe_cbind_options);
    g_tbe_cbind_decode_plan = tbe_cbind_bench_create_plan();
    check_not_null(g_tbe_cbind_decode_plan);
    check_true(cmeta_data_desc_valid(CBindEnvelope_cbind_data()));

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
      CBindEnvelope_init(&g_tbe_cbind_direct_outputs[index]);
      CBindEnvelope_init(&g_tbe_cbind_runtime_outputs[index]);
      g_tbe_cbind_direct_errors[index] = (cbind_error)CBIND_ERROR_INIT;
      g_tbe_cbind_runtime_errors[index] = (cbind_error)CBIND_ERROR_INIT;
    }
  }

  after_all() {
    size_t index;
    for (index = 0u; index < TBE_CBIND_CREATE_SAMPLES; ++index) {
      tbe_cbind_plan_destroy(g_tbe_cbind_creation_plans[index]);
      g_tbe_cbind_creation_plans[index] = NULL;
    }
    for (index = 0u; index < TBE_CBIND_DECODE_SAMPLES; ++index) {
      CBindEnvelope_clear(&g_tbe_cbind_runtime_outputs[index]);
      CBindEnvelope_clear(&g_tbe_cbind_direct_outputs[index]);
    }
    tbe_cbind_plan_destroy(g_tbe_cbind_decode_plan);
    g_tbe_cbind_decode_plan = NULL;
  }

  bench("runtime schema plan creation") {
    size_t creation_index = 0u;
    benchmark_batch("runtime TBE schema plan creation", TBE_CBIND_CREATE_SAMPLES) {
      tbe_cbind_plan *plan = NULL;
      tbe_cbind_status status = tbe_cbind_plan_create_from_text(
          TBE_CBIND_BENCH_SCHEMA, sizeof(TBE_CBIND_BENCH_SCHEMA) - 1u, "CBindEnvelope",
          sizeof("CBindEnvelope") - 1u, &tbe_cbind_bench_envelope_data, &g_tbe_cbind_options, &plan,
          &g_tbe_cbind_creation_errors[creation_index]);
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

    benchmark_batch("generated sidecar direct CBind decode", TBE_CBIND_DECODE_SAMPLES) {
      CBindEnvelope_t *out = &g_tbe_cbind_direct_outputs[direct_index];
      cbind_status status =
          CBindEnvelope_from_cserde(&g_tbe_cbind_direct_context, &g_tbe_cbind_direct_reader, out,
                                    &g_tbe_cbind_direct_errors[direct_index]);
      if (status == CBIND_OK) {
        g_tbe_cbind_sink += (size_t)(out->event_id + out->header.sequence);
      } else {
        ++g_tbe_cbind_failures;
      }
      ++direct_index;
    }

    benchmark_batch("runtime TbeCBind plan decode", TBE_CBIND_DECODE_SAMPLES) {
      CBindEnvelope_t *out = &g_tbe_cbind_runtime_outputs[runtime_index];
      cbind_status status = tbe_cbind_plan_decode(
          g_tbe_cbind_decode_plan, &g_tbe_cbind_runtime_context, &g_tbe_cbind_runtime_reader, out,
          &g_tbe_cbind_runtime_errors[runtime_index]);
      if (status == CBIND_OK) {
        g_tbe_cbind_sink += (size_t)(out->event_id + out->header.sequence);
      } else {
        ++g_tbe_cbind_failures;
      }
      ++runtime_index;
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

#undef TBE_CBIND_BENCH_STRING
#undef TBE_CBIND_BENCH_FLOAT
#undef TBE_CBIND_BENCH_SINT
#undef TBE_CBIND_BENCH_KEY
#undef TBE_CBIND_BENCH_MAP_END
#undef TBE_CBIND_BENCH_MAP_BEGIN
