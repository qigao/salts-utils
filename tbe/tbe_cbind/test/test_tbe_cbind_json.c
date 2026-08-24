#include <tbe_cbind/tbe_cbind.h>

#include "tbe_cbind_test_fixtures.h"
#include "tinytest.h"
#include "turbo_cmeta_data.h"
#include "turbo_parser_json.h"
#include "turbo_str.h"
#include "turbo_vstr.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_detail_native {
  int quantity;
} json_detail_native;

typedef struct json_order_native {
  json_detail_native detail;
  tstr symbol;
} json_order_native;

typedef struct json_view_native {
  vstr text;
} json_view_native;

typedef struct json_rollback_native {
  tstr owned;
  vstr borrowed;
} json_rollback_native;

static const char json_scalar_envelope_schema[] =
    "composite ScalarSet { "
    "[name(enabled), c(boolean)] bool flag; "
    "[name(min8), c(sint8)] int8 s8; "
    "[name(max8), c(uint8)] uint8 u8; "
    "[name(min16), c(sint16)] int16 s16; "
    "[name(max16), c(uint16)] uint16 u16; "
    "[name(min32), c(sint32)] int32 s32; "
    "[name(max32), c(uint32)] uint32 u32; "
    "[name(min64), c(sint64)] int64 s64; "
    "[name(max64), c(uint64)] uint64 u64; "
    "[name(single), c(real32)] float f32; "
    "[name(decimal), c(real64)] double f64; "
    "[name(identifier), c(uuid)] uuid id; } "
    "message Envelope { [name(payload), c(values)] ScalarSet body; }";

static const cmeta_data_buffer_shape json_owned_string_shape = {
    CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc json_owned_string_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.json.string.owned",
    .display_name = "JSON owning string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &json_owned_string_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops};

static const cmeta_data_buffer_shape json_borrowed_string_shape = {
    CMETA_DATA_BUFFER_BORROWED};
static const cmeta_data_desc json_borrowed_string_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.json.string.borrowed",
    .display_name = "JSON borrowed string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_vstr_cmeta_type,
    .shape = &json_borrowed_string_shape,
    .buffer_ops = &turbo_vstr_cmeta_buffer_ops};

static const cmeta_type_identity json_detail_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.json.detail");
static const cmeta_type_desc json_detail_type = {
    .name = "json_detail_native",
    .size = sizeof(json_detail_native),
    .align = _Alignof(json_detail_native),
    .kind = CMETA_T_OBJECT,
    .identity = &json_detail_identity};
static const cmeta_field_desc json_detail_layout_fields[] = {{
    "quantity", "int", offsetof(json_detail_native, quantity), sizeof(int),
    _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc json_detail_layout = {
    "json_detail_native", sizeof(json_detail_native),
    _Alignof(json_detail_native), json_detail_layout_fields, 1u};
static const cmeta_data_field_desc json_detail_data_fields[] = {{
    "test.tbe-cbind.json.detail.quantity", "quantity",
    offsetof(json_detail_native, quantity), &cmeta_data_int}};
static const cmeta_data_struct_shape json_detail_shape = {
    &json_detail_layout, json_detail_data_fields, 1u};
static const cmeta_data_desc json_detail_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.json.detail.data",
    .display_name = "JSON detail native",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &json_detail_type,
    .shape = &json_detail_shape};

static const cmeta_type_identity json_order_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.json.order");
static const cmeta_type_desc json_order_type = {
    .name = "json_order_native",
    .size = sizeof(json_order_native),
    .align = _Alignof(json_order_native),
    .kind = CMETA_T_OBJECT,
    .identity = &json_order_identity};
static const cmeta_field_desc json_order_layout_fields[] = {
    {"detail", "json_detail_native", offsetof(json_order_native, detail),
     sizeof(json_detail_native), _Alignof(json_detail_native),
     &json_detail_type, NULL},
    {"symbol", "tstr", offsetof(json_order_native, symbol), sizeof(tstr),
     _Alignof(tstr), &turbo_tstr_cmeta_type, NULL}};
static const cmeta_struct_desc json_order_layout = {
    "json_order_native", sizeof(json_order_native),
    _Alignof(json_order_native), json_order_layout_fields, 2u};
static const cmeta_data_field_desc json_order_data_fields[] = {
    {"test.tbe-cbind.json.order.detail", "detail",
     offsetof(json_order_native, detail), &json_detail_data},
    {"test.tbe-cbind.json.order.symbol", "symbol",
     offsetof(json_order_native, symbol), &json_owned_string_data}};
static const cmeta_data_struct_shape json_order_shape = {
    &json_order_layout, json_order_data_fields, 2u};
static const cmeta_data_desc json_order_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.json.order.data",
    .display_name = "JSON order native",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &json_order_type,
    .shape = &json_order_shape};

static const cmeta_type_identity json_view_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.json.view");
static const cmeta_type_desc json_view_type = {
    .name = "json_view_native",
    .size = sizeof(json_view_native),
    .align = _Alignof(json_view_native),
    .kind = CMETA_T_OBJECT,
    .identity = &json_view_identity};
static const cmeta_field_desc json_view_layout_fields[] = {{
    "text", "vstr", offsetof(json_view_native, text), sizeof(vstr),
    _Alignof(vstr), &turbo_vstr_cmeta_type, NULL}};
static const cmeta_struct_desc json_view_layout = {
    "json_view_native", sizeof(json_view_native), _Alignof(json_view_native),
    json_view_layout_fields, 1u};
static const cmeta_data_field_desc json_view_data_fields[] = {{
    "test.tbe-cbind.json.view.text", "text",
    offsetof(json_view_native, text), &json_borrowed_string_data}};
static const cmeta_data_struct_shape json_view_shape = {
    &json_view_layout, json_view_data_fields, 1u};
static const cmeta_data_desc json_view_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.json.view.data",
    .display_name = "JSON borrowed view native",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &json_view_type,
    .shape = &json_view_shape};

static const cmeta_type_identity json_rollback_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.json.rollback");
static const cmeta_type_desc json_rollback_type = {
    .name = "json_rollback_native",
    .size = sizeof(json_rollback_native),
    .align = _Alignof(json_rollback_native),
    .kind = CMETA_T_OBJECT,
    .identity = &json_rollback_identity};
static const cmeta_field_desc json_rollback_layout_fields[] = {
    {"owned", "tstr", offsetof(json_rollback_native, owned), sizeof(tstr),
     _Alignof(tstr), &turbo_tstr_cmeta_type, NULL},
    {"borrowed", "vstr", offsetof(json_rollback_native, borrowed),
     sizeof(vstr), _Alignof(vstr), &turbo_vstr_cmeta_type, NULL}};
static const cmeta_struct_desc json_rollback_layout = {
    "json_rollback_native", sizeof(json_rollback_native),
    _Alignof(json_rollback_native), json_rollback_layout_fields, 2u};
static const cmeta_data_field_desc json_rollback_data_fields[] = {
    {"test.tbe-cbind.json.rollback.owned", "owned",
     offsetof(json_rollback_native, owned), &json_owned_string_data},
    {"test.tbe-cbind.json.rollback.borrowed", "borrowed",
     offsetof(json_rollback_native, borrowed), &json_borrowed_string_data}};
static const cmeta_data_struct_shape json_rollback_shape = {
    &json_rollback_layout, json_rollback_data_fields, 2u};
static const cmeta_data_desc json_rollback_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.json.rollback.data",
    .display_name = "JSON rollback native",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &json_rollback_type,
    .shape = &json_rollback_shape};

static tbe_cbind_plan *json_make_plan(const char *schema, size_t schema_size,
                                      const char *type_name,
                                      size_t type_name_size,
                                      const cmeta_data_desc *native_shape) {
  tbe_cbind_plan_options options;
  tbe_cbind_plan_error error;
  tbe_cbind_plan *plan = NULL;
  tbe_cbind_plan_options_init(&options);
  tbe_cbind_plan_error_init(&error);
  check_equal(tbe_cbind_plan_create_from_text(
                  schema, schema_size, type_name, type_name_size,
                  native_shape, &options, &plan, &error),
              TBE_CBIND_OK);
  check_not_null(plan);
  return plan;
}

typedef struct transient_reader_context {
  const cserde_token *tokens;
  size_t token_count;
  size_t index;
} transient_reader_context;

static cserde_status transient_reader_next(void *opaque, cserde_token *out) {
  transient_reader_context *context = (transient_reader_context *)opaque;
  if (context == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (context->index == context->token_count) return CSERDE_DONE;
  *out = context->tokens[context->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops transient_reader_ops = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
    transient_reader_next};

spec("TbeCBind JSON integration") {
  it("decodes nested renamed fixed scalars and UUID case variants from JSON") {
    static const char lowercase_json[] =
        "{\"payload\":{\"enabled\":true,\"min8\":-128,\"max8\":255,"
        "\"min16\":-32768,\"max16\":65535,"
        "\"min32\":-2147483648,\"max32\":4294967295,"
        "\"min64\":-9223372036854775808,\"max64\":18446744073709551615,"
        "\"single\":1.5,\"decimal\":2.5,"
        "\"identifier\":\"00112233-4455-6677-8899-aabbccddeeff\"}}";
    static const char uppercase_json[] =
        "{\"payload\":{\"enabled\":true,\"min8\":-128,\"max8\":255,"
        "\"min16\":-32768,\"max16\":65535,"
        "\"min32\":-2147483648,\"max32\":4294967295,"
        "\"min64\":-9223372036854775808,\"max64\":18446744073709551615,"
        "\"single\":1.5,\"decimal\":2.5,"
        "\"identifier\":\"00112233-4455-6677-8899-AABBCCDDEEFF\"}}";
    static const char *const documents[] = {lowercase_json, uppercase_json};
    static const uint8_t expected_uuid[TURBO_UUID_SIZE] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
        0x88u, 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu};
    unsigned char scratch[32] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 2u, 0u, 64u);
    tbe_cbind_plan *plan = json_make_plan(
        json_scalar_envelope_schema, sizeof(json_scalar_envelope_schema) - 1u,
        "Envelope", sizeof("Envelope") - 1u,
        tbe_cbind_test_scalar_envelope_data_get());
    size_t index;

    for (index = 0u; index < sizeof(documents) / sizeof(documents[0]); ++index) {
      turbo_json_doc_t *document = NULL;
      cserde_reader *reader = NULL;
      tbe_cbind_test_scalar_envelope out = {0};
      cbind_error error = CBIND_ERROR_INIT;

      check_equal(turbo_parse_json((const uint8_t *)documents[index],
                                   strlen(documents[index]), &document),
                  0);
      reader = turbo_json_cserde_reader_create(document, 2u);
      check_not_null(reader);
      if (reader != NULL)
        check_equal(tbe_cbind_plan_decode(plan, &context, reader, &out, &error),
                    CBIND_OK);
      check_true(out.values.boolean);
      check_equal(out.values.sint8, INT8_MIN);
      check_equal(out.values.uint8, UINT8_MAX);
      check_equal(out.values.sint16, INT16_MIN);
      check_equal(out.values.uint16, UINT16_MAX);
      check_equal(out.values.sint32, INT32_MIN);
      check_equal(out.values.uint32, UINT32_MAX);
      check_equal(out.values.sint64, INT64_MIN);
      check_equal(out.values.uint64, UINT64_MAX);
      check(out.values.real32 == 1.5f);
      check(out.values.real64 == 2.5);
      check_equal(out.values.uuid.bytes, expected_uuid,
                  sizeof(expected_uuid));

      turbo_json_cserde_reader_destroy(reader);
      turbo_free_json(&document);
    }
    tbe_cbind_plan_destroy(plan);
  }

  it("decodes nested enum symbols and texts from JSON") {
    static const char *const documents[] = {
        "{\"detail\":{\"prefix\":11,\"state\":\"State_Ready\"},\"suffix\":9}",
        "{\"detail\":{\"prefix\":12,\"state\":\"Paused\"},\"suffix\":10}"};
    static const tbe_cbind_test_state expected_states[] = {
        TBE_CBIND_TEST_STATE_READY, TBE_CBIND_TEST_STATE_PAUSED};
    unsigned char scratch[32] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 2u, 0u, 0u);
    tbe_cbind_plan *plan = json_make_plan(
        tbe_cbind_test_state_envelope_schema,
        strlen(tbe_cbind_test_state_envelope_schema), "EnumEnvelope",
        sizeof("EnumEnvelope") - 1u,
        &tbe_cbind_test_enum_envelope_data);
    size_t index;

    for (index = 0u; index < sizeof(documents) / sizeof(documents[0]); ++index) {
      turbo_json_doc_t *document = NULL;
      cserde_reader *reader = NULL;
      tbe_cbind_test_enum_envelope out = {0};
      cbind_error error = CBIND_ERROR_INIT;

      check_equal(turbo_parse_json((const uint8_t *)documents[index],
                                   strlen(documents[index]), &document),
                  0);
      reader = turbo_json_cserde_reader_create(document, 2u);
      check_not_null(reader);
      if (reader != NULL)
        check_equal(tbe_cbind_plan_decode(plan, &context, reader, &out, &error),
                    CBIND_OK);
      check_equal(out.detail.prefix, (int32_t)(index + 11u));
      check_equal(out.detail.state, expected_states[index]);
      check_equal(out.suffix, (int32_t)(index + 9u));

      turbo_json_cserde_reader_destroy(reader);
      turbo_free_json(&document);
    }
    tbe_cbind_plan_destroy(plan);
  }

  it("rolls back JSON scalars when the final UUID is invalid") {
    static const char json[] =
        "{\"payload\":{\"enabled\":true,\"min8\":1,\"max8\":2,"
        "\"min16\":3,\"max16\":4,\"min32\":5,\"max32\":6,"
        "\"min64\":7,\"max64\":8,\"single\":1.5,\"decimal\":2.5,"
        "\"identifier\":\"00112233-4455-6677-8899-aabbccddeefX\"}}";
    static const tbe_cbind_test_scalar_envelope zero = {0};
    turbo_json_doc_t *document = NULL;
    cserde_reader *reader = NULL;
    tbe_cbind_test_scalar_envelope out = {0};
    unsigned char scratch[32] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 2u, 0u, 64u);
    cbind_error error = CBIND_ERROR_INIT;
    tbe_cbind_plan *plan = json_make_plan(
        json_scalar_envelope_schema, sizeof(json_scalar_envelope_schema) - 1u,
        "Envelope", sizeof("Envelope") - 1u,
        tbe_cbind_test_scalar_envelope_data_get());

    check_equal(turbo_parse_json((const uint8_t *)json, sizeof(json) - 1u,
                                 &document),
                0);
    reader = turbo_json_cserde_reader_create(document, 2u);
    check_not_null(reader);
    if (reader != NULL)
      check_equal(tbe_cbind_plan_decode(plan, &context, reader, &out, &error),
                  CBIND_TARGET_ERROR);
    check_equal(&out, &zero, sizeof(out));
    check_equal(error.target_status, CMETA_INVALID_ARGUMENT);

    turbo_json_cserde_reader_destroy(reader);
    turbo_free_json(&document);
    tbe_cbind_plan_destroy(plan);
  }

  it("rolls back the complete JSON record for narrow signed and unsigned overflow") {
    static const char *const documents[] = {
        "{\"payload\":{\"enabled\":true,\"min8\":-129}}",
        "{\"payload\":{\"enabled\":true,\"min8\":1,\"max8\":256}}"};
    static const tbe_cbind_test_scalar_envelope zero = {0};
    unsigned char scratch[32] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 2u, 0u, 64u);
    tbe_cbind_plan *plan = json_make_plan(
        json_scalar_envelope_schema, sizeof(json_scalar_envelope_schema) - 1u,
        "Envelope", sizeof("Envelope") - 1u,
        tbe_cbind_test_scalar_envelope_data_get());
    size_t index;

    for (index = 0u; index < sizeof(documents) / sizeof(documents[0]); ++index) {
      turbo_json_doc_t *document = NULL;
      cserde_reader *reader = NULL;
      tbe_cbind_test_scalar_envelope out = {0};
      cbind_error error = CBIND_ERROR_INIT;

      check_equal(turbo_parse_json((const uint8_t *)documents[index],
                                   strlen(documents[index]), &document),
                  0);
      reader = turbo_json_cserde_reader_create(document, 2u);
      check_not_null(reader);
      if (reader != NULL)
        check_equal(tbe_cbind_plan_decode(plan, &context, reader, &out, &error),
                    CBIND_VALUE_OUT_OF_RANGE);
      check_equal(&out, &zero, sizeof(out));
      check_equal(error.status, CBIND_VALUE_OUT_OF_RANGE);

      turbo_json_cserde_reader_destroy(reader);
      turbo_free_json(&document);
    }
    tbe_cbind_plan_destroy(plan);
  }

  it("decodes nested renamed JSON after schema and type buffers are released") {
    static const char schema_text[] =
        "composite Detail { [c(quantity), name(amount)] int32 count; } "
        "message Order { [c(detail), name(payload)] Detail body; "
        "[c(symbol), name(ticker)] string label; }";
    static const char json_text[] =
        "{\"payload\":{\"amount\":7},\"ticker\":\"TURBO\"}";
    char *schema = (char *)malloc(sizeof(schema_text) - 1u);
    char *type_name = (char *)malloc(sizeof("Order") - 1u);
    turbo_json_doc_t *document = NULL;
    cserde_reader *reader = NULL;
    tbe_cbind_plan *plan = NULL;
    json_order_native out = {0};
    unsigned char scratch[2] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 2u, 0u, 64u);
    cbind_error error = CBIND_ERROR_INIT;

    check_not_null(schema);
    check_not_null(type_name);
    if (schema == NULL || type_name == NULL) {
      free(schema);
      free(type_name);
      return;
    }
    memcpy(schema, schema_text, sizeof(schema_text) - 1u);
    memcpy(type_name, "Order", sizeof("Order") - 1u);
    plan = json_make_plan(schema, sizeof(schema_text) - 1u, type_name,
                          sizeof("Order") - 1u, &json_order_data);
    memset(schema, '?', sizeof(schema_text) - 1u);
    memset(type_name, '?', sizeof("Order") - 1u);
    free(schema);
    free(type_name);
    if (plan == NULL) return;

    check_equal(turbo_parse_json((const uint8_t *)json_text,
                                 sizeof(json_text) - 1u, &document),
                0);
    reader = turbo_json_cserde_reader_create(document, 2u);
    check_not_null(reader);
    if (reader != NULL)
      check_equal(tbe_cbind_plan_decode(plan, &context, reader, &out, &error),
                  CBIND_OK);
    turbo_json_cserde_reader_destroy(reader);
    turbo_free_json(&document);

    check_equal(out.detail.quantity, 7);
    check_not_null(out.symbol);
    if (out.symbol != NULL) check_equal(out.symbol, "TURBO");
    check_equal(cmeta_data_buffer_restore_zero(&json_owned_string_data,
                                               &out.symbol),
                CMETA_OK);
    tbe_cbind_plan_destroy(plan);
  }

  it("keeps JSON DOM slices valid after the reader wrapper is destroyed") {
    static const char schema[] =
        "message View { [c(text), name(label)] string value; }";
    static const char json[] = "{\"label\":\"dom-backed\"}";
    turbo_json_doc_t *document = NULL;
    json_value_t *string_node;
    const char *dom_text;
    cserde_reader *reader;
    tbe_cbind_plan *plan =
        json_make_plan(schema, sizeof(schema) - 1u, "View", 4u,
                       &json_view_data);
    json_view_native out = {0};
    unsigned char scratch[1] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 1u, 0u, 32u);
    cbind_error error = CBIND_ERROR_INIT;

    if (plan == NULL) return;
    check_equal(turbo_parse_json((const uint8_t *)json, sizeof(json) - 1u,
                                 &document),
                0);
    if (document == NULL) {
      tbe_cbind_plan_destroy(plan);
      return;
    }
    string_node = turbo_json_object_get(document, "label");
    dom_text = turbo_json_string(string_node);
    check_not_null(dom_text);
    reader = turbo_json_cserde_reader_create(document, 1u);
    check_not_null(reader);
    if (reader != NULL)
      check_equal(tbe_cbind_plan_decode(plan, &context, reader, &out, &error),
                  CBIND_OK);

    turbo_json_cserde_reader_destroy(reader);
    check_true(out.text.data == dom_text);
    check_equal(out.text.len, (size_t)10u);
    if (out.text.data != NULL)
      check_equal(out.text.data, (const char *)"dom-backed", out.text.len);

    check_equal(cmeta_data_buffer_restore_zero(&json_borrowed_string_data,
                                               &out.text),
                CMETA_OK);
    check_null(out.text.data);
    check_equal(out.text.len, (size_t)0u);
    turbo_free_json(&document);
    tbe_cbind_plan_destroy(plan);
  }

  it("rolls back an owning string when a later borrowed slice is transient") {
    static const char schema[] =
        "message Rollback { string owned; string borrowed; }";
    static const cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        {.kind = CSERDE_STRING,
         .value.slice = {(const unsigned char *)"owned", 5u,
                         CSERDE_VIEW_STABLE}},
        {.kind = CSERDE_STRING,
         .value.slice = {(const unsigned char *)"allocated", 9u,
                         CSERDE_VIEW_TRANSIENT}},
        {.kind = CSERDE_STRING,
         .value.slice = {(const unsigned char *)"borrowed", 8u,
                         CSERDE_VIEW_STABLE}},
        {.kind = CSERDE_STRING,
         .value.slice = {(const unsigned char *)"ephemeral", 9u,
                         CSERDE_VIEW_TRANSIENT}},
        {.kind = CSERDE_MAP_END}};
    transient_reader_context source = {
        tokens, sizeof(tokens) / sizeof(tokens[0]), 0u};
    cserde_reader reader = {0};
    tbe_cbind_plan *plan =
        json_make_plan(schema, sizeof(schema) - 1u, "Rollback", 8u,
                       &json_rollback_data);
    json_rollback_native out = {0};
    unsigned char scratch[1] = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), 1u, 0u, 32u);
    cbind_error error = CBIND_ERROR_INIT;

    if (plan == NULL) return;
    check_equal(cserde_reader_init(&reader, &transient_reader_ops, &source),
                CSERDE_OK);
    check_equal(tbe_cbind_plan_decode(plan, &context, &reader, &out, &error),
                CBIND_UNSUPPORTED);
    check_null(out.owned);
    check_null(out.borrowed.data);
    check_equal(out.borrowed.len, (size_t)0u);
    tbe_cbind_plan_destroy(plan);
  }
}
