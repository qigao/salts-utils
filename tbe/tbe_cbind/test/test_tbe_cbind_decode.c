#include <tbe_cbind/tbe_cbind.h>

#include "tbe_cbind_test_fixtures.h"
#include "tinytest.h"
#include "turbo_thread.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define TOKEN_MAP_BEGIN { .kind = CSERDE_MAP_BEGIN }
#define TOKEN_MAP_END { .kind = CSERDE_MAP_END }
#define TOKEN_SINT(value_) { .kind = CSERDE_SINT, .value.sint = (value_) }
#define TOKEN_FLOAT(value_) { .kind = CSERDE_FLOAT, .value.floating = (value_) }
#define TOKEN_SLICE(kind_, text_, lifetime_)                              \
  { .kind = (kind_),                                                     \
    .value.slice = {(const unsigned char *)(text_), sizeof(text_) - 1u,  \
                    (lifetime_)} }
#define TOKEN_KEY(text_) TOKEN_SLICE(CSERDE_STRING, text_, CSERDE_VIEW_STABLE)

typedef struct token_reader_context {
  const cserde_token *tokens;
  size_t token_count;
  size_t index;
  size_t fail_at;
} token_reader_context;

static cserde_status token_reader_next(void *opaque, cserde_token *out) {
  token_reader_context *source = (token_reader_context *)opaque;
  if (source == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (source->index == source->fail_at) return CSERDE_SOURCE_ERROR;
  if (source->index == source->token_count) return CSERDE_DONE;
  *out = source->tokens[source->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops token_reader_ops = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
    token_reader_next};

static tbe_cbind_plan *make_plan(const char *schema, const char *type_name,
                                 const cmeta_data_desc *native_shape) {
  tbe_cbind_plan_options options;
  tbe_cbind_plan_error error;
  tbe_cbind_plan *plan = NULL;
  tbe_cbind_plan_options_init(&options);
  tbe_cbind_plan_error_init(&error);
  check_equal(tbe_cbind_plan_create_from_text(
                  schema, strlen(schema), type_name, strlen(type_name),
                  native_shape, &options, &plan, &error),
              TBE_CBIND_OK);
  check_not_null(plan);
  return plan;
}

static cbind_status decode_tokens(
    const tbe_cbind_plan *plan, const cserde_token *tokens, size_t token_count,
    size_t fail_at, void *out, size_t max_depth, size_t max_buffer_bytes,
    void *scratch, size_t scratch_size, cbind_error *error,
    size_t *consumed) {
  token_reader_context source = {tokens, token_count, 0u, fail_at};
  cserde_reader reader = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, scratch_size, max_depth, 0u, max_buffer_bytes);
  cbind_status status;
  check_equal(cserde_reader_init(&reader, &token_reader_ops, &source),
              CSERDE_OK);
  status = tbe_cbind_plan_decode(plan, &context, &reader, out, error);
  if (consumed != NULL) *consumed = source.index;
  return status;
}

typedef struct decode_worker {
  const tbe_cbind_plan *plan;
  int input;
  int output;
  cbind_status status;
} decode_worker;

static void decode_worker_run(void *opaque) {
  decode_worker *worker = (decode_worker *)opaque;
  cserde_token tokens[] = {
      TOKEN_MAP_BEGIN, TOKEN_KEY("external"), TOKEN_SINT(0), TOKEN_MAP_END};
  unsigned char scratch[1] = {0};
  cbind_error error = CBIND_ERROR_INIT;
  tbe_cbind_test_one out = {0};
  tokens[2].value.sint = worker->input;
  worker->status = decode_tokens(worker->plan, tokens,
                                 sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                                 &out, 1u, 0u, scratch, sizeof(scratch),
                                 &error, NULL);
  worker->output = out.value;
}

spec("TbeCBind transactional decode facade") {
  it("decodes renamed scalar storage through the plan overlay") {
    static const char schema[] =
        "message One { [name(external), c(value)] int32 internal; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("external"), TOKEN_SINT(7), TOKEN_MAP_END};
    unsigned char scratch[1] = {0};
    tbe_cbind_test_one out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan = make_plan(schema, "One", &tbe_cbind_test_one_data);

    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 1u, 0u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_OK);
    check_equal(out.value, 7);
    tbe_cbind_plan_destroy(plan);
  }

  it("decodes nested semantic names with independent native member names") {
    static const char schema[] =
        "composite Detail { [name(amount), c(quantity)] int32 quantity; } "
        "message Root { Detail detail; double score; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("detail"), TOKEN_MAP_BEGIN,
        TOKEN_KEY("amount"), TOKEN_SINT(11), TOKEN_MAP_END,
        TOKEN_KEY("score"), TOKEN_FLOAT(2.5), TOKEN_MAP_END};
    unsigned char scratch[2] = {0};
    tbe_cbind_test_nested out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan =
        make_plan(schema, "Root", &tbe_cbind_test_nested_data);

    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 2u, 0u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_OK);
    check_equal(out.detail.quantity, 11);
    check(out.score == 2.5);
    tbe_cbind_plan_destroy(plan);
  }

  it("copies owning strings and borrows only stable string views") {
    static const char schema[] =
        "message Text { string owned; string borrowed; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("owned"), TOKEN_SLICE(CSERDE_STRING, "copy", CSERDE_VIEW_TRANSIENT),
        TOKEN_KEY("borrowed"), TOKEN_SLICE(CSERDE_STRING, "view", CSERDE_VIEW_STABLE),
        TOKEN_MAP_END};
    unsigned char scratch[1] = {0};
    tbe_cbind_test_strings out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan =
        make_plan(schema, "Text", &tbe_cbind_test_strings_data);

    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 1u, 16u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_OK);
    check_not_null(out.owned);
    check_equal(tstr_len(out.owned), (size_t)4);
    check_equal(out.owned, "copy");
    check_true(out.borrowed.data == (const char *)tokens[4].value.slice.data);
    check_equal(out.borrowed.len, (size_t)4);
    check_equal(cmeta_data_buffer_restore_zero(
                    &tbe_cbind_test_owned_string_data, &out.owned),
                CMETA_OK);
    check_equal(cmeta_data_buffer_restore_zero(
                    &tbe_cbind_test_borrowed_string_data, &out.borrowed),
                CMETA_OK);
    tbe_cbind_plan_destroy(plan);
  }

  it("rejects transient borrowed strings and rolls back an earlier owning value") {
    static const char schema[] =
        "message Text { string owned; string borrowed; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("owned"), TOKEN_SLICE(CSERDE_STRING, "copy", CSERDE_VIEW_STABLE),
        TOKEN_KEY("borrowed"), TOKEN_SLICE(CSERDE_STRING, "view", CSERDE_VIEW_TRANSIENT),
        TOKEN_MAP_END};
    unsigned char scratch[1] = {0};
    tbe_cbind_test_strings out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan =
        make_plan(schema, "Text", &tbe_cbind_test_strings_data);

    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 1u, 16u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_UNSUPPORTED);
    check_null(out.owned);
    check_null(out.borrowed.data);
    check_equal(out.borrowed.len, (size_t)0);
    tbe_cbind_plan_destroy(plan);
  }

  it("performs authoritative destination and scratch preflight before source reads") {
    static const char schema[] = "message One { int32 value; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("value"), TOKEN_SINT(1), TOKEN_MAP_END};
    unsigned char scratch[1] = {0};
    tbe_cbind_test_one out = {9};
    cbind_error error = CBIND_ERROR_INIT;
    size_t consumed = SIZE_MAX;
    tbe_cbind_plan *plan = make_plan(schema, "One", &tbe_cbind_test_one_data);

    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 1u, 0u, scratch, sizeof(scratch), &error,
                              &consumed),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(consumed, (size_t)0);

    out.value = 0;
    error = (cbind_error)CBIND_ERROR_INIT;
    consumed = SIZE_MAX;
    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 1u, 0u, NULL, 0u, &error, &consumed),
                CBIND_LIMIT_EXCEEDED);
    check_equal(consumed, (size_t)0);
    tbe_cbind_plan_destroy(plan);
  }

  it("preserves strict unknown duplicate missing and range statuses with rollback") {
    static const char schema[] = "message One { int32 value; }";
    static const cserde_token unknown[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("other"), TOKEN_SINT(1), TOKEN_MAP_END};
    static const cserde_token duplicate[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("value"), TOKEN_SINT(1),
        TOKEN_KEY("value"), TOKEN_SINT(2), TOKEN_MAP_END};
    static const cserde_token missing[] = {TOKEN_MAP_BEGIN, TOKEN_MAP_END};
    static const cserde_token range[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("value"), TOKEN_SINT(INT64_MAX), TOKEN_MAP_END};
    const cserde_token *const cases[] = {unknown, duplicate, missing, range};
    const size_t counts[] = {
        sizeof(unknown) / sizeof(unknown[0]),
        sizeof(duplicate) / sizeof(duplicate[0]),
        sizeof(missing) / sizeof(missing[0]),
        sizeof(range) / sizeof(range[0])};
    const cbind_status expected[] = {
        CBIND_UNKNOWN_FIELD, CBIND_DUPLICATE_FIELD, CBIND_MISSING_FIELD,
        CBIND_VALUE_OUT_OF_RANGE};
    unsigned char scratch[1] = {0};
    size_t index;
    tbe_cbind_plan *plan = make_plan(schema, "One", &tbe_cbind_test_one_data);
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      tbe_cbind_test_one out = {0};
      cbind_error error = CBIND_ERROR_INIT;
      check_equal(decode_tokens(plan, cases[index], counts[index], SIZE_MAX,
                                &out, 1u, 0u, scratch, sizeof(scratch),
                                &error, NULL),
                  expected[index]);
      check_equal(out.value, 0);
      check_equal(error.status, expected[index]);
    }
    tbe_cbind_plan_destroy(plan);
  }

  it("propagates source and buffer limits without converting CBind errors") {
    static const char schema[] = "message Text { string owned; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("owned"),
        TOKEN_SLICE(CSERDE_STRING, "wide", CSERDE_VIEW_STABLE), TOKEN_MAP_END};
    cmeta_data_desc data = tbe_cbind_test_strings_data;
    cmeta_data_struct_shape shape = tbe_cbind_test_strings_shape;
    cmeta_struct_desc layout = tbe_cbind_test_strings_layout;
    cmeta_data_field_desc data_field = tbe_cbind_test_strings_data_fields[0];
    cmeta_field_desc layout_field = tbe_cbind_test_strings_layout_fields[0];
    unsigned char scratch[1] = {0};
    tbe_cbind_test_strings out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan;
    shape.fields = &data_field;
    shape.field_count = 1u;
    layout.fields = &layout_field;
    layout.field_count = 1u;
    shape.layout = &layout;
    data.shape = &shape;
    plan = make_plan(schema, "Text", &data);

    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 1u, 3u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_LIMIT_EXCEEDED);
    check_null(out.owned);

    error = (cbind_error)CBIND_ERROR_INIT;
    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), 1u,
                              &out, 1u, 8u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_SOURCE_ERROR);
    check_equal(error.source_status, CSERDE_SOURCE_ERROR);
    check_null(out.owned);
    tbe_cbind_plan_destroy(plan);
  }

  it("rolls back a complete nested graph after a later token mismatch") {
    static const char schema[] =
        "composite Detail { int32 quantity; } "
        "message Root { Detail detail; double score; }";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("detail"), TOKEN_MAP_BEGIN,
        TOKEN_KEY("quantity"), TOKEN_SINT(11), TOKEN_MAP_END,
        TOKEN_KEY("score"), TOKEN_KEY("wrong"), TOKEN_MAP_END};
    unsigned char scratch[2] = {0};
    tbe_cbind_test_nested out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan =
        make_plan(schema, "Root", &tbe_cbind_test_nested_data);
    check_equal(decode_tokens(plan, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), SIZE_MAX,
                              &out, 2u, 0u, scratch, sizeof(scratch), &error,
                              NULL),
                CBIND_TOKEN_MISMATCH);
    check_equal(out.detail.quantity, 0);
    check(out.score == 0.0);
    check_not_null(error.field);
    check_equal(error.field->name, "score");
    tbe_cbind_plan_destroy(plan);
  }

  it("shares one immutable ready plan across independent thread execution state") {
    enum { THREAD_COUNT = 4 };
    static const char schema[] =
        "message One { [name(external), c(value)] int32 internal; }";
    tbe_cbind_plan *plan = make_plan(schema, "One", &tbe_cbind_test_one_data);
    turbo_thread_t threads[THREAD_COUNT] = {0};
    decode_worker workers[THREAD_COUNT];
    size_t created = 0u;
    size_t index;
    for (index = 0u; index < THREAD_COUNT; ++index) {
      workers[index] = (decode_worker){plan, (int)index + 10, 0,
                                       CBIND_INVALID_ARGUMENT};
      if (turbo_thread_create(&threads[index], decode_worker_run,
                              &workers[index]) == 0)
        ++created;
      else
        break;
    }
    for (index = 0u; index < created; ++index)
      check_equal(turbo_thread_join(&threads[index]), 0);
    check_equal(created, (size_t)THREAD_COUNT);
    for (index = 0u; index < created; ++index) {
      check_equal(workers[index].status, CBIND_OK);
      check_equal(workers[index].output, workers[index].input);
    }
    tbe_cbind_plan_destroy(plan);
  }
}
