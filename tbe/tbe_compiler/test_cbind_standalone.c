#include "cbind_standalone.h"

#include <stdio.h>
#include <string.h>

#define TEST_STANDALONE_LABEL "owned"

enum {
  TEST_STANDALONE_SCRATCH_BYTES = 2,
  TEST_STANDALONE_MAX_DEPTH = 2,
  TEST_STANDALONE_MAX_BUFFER_BYTES = sizeof(TEST_STANDALONE_LABEL) - 1u
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

static const cserde_token test_tokens[] = {
    TEST_MAP_BEGIN, TEST_KEY("details"), TEST_MAP_BEGIN, TEST_KEY("sequence"), TEST_SINT(-17),
    TEST_MAP_END, TEST_KEY("eventId"), TEST_SINT(42), TEST_KEY("label"),
    TEST_KEY(TEST_STANDALONE_LABEL), TEST_MAP_END};

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
  int result = 0;

  if (!cmeta_data_desc_valid(CBindStandaloneEnvelope_cbind_data())) {
    fprintf(stderr, "standalone CBind descriptor is invalid\n");
    return 1;
  }
  if (cserde_reader_init(&reader, &test_reader_ops, &state) != CSERDE_OK) {
    fprintf(stderr, "standalone CSerde reader initialization failed\n");
    return 2;
  }
  if (CBindStandaloneEnvelope_from_cserde(&context, &reader, &envelope, &error) != CBIND_OK) {
    fprintf(stderr, "standalone CBind decode failed: status=%d field=%s\n", (int)error.status,
            error.field != NULL ? error.field->name : "(none)");
    result = 3;
    goto cleanup;
  }
  if (envelope.details.sequence != -17 || envelope.event_id != 42 || envelope.label == NULL ||
      tstr_len(envelope.label) != sizeof(TEST_STANDALONE_LABEL) - 1u ||
      memcmp(envelope.label, TEST_STANDALONE_LABEL,
             sizeof(TEST_STANDALONE_LABEL) - 1u) != 0) {
    fprintf(stderr, "standalone CBind decode result mismatch\n");
    result = 4;
  }

cleanup:
  tstr_freep(&envelope.label);
  envelope = (CBindStandaloneEnvelope_t){0};
  if (envelope.details.sequence != 0 || envelope.label != NULL || envelope.event_id != 0) {
    fprintf(stderr, "standalone CBind cleanup did not restore semantic zero\n");
    result = 5;
  }
  return result;
}

#undef TEST_SINT
#undef TEST_KEY
#undef TEST_MAP_END
#undef TEST_MAP_BEGIN
#undef TEST_STANDALONE_LABEL
