#include "cbind_sidecar.h"

#include "json_cserde_reader.h"
#include "json_parser.h"
#include "tinytest.h"

#include <cbind/cbind.h>
#include <cmeta/data.h>

#include <limits.h>
#include <string.h>

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

typedef struct TestCbindDecodeObservation {
  bool descriptor_valid;
  bool root_created;
  bool reader_created;
  bool reader_destroyed;
  bool dom_freed;
  bool object_cleared;
  bool cleanup_complete;
  cbind_status status;
  cbind_status error_status;
  const char *error_field_name;
  int sequence;
  int event_id;
  bool note_present;
  size_t note_size;
  char note[sizeof(TEST_CBIND_NOTE)];
  double score;
  bool semantic_zero_before_clear;
  bool semantic_zero_after_clear;
} TestCbindDecodeObservation;

static bool test_envelope_is_semantic_zero(const CBindEnvelope_t *envelope) {
  return envelope != NULL && envelope->header.sequence == 0 && envelope->event_id == 0 &&
         envelope->note == NULL && envelope->score == 0.0;
}

static TestCbindDecodeObservation test_decode_json(const char *json, size_t json_size) {
  unsigned char scratch[TEST_CBIND_SCRATCH_BYTES] = {0};
  cbind_context context = test_cbind_context(scratch);
  cbind_error error = CBIND_ERROR_INIT;
  TestCbindDecodeObservation observed = {0};
  CBindEnvelope_t envelope;
  json_value_t *root = NULL;
  cserde_reader *reader = NULL;

  observed.status = CBIND_INVALID_ARGUMENT;
  observed.descriptor_valid = cmeta_data_desc_valid(CBindEnvelope_cbind_data());
  CBindEnvelope_init(&envelope);

  root = json_parse(json, json_size);
  observed.root_created = root != NULL;
  if (root == NULL) goto cleanup;

  reader = json_cserde_reader_create(root, TEST_JSON_MAX_DEPTH);
  observed.reader_created = reader != NULL;
  if (reader == NULL) goto cleanup;

  observed.status = CBindEnvelope_from_cserde(&context, reader, &envelope, &error);
  observed.error_status = error.status;
  observed.error_field_name = error.field != NULL ? error.field->name : NULL;
  observed.sequence = envelope.header.sequence;
  observed.event_id = envelope.event_id;
  observed.note_present = envelope.note != NULL;
  if (envelope.note != NULL) {
    observed.note_size = tstr_len(envelope.note);
    if (observed.note_size < sizeof(observed.note)) {
      memcpy(observed.note, envelope.note, observed.note_size);
      observed.note[observed.note_size] = '\0';
    }
  }
  observed.score = envelope.score;
  observed.semantic_zero_before_clear = test_envelope_is_semantic_zero(&envelope);

cleanup:
  if (reader != NULL) {
    json_cserde_reader_destroy(reader);
    reader = NULL;
    observed.reader_destroyed = true;
  }
  if (root != NULL) {
    json_free(root);
    root = NULL;
    observed.dom_freed = true;
  }
  CBindEnvelope_clear(&envelope);
  observed.object_cleared = true;
  observed.semantic_zero_after_clear = test_envelope_is_semantic_zero(&envelope);
  observed.cleanup_complete = (!observed.reader_created || observed.reader_destroyed) &&
                              (!observed.root_created || observed.dom_freed) &&
                              observed.object_cleared;
  return observed;
}

spec("generated CBind sidecar") {
  it("decodes named JSON fields into generated owning C storage") {
    static const char json[] =
        "{\"header\":{\"sequence\":-17},\"eventId\":42,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"score\":3.5}";
    TestCbindDecodeObservation observed = test_decode_json(json, sizeof(json) - 1u);

    check_true(observed.cleanup_complete);
    check_true(observed.reader_destroyed);
    check_true(observed.dom_freed);
    check_true(observed.object_cleared);
    check_true(observed.descriptor_valid);
    check_true(observed.root_created);
    check_true(observed.reader_created);
    check_equal(observed.status, CBIND_OK);
    check_equal(observed.sequence, -17);
    check_equal(observed.event_id, 42);
    check_true(observed.note_present);
    check_equal(observed.note_size, sizeof(TEST_CBIND_NOTE) - 1u);
    check_equal(observed.note, TEST_CBIND_NOTE);
    check_true(observed.score == 3.5);
    check_true(observed.semantic_zero_after_clear);
  }

  it("rolls back the owning string and earlier fields after a later token mismatch") {
    static const char json[] =
        "{\"header\":{\"sequence\":9},\"eventId\":7,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"score\":\"wrong\"}";
    TestCbindDecodeObservation observed = test_decode_json(json, sizeof(json) - 1u);

    check_true(observed.cleanup_complete);
    check_true(observed.reader_destroyed);
    check_true(observed.dom_freed);
    check_true(observed.object_cleared);
    check_true(observed.root_created);
    check_true(observed.reader_created);
    check_equal(observed.status, CBIND_TOKEN_MISMATCH);
    check_equal(observed.error_status, CBIND_TOKEN_MISMATCH);
    check_equal(observed.error_field_name, "score");
    check_true(observed.semantic_zero_before_clear);
    check_false(observed.note_present);
    check_true(observed.semantic_zero_after_clear);
  }
}

#undef TEST_CBIND_NOTE
