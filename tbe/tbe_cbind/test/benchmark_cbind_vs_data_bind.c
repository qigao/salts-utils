#include <tbe_cbind/tbe_cbind.h>
#include <tbe_typed.h>
#include <turbo_parser_json.h>

#include "benchmark_tbe_cbind_fixture.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TBE_CBIND_BENCHMARK_SCHEMA_PATH
  #error "TBE_CBIND_BENCHMARK_SCHEMA_PATH must identify the shared schema input"
#endif

enum {
  CBIND_DATABIND_SETUP_SAMPLES = 100,
  CBIND_DATABIND_DECODE_SAMPLES = 10000,
  CBIND_DATABIND_WARMUP_SAMPLES = 256,
  CBIND_DATABIND_MAX_DEPTH = 2,
  CBIND_DATABIND_MAX_BUFFER_BYTES = 1
};

static const char cbind_databind_json[] =
    "{\"header\":{\"sequence\":-17},\"eventId\":42,\"score\":3.5}";

#define CBIND_DATABIND_WIRE_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, KIND, WIRE_OFFSET, WIRE_SIZE)       \
  TBE_TYPED_FIELD_EX(C_TYPE, MEMBER, SCHEMA_NAME, KIND, KIND, TBE_TYPED_BOOL, TBE_TYPED_BOOL, 0u,  \
                     0u, NULL, 0u, 0u, 0u, TBE_TYPED_BOOL, TBE_TYPED_BOOL, NULL, WIRE_OFFSET,      \
                     WIRE_SIZE, 0u, TBE_TYPED_FIELD_WIRE_OFFSET)

#define CBIND_DATABIND_WIRE_OBJECT_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, OBJECT_TYPE, WIRE_OFFSET,    \
                                         WIRE_SIZE)                                                \
  TBE_TYPED_FIELD_EX(C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_OBJECT, TBE_TYPED_OBJECT,              \
                     TBE_TYPED_BOOL, TBE_TYPED_BOOL, 0u, 0u, OBJECT_TYPE, 0u, 0u, 0u,              \
                     TBE_TYPED_BOOL, TBE_TYPED_BOOL, NULL, WIRE_OFFSET, WIRE_SIZE, 0u,             \
                     TBE_TYPED_FIELD_WIRE_OFFSET)

TBE_TYPED_DEFINE_STRUCT_EX(CBIND_DATABIND_HEADER, TbeCBindBenchHeader_t, "TbeCBindBenchHeader", 4u,
                           0u, 0u, 0,
                           CBIND_DATABIND_WIRE_FIELD(TbeCBindBenchHeader_t, sequence, "sequence",
                                                     TBE_TYPED_I32, 0u, 4u));

TBE_TYPED_DEFINE_STRUCT_EX(
    CBIND_DATABIND_ENVELOPE, TbeCBindBenchEnvelope_t, "TbeCBindBenchEnvelope", 16u, 0u, 0u, 0,
    CBIND_DATABIND_WIRE_OBJECT_FIELD(TbeCBindBenchEnvelope_t, header, "header",
                                     &CBIND_DATABIND_HEADER, 0u, 4u),
    CBIND_DATABIND_WIRE_FIELD(TbeCBindBenchEnvelope_t, event_id, "id", TBE_TYPED_I32, 4u, 4u),
    CBIND_DATABIND_WIRE_FIELD(TbeCBindBenchEnvelope_t, score, "score", TBE_TYPED_F64, 8u, 8u));

static char *g_cbind_databind_schema_text;
static size_t g_cbind_databind_schema_size;
static tbe_cbind_plan_options g_cbind_databind_plan_options;
static tbe_cbind_plan *g_cbind_databind_decode_plan;
static DataBind *g_cbind_databind_decode_codec;
static tbe_cbind_plan *g_cbind_databind_setup_plans[CBIND_DATABIND_SETUP_SAMPLES];
static DataBind *g_cbind_databind_setup_codecs[CBIND_DATABIND_SETUP_SAMPLES];
static tbe_cbind_plan_error g_cbind_databind_plan_errors[CBIND_DATABIND_SETUP_SAMPLES];
static DataBindError g_cbind_databind_codec_errors[CBIND_DATABIND_SETUP_SAMPLES];
static TbeCBindBenchEnvelope_t g_cbind_outputs[CBIND_DATABIND_DECODE_SAMPLES];
static TbeCBindBenchEnvelope_t g_databind_outputs[CBIND_DATABIND_DECODE_SAMPLES];
static cbind_error g_cbind_errors[CBIND_DATABIND_DECODE_SAMPLES];
static DataBindError g_databind_errors[CBIND_DATABIND_DECODE_SAMPLES];
static unsigned char g_cbind_databind_scratch[2];
static cbind_context g_cbind_databind_context;
static volatile size_t g_cbind_databind_sink;

static int cbind_databind_output_matches(const TbeCBindBenchEnvelope_t *out) {
  return out != NULL && out->header.sequence == -17 && out->event_id == 42 && out->score == 3.5;
}

static int cbind_databind_output_is_zero(const TbeCBindBenchEnvelope_t *out) {
  return out != NULL && out->header.sequence == 0 && out->event_id == 0 && out->score == 0.0;
}

static tbe_cbind_status cbind_databind_create_plan(tbe_cbind_plan **out,
                                                   tbe_cbind_plan_error *error) {
  return tbe_cbind_plan_create_from_text(
      g_cbind_databind_schema_text, g_cbind_databind_schema_size, "TbeCBindBenchEnvelope",
      sizeof("TbeCBindBenchEnvelope") - 1u, &tbe_cbind_bench_envelope_data,
      &g_cbind_databind_plan_options, out, error);
}

static DataBindStatus cbind_databind_create_codec(DataBind **out, DataBindError *error) {
  DataBindStatus status = data_bind_create_from_text(g_cbind_databind_schema_text,
                                                     g_cbind_databind_schema_size, out, error);
  if (status == DATA_BIND_OK)
    status =
        tbe_typed_validate_schema(*out, "TbeCBindBenchEnvelope", &CBIND_DATABIND_ENVELOPE, error);
  if (status != DATA_BIND_OK) {
    data_bind_free(*out);
    *out = NULL;
  }
  return status;
}

static cbind_status cbind_databind_decode_cbind(TbeCBindBenchEnvelope_t *out, cbind_error *error) {
  turbo_json_doc_t *document = NULL;
  cserde_reader *reader = NULL;
  cbind_status status = CBIND_INVALID_ARGUMENT;
  if (turbo_parse_json((const uint8_t *)cbind_databind_json, sizeof(cbind_databind_json) - 1u,
                       &document) != 0)
    return CBIND_SOURCE_ERROR;
  reader = turbo_json_cserde_reader_create(document, CBIND_DATABIND_MAX_DEPTH);
  if (reader != NULL)
    status = tbe_cbind_plan_decode(g_cbind_databind_decode_plan, &g_cbind_databind_context, reader,
                                   out, error);
  turbo_json_cserde_reader_destroy(reader);
  turbo_free_json(&document);
  return status;
}

static DataBindStatus cbind_databind_decode_databind(TbeCBindBenchEnvelope_t *out,
                                                     DataBindError *error) {
  return TBE_TYPED_BIND_PARSE(g_cbind_databind_decode_codec, CBIND_DATABIND_ENVELOPE, "json",
                              cbind_databind_json, sizeof(cbind_databind_json) - 1u, 0u, out,
                              error);
}

static size_t cbind_databind_warm_cbind(void) {
  size_t failures = 0u;
  size_t index;
  for (index = 0u; index < CBIND_DATABIND_WARMUP_SAMPLES; ++index) {
    TbeCBindBenchEnvelope_t out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    if (cbind_databind_decode_cbind(&out, &error) != CBIND_OK ||
        !cbind_databind_output_matches(&out))
      ++failures;
    g_cbind_databind_sink += (size_t)(out.header.sequence + out.event_id + (int)out.score);
  }
  return failures;
}

static size_t cbind_databind_warm_databind(void) {
  size_t failures = 0u;
  size_t index;
  for (index = 0u; index < CBIND_DATABIND_WARMUP_SAMPLES; ++index) {
    TbeCBindBenchEnvelope_t out;
    DataBindError error = DATA_BIND_ERROR_INIT;
    if (TBE_TYPED_BIND_INIT(CBIND_DATABIND_ENVELOPE, &out, &error) != DATA_BIND_OK ||
        cbind_databind_decode_databind(&out, &error) != DATA_BIND_OK ||
        !cbind_databind_output_matches(&out))
      ++failures;
    g_cbind_databind_sink += (size_t)(out.header.sequence + out.event_id + (int)out.score);
    TBE_TYPED_BIND_CLEAR(CBIND_DATABIND_ENVELOPE, &out);
  }
  return failures;
}

spec("CBind versus DataBind benchmarks") {
  before_all() {
    size_t index;
    tbe_cbind_plan_error plan_error;
    DataBindError codec_error = DATA_BIND_ERROR_INIT;
    tbe_cbind_status plan_status;
    DataBindStatus codec_status;

    g_cbind_databind_schema_text =
        tt_read_file(TBE_CBIND_BENCHMARK_SCHEMA_PATH, &g_cbind_databind_schema_size);
    check_not_null(g_cbind_databind_schema_text);
    tbe_cbind_plan_options_init(&g_cbind_databind_plan_options);
    tbe_cbind_plan_error_init(&plan_error);
    plan_status = cbind_databind_create_plan(&g_cbind_databind_decode_plan, &plan_error);
    if (plan_status != TBE_CBIND_OK)
      fprintf(stderr, "CBind plan setup failed: status=%d phase=%d path=%s message=%s\n",
              (int)plan_status, (int)plan_error.phase, plan_error.path, plan_error.message);
    check_equal(plan_status, TBE_CBIND_OK);
    check_not_null(g_cbind_databind_decode_plan);
    codec_status = cbind_databind_create_codec(&g_cbind_databind_decode_codec, &codec_error);
    if (codec_status != DATA_BIND_OK)
      fprintf(stderr, "DataBind setup failed: status=%d path=%s message=%s\n", (int)codec_status,
              codec_error.path, codec_error.message);
    check_equal(codec_status, DATA_BIND_OK);
    check_not_null(g_cbind_databind_decode_codec);
    g_cbind_databind_context = (cbind_context)CBIND_CONTEXT_WITH_BUFFERS_INIT(
        g_cbind_databind_scratch, sizeof(g_cbind_databind_scratch), CBIND_DATABIND_MAX_DEPTH, 0u,
        CBIND_DATABIND_MAX_BUFFER_BYTES);

    for (index = 0u; index < CBIND_DATABIND_SETUP_SAMPLES; ++index) {
      tbe_cbind_plan_error_init(&g_cbind_databind_plan_errors[index]);
      g_cbind_databind_codec_errors[index] = (DataBindError)DATA_BIND_ERROR_INIT;
    }
    for (index = 0u; index < CBIND_DATABIND_DECODE_SAMPLES; ++index) {
      g_cbind_errors[index] = (cbind_error)CBIND_ERROR_INIT;
      g_databind_errors[index] = (DataBindError)DATA_BIND_ERROR_INIT;
      check_equal(TBE_TYPED_BIND_INIT(CBIND_DATABIND_ENVELOPE, &g_databind_outputs[index],
                                      &g_databind_errors[index]),
                  DATA_BIND_OK);
      check_true(cbind_databind_output_is_zero(&g_cbind_outputs[index]));
      check_true(cbind_databind_output_is_zero(&g_databind_outputs[index]));
    }
  }

  after_all() {
    size_t index;
    for (index = 0u; index < CBIND_DATABIND_SETUP_SAMPLES; ++index) {
      tbe_cbind_plan_destroy(g_cbind_databind_setup_plans[index]);
      data_bind_free(g_cbind_databind_setup_codecs[index]);
    }
    for (index = 0u; index < CBIND_DATABIND_DECODE_SAMPLES; ++index)
      TBE_TYPED_BIND_CLEAR(CBIND_DATABIND_ENVELOPE, &g_databind_outputs[index]);
    tbe_cbind_plan_destroy(g_cbind_databind_decode_plan);
    data_bind_free(g_cbind_databind_decode_codec);
    free(g_cbind_databind_schema_text);
    g_cbind_databind_decode_plan = NULL;
    g_cbind_databind_decode_codec = NULL;
    g_cbind_databind_schema_text = NULL;
    g_cbind_databind_schema_size = 0u;
  }

  it("decodes identical JSON into the same generated C struct") {
    TbeCBindBenchEnvelope_t cbind_out = {0};
    TbeCBindBenchEnvelope_t databind_out;
    cbind_error cbind_error_value = CBIND_ERROR_INIT;
    DataBindError databind_error = DATA_BIND_ERROR_INIT;

    check_equal(TBE_TYPED_BIND_INIT(CBIND_DATABIND_ENVELOPE, &databind_out, &databind_error),
                DATA_BIND_OK);
    check_equal(cbind_databind_decode_cbind(&cbind_out, &cbind_error_value), CBIND_OK);
    check_equal(cbind_databind_decode_databind(&databind_out, &databind_error), DATA_BIND_OK);
    check_true(cbind_databind_output_matches(&cbind_out));
    check_true(cbind_databind_output_matches(&databind_out));
    check_equal(cbind_out.header.sequence, databind_out.header.sequence);
    check_equal(cbind_out.event_id, databind_out.event_id);
    check_equal(cbind_out.score, databind_out.score);
    TBE_TYPED_BIND_CLEAR(CBIND_DATABIND_ENVELOPE, &databind_out);
  }

  bench("schema and native binding setup") {
    size_t cbind_index = 0u;
    size_t databind_index = 0u;
    size_t failures = 0u;

    benchmark_batch("CBind: TBE plan creation", CBIND_DATABIND_SETUP_SAMPLES) {
      if (cbind_databind_create_plan(&g_cbind_databind_setup_plans[cbind_index],
                                     &g_cbind_databind_plan_errors[cbind_index]) != TBE_CBIND_OK)
        ++failures;
      ++cbind_index;
    }
    benchmark_batch("DataBind: codec creation + typed schema validation",
                    CBIND_DATABIND_SETUP_SAMPLES) {
      if (cbind_databind_create_codec(&g_cbind_databind_setup_codecs[databind_index],
                                      &g_cbind_databind_codec_errors[databind_index]) !=
          DATA_BIND_OK)
        ++failures;
      ++databind_index;
    }

    check_equal(cbind_index, (size_t)CBIND_DATABIND_SETUP_SAMPLES);
    check_equal(databind_index, (size_t)CBIND_DATABIND_SETUP_SAMPLES);
    check_equal(failures, (size_t)0u);
  }

  bench("end-to-end JSON to generated C struct") {
    size_t cbind_index = 0u;
    size_t databind_index = 0u;
    size_t failures = 0u;
    size_t index;

    check_equal(cbind_databind_warm_cbind(), (size_t)0u);
    benchmark_bytes("CBind: JSON DOM + CSerde + plan decode", CBIND_DATABIND_DECODE_SAMPLES,
                    sizeof(cbind_databind_json) - 1u) {
      TbeCBindBenchEnvelope_t *out = &g_cbind_outputs[cbind_index];
      if (cbind_databind_decode_cbind(out, &g_cbind_errors[cbind_index]) == CBIND_OK)
        g_cbind_databind_sink += (size_t)(out->header.sequence + out->event_id + (int)out->score);
      else ++failures;
      ++cbind_index;
    }

    check_equal(cbind_databind_warm_databind(), (size_t)0u);
    benchmark_bytes("DataBind: JSON parse + typed struct materialization",
                    CBIND_DATABIND_DECODE_SAMPLES, sizeof(cbind_databind_json) - 1u) {
      TbeCBindBenchEnvelope_t *out = &g_databind_outputs[databind_index];
      if (cbind_databind_decode_databind(out, &g_databind_errors[databind_index]) == DATA_BIND_OK)
        g_cbind_databind_sink += (size_t)(out->header.sequence + out->event_id + (int)out->score);
      else ++failures;
      ++databind_index;
    }

    for (index = 0u; index < CBIND_DATABIND_DECODE_SAMPLES; ++index) {
      check_true(cbind_databind_output_matches(&g_cbind_outputs[index]));
      check_true(cbind_databind_output_matches(&g_databind_outputs[index]));
      g_cbind_outputs[index] = (TbeCBindBenchEnvelope_t){0};
      TBE_TYPED_BIND_CLEAR(CBIND_DATABIND_ENVELOPE, &g_databind_outputs[index]);
      check_true(cbind_databind_output_is_zero(&g_cbind_outputs[index]));
      check_true(cbind_databind_output_is_zero(&g_databind_outputs[index]));
    }

    check_equal(cbind_index, (size_t)CBIND_DATABIND_DECODE_SAMPLES);
    check_equal(databind_index, (size_t)CBIND_DATABIND_DECODE_SAMPLES);
    check_equal(failures, (size_t)0u);
    check_greater(g_cbind_databind_sink, (size_t)0u);
  }
}
