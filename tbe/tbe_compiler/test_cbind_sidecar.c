#include "cbind_sidecar.h"

#include "json_cserde_reader.h"
#include "json_parser.h"
#include "tinytest.h"

#include <cbind/cbind.h>
#include <cmeta/data.h>

#include <limits.h>

#define TEST_CBIND_NOTE "owned"

enum {
  TEST_CBIND_HEADER_FIELD_COUNT = 1,
  TEST_CBIND_ENVELOPE_FIELD_COUNT = 4,
  TEST_CBIND_MAX_DEPTH = 2,
  TEST_JSON_MAX_DEPTH = TEST_CBIND_MAX_DEPTH,
  TEST_CBIND_MAX_CONTAINER_ITEMS = 0,
  TEST_CBIND_MAX_BUFFER_BYTES = sizeof(TEST_CBIND_NOTE) - 1u,
  TEST_CBIND_SCRATCH_BYTES =
      (TEST_CBIND_HEADER_FIELD_COUNT + CHAR_BIT - 1) / CHAR_BIT +
      (TEST_CBIND_ENVELOPE_FIELD_COUNT + CHAR_BIT - 1) / CHAR_BIT
};

static cbind_context test_cbind_context(unsigned char *scratch) {
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, TEST_CBIND_SCRATCH_BYTES, TEST_CBIND_MAX_DEPTH,
      TEST_CBIND_MAX_CONTAINER_ITEMS, TEST_CBIND_MAX_BUFFER_BYTES);
  return context;
}

spec("generated CBind sidecar") {
  it("decodes named JSON fields into generated owning C storage") {
    static const char json[] =
        "{\"header\":{\"sequence\":-17},\"eventId\":42,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"score\":3.5}";
    unsigned char scratch[TEST_CBIND_SCRATCH_BYTES] = {0};
    cbind_context context = test_cbind_context(scratch);
    cbind_error error = CBIND_ERROR_INIT;
    CBindEnvelope_t envelope;
    json_value_t *root;
    cserde_reader *reader;

    check_true(cmeta_data_desc_valid(CBindEnvelope_cbind_data()));
    root = json_parse(json, sizeof(json) - 1u);
    check_not_null(root);
    reader = json_cserde_reader_create(root, TEST_JSON_MAX_DEPTH);
    check_not_null(reader);

    CBindEnvelope_init(&envelope);
    if (reader != NULL)
      check_equal(CBindEnvelope_from_cserde(&context, reader, &envelope, &error), CBIND_OK);
    check_equal(envelope.header.sequence, -17);
    check_equal(envelope.event_id, 42);
    check_equal(envelope.note, TEST_CBIND_NOTE);
    check_true(envelope.score == 3.5);

    json_cserde_reader_destroy(reader);
    json_free(root);
    CBindEnvelope_clear(&envelope);
  }

  it("rolls back the owning string and earlier fields after a later token mismatch") {
    static const char json[] =
        "{\"header\":{\"sequence\":9},\"eventId\":7,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"score\":\"wrong\"}";
    unsigned char scratch[TEST_CBIND_SCRATCH_BYTES] = {0};
    cbind_context context = test_cbind_context(scratch);
    cbind_error error = CBIND_ERROR_INIT;
    CBindEnvelope_t envelope;
    json_value_t *root = json_parse(json, sizeof(json) - 1u);
    cserde_reader *reader = json_cserde_reader_create(root, TEST_JSON_MAX_DEPTH);

    check_not_null(root);
    check_not_null(reader);
    CBindEnvelope_init(&envelope);
    if (reader != NULL)
      check_equal(CBindEnvelope_from_cserde(&context, reader, &envelope, &error),
                  CBIND_TOKEN_MISMATCH);
    check_equal(error.status, CBIND_TOKEN_MISMATCH);
    check_not_null(error.field);
    if (error.field != NULL) check_equal(error.field->name, "score");
    check_equal(envelope.header.sequence, 0);
    check_equal(envelope.event_id, 0);
    check_null(envelope.note);
    check_true(envelope.score == 0.0);

    json_cserde_reader_destroy(reader);
    json_free(root);
    CBindEnvelope_clear(&envelope);
  }
}

#undef TEST_CBIND_NOTE
