#include "cbind_sidecar.h"

#include "json_cserde_reader.h"
#include "json_parser.h"
#include "tinytest.h"

#include <cbind/cbind.h>
#include <cmeta/data.h>
#include <turbo_cmeta_data.h>

#include <limits.h>
#include <string.h>

#define TEST_CBIND_NOTE "owned"
#define TEST_CBIND_UUID_LOWER "00112233-4455-6677-8899-aabbccddeeff"
#define TEST_CBIND_UUID_UPPER "00112233-4455-6677-8899-AABBCCDDEEFF"

enum {
  TEST_CBIND_HEADER_FIELD_COUNT = 1,
  TEST_CBIND_ENVELOPE_FIELD_COUNT = 17,
  TEST_CBIND_UUID_FIELD_INDEX = 14,
  TEST_CBIND_MAX_DEPTH = 2,
  TEST_JSON_MAX_DEPTH = TEST_CBIND_MAX_DEPTH,
  TEST_CBIND_MAX_CONTAINER_ITEMS = 0,
  TEST_CBIND_MAX_BUFFER_BYTES = sizeof(TEST_CBIND_UUID_LOWER) - 1u,
  TEST_CBIND_SCRATCH_BYTES =
      (TEST_CBIND_HEADER_FIELD_COUNT + CHAR_BIT - 1) / CHAR_BIT +
      (TEST_CBIND_ENVELOPE_FIELD_COUNT + CHAR_BIT - 1) / CHAR_BIT
};

typedef struct TestCbindDecodeObservation {
  cbind_status status;
  cbind_status error_status;
  cmeta_status target_status;
  const char *error_field_name;
  bool descriptor_valid;
  bool cleanup_complete;
  bool semantic_zero_before_clear;
  bool semantic_zero_after_clear;
  int32_t sequence;
  int32_t event_id;
  bool enabled;
  int8_t sint8;
  uint8_t uint8_value;
  int16_t sint16;
  uint16_t uint16_value;
  int32_t sint32;
  uint32_t uint32_value;
  int64_t sint64;
  uint64_t uint64_value;
  float real32;
  double real64;
  bool note_present;
  size_t note_size;
  char note[sizeof(TEST_CBIND_NOTE)];
  turbo_uuid_t request_id;
  CBindState_t state;
  CBindDefaultState_t default_state;
} TestCbindDecodeObservation;

static cbind_context test_cbind_context(unsigned char *scratch) {
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, TEST_CBIND_SCRATCH_BYTES, TEST_CBIND_MAX_DEPTH,
      TEST_CBIND_MAX_CONTAINER_ITEMS, TEST_CBIND_MAX_BUFFER_BYTES);
  return context;
}

static bool test_uuid_is_zero(const turbo_uuid_t *value) {
  static const turbo_uuid_t zero = {{0}};
  return value != NULL && memcmp(value, &zero, sizeof(zero)) == 0;
}

static bool test_envelope_is_semantic_zero(const CBindEnvelope_t *envelope) {
  return envelope != NULL && envelope->header.sequence == 0 && envelope->event_id == 0 &&
         envelope->enabled == false && envelope->sint8 == 0 && envelope->uint8_value == 0u &&
         envelope->sint16 == 0 && envelope->uint16_value == 0u && envelope->sint32 == 0 &&
         envelope->uint32_value == 0u && envelope->sint64 == 0 && envelope->uint64_value == 0u &&
         envelope->real32 == 0.0f && envelope->real64 == 0.0 && envelope->note == NULL &&
         test_uuid_is_zero(&envelope->request_id) && envelope->state == CBindState_Unknown &&
         envelope->default_state == CBindDefaultState_Idle;
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
  if (root == NULL) goto cleanup;
  reader = json_cserde_reader_create(root, TEST_JSON_MAX_DEPTH);
  if (reader == NULL) goto cleanup;

  observed.status = CBindEnvelope_from_cserde(&context, reader, &envelope, &error);
  observed.error_status = error.status;
  observed.target_status = error.target_status;
  observed.error_field_name = error.field != NULL ? error.field->name : NULL;
  observed.sequence = envelope.header.sequence;
  observed.event_id = envelope.event_id;
  observed.enabled = envelope.enabled != 0;
  observed.sint8 = envelope.sint8;
  observed.uint8_value = envelope.uint8_value;
  observed.sint16 = envelope.sint16;
  observed.uint16_value = envelope.uint16_value;
  observed.sint32 = envelope.sint32;
  observed.uint32_value = envelope.uint32_value;
  observed.sint64 = envelope.sint64;
  observed.uint64_value = envelope.uint64_value;
  observed.real32 = envelope.real32;
  observed.real64 = envelope.real64;
  observed.note_present = envelope.note != NULL;
  if (envelope.note != NULL) {
    observed.note_size = tstr_len(envelope.note);
    if (observed.note_size < sizeof(observed.note)) {
      memcpy(observed.note, envelope.note, observed.note_size);
      observed.note[observed.note_size] = '\0';
    }
  }
  observed.request_id = envelope.request_id;
  observed.state = envelope.state;
  observed.default_state = envelope.default_state;
  observed.semantic_zero_before_clear = test_envelope_is_semantic_zero(&envelope);

cleanup:
  if (reader != NULL) json_cserde_reader_destroy(reader);
  if (root != NULL) json_free(root);
  CBindEnvelope_clear(&envelope);
  observed.semantic_zero_after_clear = test_envelope_is_semantic_zero(&envelope);
  observed.cleanup_complete = true;
  return observed;
}

static void test_check_full_scalar_result(const TestCbindDecodeObservation *observed,
                                          const char *uuid_text,
                                          CBindState_t state) {
  turbo_uuid_t expected_uuid;
  check_not_null(observed);
  if (observed != NULL && observed->status != CBIND_OK)
    info("decode status=%d field=%s", (int)observed->status,
         observed->error_field_name != NULL ? observed->error_field_name : "(none)");
  check_equal(turbo_uuid_parse(uuid_text, &expected_uuid), 0);
  check_true(observed->cleanup_complete);
  check_true(observed->descriptor_valid);
  check_equal(observed->status, CBIND_OK);
  check_equal(observed->sequence, -17);
  check_equal(observed->event_id, 42);
  check_true(observed->enabled);
  check_equal(observed->sint8, INT8_MIN);
  check_equal(observed->uint8_value, UINT8_MAX);
  check_equal(observed->sint16, INT16_MIN);
  check_equal(observed->uint16_value, UINT16_MAX);
  check_equal(observed->sint32, INT32_MIN);
  check_true(observed->uint32_value == UINT32_MAX);
  check_true(observed->sint64 == INT64_MIN);
  check_true(observed->uint64_value == UINT64_MAX);
  check_true(observed->real32 == 1.25f);
  check_true(observed->real64 == 3.5);
  check_true(observed->note_present);
  check_equal(observed->note_size, sizeof(TEST_CBIND_NOTE) - 1u);
  check_equal(observed->note, TEST_CBIND_NOTE);
  check_true(turbo_uuid_equal(&observed->request_id, &expected_uuid));
  check_equal(observed->state, state);
  check_equal(observed->default_state, CBindDefaultState_Active);
  check_true(observed->semantic_zero_after_clear);
}

spec("generated CBind sidecar") {
  it("exposes ABI-correct struct UUID and enum metadata through its accessor") {
    const cmeta_data_desc *descriptor = CBindEnvelope_cbind_data();
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *state_descriptor = CBindState_cbind_data();
    const cmeta_data_enum_shape *state_shape;

    check_not_null(descriptor);
    check_true(cmeta_data_desc_valid(descriptor));
    check_equal(descriptor->struct_size, sizeof(cmeta_data_desc));
    check_equal(descriptor->abi_version, CMETA_DATA_DESC_ABI_VERSION);
    check_equal(descriptor->kind, CMETA_DATA_STRUCT);
    check_equal(descriptor->storage_type->size, sizeof(CBindEnvelope_t));
    check_equal(descriptor->storage_type->align, _Alignof(CBindEnvelope_t));
    shape = (const cmeta_data_struct_shape *)descriptor->shape;
    check_not_null(shape);
    check_equal(shape->field_count, TEST_CBIND_ENVELOPE_FIELD_COUNT);
    check_equal(shape->layout->size, sizeof(CBindEnvelope_t));
    check_equal(shape->layout->align, _Alignof(CBindEnvelope_t));
    check_equal(shape->layout->fields[0].name, "header");
    check_equal(shape->layout->fields[1].name, "eventId");
    check_equal(shape->layout->fields[TEST_CBIND_UUID_FIELD_INDEX].name, "request_id");
    check_true(shape->fields[TEST_CBIND_UUID_FIELD_INDEX].value == &turbo_uuid_cmeta_data);

    check_not_null(state_descriptor);
    check_true(cmeta_data_desc_valid(state_descriptor));
    check_equal(state_descriptor->kind, CMETA_DATA_ENUM);
    check_equal(state_descriptor->storage_type->size, sizeof(uint16_t));
    check_equal(state_descriptor->storage_type->align, _Alignof(uint16_t));
    state_shape = (const cmeta_data_enum_shape *)state_descriptor->shape;
    check_not_null(state_shape);
    check_equal(state_shape->meta->name, "CBindState");
    check_equal(state_shape->meta->count, 5u);
    check_equal(state_shape->meta->items[0].value, 0);
    check_equal(state_shape->meta->items[0].symbol, "CBindState_Unknown");
    check_equal(state_shape->meta->items[0].text, "Unknown");
    check_equal(state_shape->meta->items[1].value, 7);
    check_equal(state_shape->meta->items[1].symbol, "CBindState_Ready");
    check_equal(state_shape->meta->items[1].text, "Ready");
    check_equal(state_shape->meta->items[2].value, 9);
    check_equal(state_shape->meta->items[3].value, 10);
    check_equal(state_shape->meta->items[3].symbol, "CBindState_cbind_read");
    check_equal(state_shape->meta->items[3].text, "cbind_read");
    check_equal(state_shape->meta->items[4].value, 11);
    check_equal(state_shape->meta->items[4].symbol, "CBindState_cbind_descriptor");
    check_equal(state_shape->meta->items[4].text, "cbind_descriptor");
    check_equal(CBindState_cbind_read, (CBindState_t)10);
    check_equal(CBindState_cbind_descriptor, (CBindState_t)11);
    check_true(CBindState_cbind_data() == state_descriptor);
  }

  it("decodes every scalar width lowercase UUID and enum symbol") {
    static const char json[] =
        "{\"header\":{\"sequence\":-17},\"eventId\":42,\"enabled\":true,"
        "\"sint8\":-128,\"uint8_value\":255,\"sint16\":-32768,"
        "\"uint16_value\":65535,\"sint32\":-2147483648,"
        "\"uint32_value\":4294967295,\"sint64\":-9223372036854775808,"
        "\"uint64_value\":18446744073709551615,\"real32\":1.25,\"real64\":3.5,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"request_id\":\"" TEST_CBIND_UUID_LOWER "\","
        "\"state\":\"CBindState_Ready\",\"default_state\":\"Active\"}";
    TestCbindDecodeObservation observed = test_decode_json(json, sizeof(json) - 1u);
    test_check_full_scalar_result(&observed, TEST_CBIND_UUID_LOWER, CBindState_Ready);
  }

  it("accepts uppercase UUID enum text and enum numeric encodings") {
    static const char text_json[] =
        "{\"header\":{\"sequence\":-17},\"eventId\":42,\"enabled\":true,"
        "\"sint8\":-128,\"uint8_value\":255,\"sint16\":-32768,"
        "\"uint16_value\":65535,\"sint32\":-2147483648,"
        "\"uint32_value\":4294967295,\"sint64\":-9223372036854775808,"
        "\"uint64_value\":18446744073709551615,\"real32\":1.25,\"real64\":3.5,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"request_id\":\"" TEST_CBIND_UUID_UPPER "\","
        "\"state\":\"Ready\",\"default_state\":3}";
    static const char numeric_json[] =
        "{\"header\":{\"sequence\":-17},\"eventId\":42,\"enabled\":true,"
        "\"sint8\":-128,\"uint8_value\":255,\"sint16\":-32768,"
        "\"uint16_value\":65535,\"sint32\":-2147483648,"
        "\"uint32_value\":4294967295,\"sint64\":-9223372036854775808,"
        "\"uint64_value\":18446744073709551615,\"real32\":1.25,\"real64\":3.5,"
        "\"note\":\"" TEST_CBIND_NOTE "\",\"request_id\":\"" TEST_CBIND_UUID_LOWER "\","
        "\"state\":7,\"default_state\":3}";
    TestCbindDecodeObservation text_observed =
        test_decode_json(text_json, sizeof(text_json) - 1u);
    TestCbindDecodeObservation numeric_observed =
        test_decode_json(numeric_json, sizeof(numeric_json) - 1u);
    test_check_full_scalar_result(&text_observed, TEST_CBIND_UUID_UPPER, CBindState_Ready);
    test_check_full_scalar_result(&numeric_observed, TEST_CBIND_UUID_LOWER, CBindState_Ready);
  }

  it("rolls back earlier fields for invalid UUID and unknown enum inputs") {
    static const char invalid_uuid[] =
        "{\"header\":{\"sequence\":9},\"eventId\":7,\"note\":\"owned\","
        "\"request_id\":\"not-a-uuid\"}";
    static const char invalid_enum_text[] =
        "{\"header\":{\"sequence\":9},\"eventId\":7,\"note\":\"owned\","
        "\"request_id\":\"" TEST_CBIND_UUID_LOWER "\",\"state\":\"Missing\"}";
    static const char invalid_enum_number[] =
        "{\"header\":{\"sequence\":9},\"eventId\":7,\"note\":\"owned\","
        "\"request_id\":\"" TEST_CBIND_UUID_LOWER "\",\"state\":8}";
    TestCbindDecodeObservation uuid_observed =
        test_decode_json(invalid_uuid, sizeof(invalid_uuid) - 1u);
    TestCbindDecodeObservation text_observed =
        test_decode_json(invalid_enum_text, sizeof(invalid_enum_text) - 1u);
    TestCbindDecodeObservation number_observed =
        test_decode_json(invalid_enum_number, sizeof(invalid_enum_number) - 1u);

    check_equal(uuid_observed.status, CBIND_TARGET_ERROR);
    check_equal(uuid_observed.error_status, CBIND_TARGET_ERROR);
    check_equal(uuid_observed.target_status, CMETA_INVALID_ARGUMENT);
    check_equal(uuid_observed.error_field_name, "request_id");
    check_true(uuid_observed.semantic_zero_before_clear);
    check_false(uuid_observed.note_present);
    check_equal(text_observed.status, CBIND_VALUE_OUT_OF_RANGE);
    check_equal(text_observed.error_field_name, "state");
    check_true(text_observed.semantic_zero_before_clear);
    check_equal(number_observed.status, CBIND_VALUE_OUT_OF_RANGE);
    check_equal(number_observed.error_field_name, "state");
    check_true(number_observed.semantic_zero_before_clear);
    check_true(uuid_observed.semantic_zero_after_clear);
    check_true(text_observed.semantic_zero_after_clear);
    check_true(number_observed.semantic_zero_after_clear);
  }
}

#undef TEST_CBIND_UUID_UPPER
#undef TEST_CBIND_UUID_LOWER
#undef TEST_CBIND_NOTE
