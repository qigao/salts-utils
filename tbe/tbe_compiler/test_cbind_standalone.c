#include "cbind_standalone.h"

#include <turbo_cmeta_data.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_STANDALONE_LABEL "owned"
#define TEST_STANDALONE_UUID "00112233-4455-6677-8899-aabbccddeeff"

enum {
  TEST_STANDALONE_SCRATCH_BYTES = 2,
  TEST_STANDALONE_MAX_DEPTH = 2,
  TEST_STANDALONE_MAX_BUFFER_BYTES = sizeof(TEST_STANDALONE_UUID) - 1u
};

#define TEST_MAP_BEGIN {.kind = CSERDE_MAP_BEGIN}
#define TEST_MAP_END {.kind = CSERDE_MAP_END}
#define TEST_KEY(text_)                                                                            \
  {                                                                                                \
    .kind = CSERDE_STRING, .value.slice = {                                                        \
      (const unsigned char *)(text_), sizeof(text_) - 1u, CSERDE_VIEW_STABLE                       \
    }                                                                                              \
  }
#define TEST_SINT(value_) {.kind = CSERDE_SINT, .value.sint = (value_)}
#define TEST_UINT(value_) {.kind = CSERDE_UINT, .value.uint = (value_)}
#define TEST_BOOL(value_) {.kind = CSERDE_BOOL, .value.boolean = (value_)}

static const cserde_token test_tokens[] = {
    TEST_MAP_BEGIN, TEST_KEY("details"), TEST_MAP_BEGIN, TEST_KEY("sequence"), TEST_SINT(-17),
    TEST_MAP_END, TEST_KEY("eventId"), TEST_SINT(42), TEST_KEY("enabled"), TEST_BOOL(true),
    TEST_KEY("min_value"), TEST_SINT(INT64_MIN), TEST_KEY("max_value"), TEST_UINT(UINT64_MAX),
    TEST_KEY("label"), TEST_KEY(TEST_STANDALONE_LABEL), TEST_KEY("request_id"),
    TEST_KEY(TEST_STANDALONE_UUID), TEST_KEY("state"),
    TEST_KEY("CBindStandaloneState_Ready"), TEST_MAP_END};

typedef struct test_reader_state {
  size_t position;
} test_reader_state;

static cserde_status test_reader_next(void *opaque, cserde_token *out) {
  test_reader_state *state = (test_reader_state *)opaque;
  if (state == NULL || out == NULL ||
      state->position >= sizeof(test_tokens) / sizeof(test_tokens[0]))
    return CSERDE_SOURCE_ERROR;
  *out = test_tokens[state->position++];
  return CSERDE_OK;
}

static const cserde_reader_ops test_reader_ops = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, test_reader_next};

int main(void) {
  unsigned char scratch[TEST_STANDALONE_SCRATCH_BYTES] = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, sizeof(scratch), TEST_STANDALONE_MAX_DEPTH, 0u,
      TEST_STANDALONE_MAX_BUFFER_BYTES);
  cbind_error error = CBIND_ERROR_INIT;
  test_reader_state state = {0};
  cserde_reader reader = {0};
  CBindStandaloneEnvelope_t envelope = {0};
  turbo_uuid_t expected_uuid = {{0}};
  int result = 0;

  if (!cmeta_data_desc_valid(CBindStandaloneEnvelope_cbind_data())) {
    fprintf(stderr, "standalone CBind descriptor is invalid\n");
    return 1;
  }
  if (!cmeta_data_desc_valid(CBindStandaloneState_cbind_data())) {
    fprintf(stderr, "standalone CBind enum descriptor is invalid\n");
    return 2;
  }
  if (turbo_uuid_parse(TEST_STANDALONE_UUID, &expected_uuid) != 0) return 2;
  if (cserde_reader_init(&reader, &test_reader_ops, &state) != CSERDE_OK) {
    fprintf(stderr, "standalone CSerde reader initialization failed\n");
    return 3;
  }
  if (CBindStandaloneEnvelope_from_cserde(&context, &reader, &envelope, &error) != CBIND_OK) {
    fprintf(stderr, "standalone CBind decode failed: status=%d field=%s\n", (int)error.status,
            error.field != NULL ? error.field->name : "(none)");
    result = 4;
    goto cleanup;
  }
  if (envelope.details.sequence != -17 || envelope.event_id != 42 || envelope.enabled == 0 ||
      envelope.min_value != INT64_MIN || envelope.max_value != UINT64_MAX ||
      envelope.label == NULL ||
      tstr_len(envelope.label) != sizeof(TEST_STANDALONE_LABEL) - 1u ||
      memcmp(envelope.label, TEST_STANDALONE_LABEL,
             sizeof(TEST_STANDALONE_LABEL) - 1u) != 0 ||
      !turbo_uuid_equal(&envelope.request_id, &expected_uuid) ||
      envelope.state != CBindStandaloneState_Ready) {
    fprintf(stderr, "standalone CBind decode result mismatch\n");
    result = 5;
  }

cleanup:
  tstr_freep(&envelope.label);
  envelope = (CBindStandaloneEnvelope_t){0};
  if (envelope.details.sequence != 0 || envelope.label != NULL || envelope.event_id != 0 ||
      envelope.enabled != 0 || envelope.min_value != 0 || envelope.max_value != 0 ||
      envelope.state != CBindStandaloneState_Idle) {
    fprintf(stderr, "standalone CBind cleanup did not restore semantic zero\n");
    result = 6;
  }
  return result;
}

#undef TEST_SINT
#undef TEST_UINT
#undef TEST_BOOL
#undef TEST_KEY
#undef TEST_MAP_END
#undef TEST_MAP_BEGIN
#undef TEST_STANDALONE_LABEL
#undef TEST_STANDALONE_UUID
