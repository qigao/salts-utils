#include "data_bind_projection_plan.h"
#include "tinytest.h"

#include <string.h>

static DataBind *projection_plan_codec(void) {
  static const char schema[] =
      "schema ProjectionPlan [version(1)];"
      "message Request {"
      " optional uint32 optional_id;"
      " nullable uint32 nullable_id;"
      " optional nullable uint32 tri_id;"
      "}"
      "message Response {"
      " uint32 value;"
      "}"
      "enum CsvState <uint8> { Ready = 1; Busy = 2; }"
      "message CsvFlat {"
      " uint32 id;"
      " CsvState state;"
      " optional string note;"
      "}"
      "composite CsvPoint { int32 x; int32 y; }"
      "message CsvNested { CsvPoint point; }"
      "message CsvList { list<uint32> values; }"
      "message CsvSet { set<uint32> values; }"
      "message CsvMap { map<string,int32> attrs; }"
      "union CsvChoice { CsvPoint point; }"
      "message CsvUnion { CsvChoice choice; }"
      "message NamedRoot {"
      " [name(orderId), alias(legacyId)] uint32 id;"
      " uint32 qty;"
      "}"
      "message NamedChild {"
      " [name(childValue), alias(legacyChild)] uint32 value;"
      "}"
      "message NamedNested { NamedChild child; }"
      "service Store {"
      " Read: Request -> Response;"
      "}"
      "service ShapeStore {"
      " Nested: CsvNested -> Response;"
      " List: CsvList -> Response;"
      "}";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  return data_bind_create_from_text(
             schema, sizeof(schema) - 1u, &codec, &error) == DATA_BIND_OK
             ? codec
             : NULL;
}

typedef struct PlanTokenReader {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} PlanTokenReader;

static cserde_status plan_token_next(void *context, cserde_token *out) {
  PlanTokenReader *state = (PlanTokenReader *)context;
  if (state == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (state->index >= state->count) return CSERDE_DONE;
  *out = state->tokens[state->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops PLAN_TOKEN_READER_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    plan_token_next};

static cserde_token plan_key(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static int plan_reader_init(
    cserde_reader *reader,
    PlanTokenReader *state,
    const cserde_token *tokens,
    size_t count) {
  if (reader == NULL || state == NULL || tokens == NULL) return 0;
  *reader = (cserde_reader){0};
  state->tokens = tokens;
  state->count = count;
  state->index = 0u;
  return cserde_reader_init(
             reader, &PLAN_TOKEN_READER_OPS, state) == CSERDE_OK;
}

static int token_key_equal(const cserde_token *token, const char *text) {
  size_t length;
  if (token == NULL || text == NULL || token->kind != CSERDE_STRING)
    return 0;
  length = strlen(text);
  return token->value.slice.size == length &&
         (length == 0u ||
          memcmp(token->value.slice.data, text, length) == 0);
}

static DataBindStatus projection_collision_codec(
    DataBind **out_codec) {
  static const char schema[] =
      "message Collision {"
      " [name(shared)] uint32 left;"
      " [alias(shared)] uint32 right;"
      "}";
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (out_codec == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out_codec = NULL;
  return data_bind_create_from_text(
      schema, sizeof(schema) - 1u, out_codec, &error);
}

spec("DataBind FormatPlan and TransportPlan") {
  it("freezes format state-space facts without runtime schema lookup") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *json = NULL;
    DataBindFormatPlanInfo info = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_JSON, &json, &error),
        DATA_BIND_OK);
    check_not_null(json);
    check(data_bind_format_plan_info(json, &info));
    check_equal(info.abi_version,
                (uint32_t)DATA_BIND_PROJECTION_PLAN_ABI_VERSION);
    check_equal(info.type_name, "Request");
    check_equal(info.format, DATA_BIND_FORMAT_JSON);
    check_true(info.has_optional);
    check_true(info.has_nullable);
    check((info.value_states & DATA_BIND_FORMAT_STATE_VALUE) != 0u);
    check((info.value_states & DATA_BIND_FORMAT_STATE_ABSENT) != 0u);
    check((info.value_states & DATA_BIND_FORMAT_STATE_NULL) != 0u);

    data_bind_free(codec);
    codec = NULL;

    /* The plan copied its execution facts and type identity. */
    info = (DataBindFormatPlanInfo)DATA_BIND_FORMAT_PLAN_INFO_INIT;
    check(data_bind_format_plan_info(json, &info));
    check_equal(info.type_name, "Request");
    check_true(info.has_nullable);

    data_bind_format_plan_free(json);
  }

  it("canonicalizes primary alias and canonical root field names without schema lookup") {
    static const char *const accepted[] = {
        "orderId", "legacyId", "id"};
    size_t case_index;

    for (case_index = 0u;
         case_index < sizeof(accepted) / sizeof(accepted[0]);
         ++case_index) {
      DataBind *codec = projection_plan_codec();
      DataBindFormatPlan *plan = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;
      DataBindFormatCanonicalReader canonical =
          DATA_BIND_FORMAT_CANONICAL_READER_INIT;
      PlanTokenReader source = {0};
      cserde_reader raw = {0};
      cserde_reader *reader;
      cserde_token token = {0};
      const cserde_token tokens[] = {
          {.kind = CSERDE_MAP_BEGIN},
          plan_key(accepted[case_index]),
          {.kind = CSERDE_UINT, .value.uint = 7u},
          plan_key("qty"),
          {.kind = CSERDE_UINT, .value.uint = 2u},
          {.kind = CSERDE_MAP_END},
          {.kind = CSERDE_UINT, .value.uint = 99u},
      };

      check_not_null(codec);
      if (codec == NULL) return;
      check_equal(
          data_bind_format_plan_compile(
              codec, "NamedRoot", DATA_BIND_FORMAT_JSON, &plan, &error),
          DATA_BIND_OK);
      check_not_null(plan);

      /* Mapping is plan-owned; runtime canonicalization retains no codec. */
      data_bind_free(codec);
      codec = NULL;

      check_true(plan_reader_init(
          &raw, &source, tokens,
          sizeof(tokens) / sizeof(tokens[0])));
      check_equal(
          data_bind_format_canonical_reader_init(
              plan, &raw, &canonical, &error),
          DATA_BIND_OK);
      reader =
          data_bind_format_canonical_reader_reader(&canonical);
      check_not_null(reader);
      if (reader == NULL) {
        data_bind_format_plan_free(plan);
        return;
      }

      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_MAP_BEGIN);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_true(token_key_equal(&token, "id"));
      check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_UINT);
      check_equal(token.value.uint, (uint64_t)7u);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_true(token_key_equal(&token, "qty"));
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_UINT);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_MAP_END);

      /* The wrapper owns one root value and never consumes the sentinel. */
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
      check_equal(source.index, (size_t)6u);

      data_bind_format_plan_free(plan);
    }
  }

  it("normalizes primary and alias spellings to one duplicate logical field") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindFormatCanonicalReader canonical =
        DATA_BIND_FORMAT_CANONICAL_READER_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    PlanTokenReader source = {0};
    cserde_reader raw = {0};
    cserde_reader *reader;
    cserde_token token = {0};
    const cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        plan_key("orderId"),
        {.kind = CSERDE_UINT, .value.uint = 1u},
        plan_key("legacyId"),
        {.kind = CSERDE_UINT, .value.uint = 2u},
        {.kind = CSERDE_MAP_END},
    };

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_format_plan_compile(
            codec, "NamedRoot", DATA_BIND_FORMAT_JSON, &plan, &error),
        DATA_BIND_OK);
    check_true(plan_reader_init(
        &raw, &source, tokens,
        sizeof(tokens) / sizeof(tokens[0])));
    check_equal(
        data_bind_format_canonical_reader_init(
            plan, &raw, &canonical, &error),
        DATA_BIND_OK);
    reader = data_bind_format_canonical_reader_reader(&canonical);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_true(token_key_equal(&token, "id"));
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_true(token_key_equal(&token, "id"));
    }

    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

  it("rejects unknown root names nested alias gaps and cross-field collisions") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindFormatCanonicalReader canonical =
        DATA_BIND_FORMAT_CANONICAL_READER_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    PlanTokenReader source = {0};
    cserde_reader raw = {0};
    cserde_reader *reader;
    cserde_token token = {0};
    const cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        plan_key("vendor"),
        {.kind = CSERDE_UINT, .value.uint = 1u},
        {.kind = CSERDE_MAP_END},
    };

    check_not_null(codec);
    if (codec == NULL) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "NamedRoot", DATA_BIND_FORMAT_JSON, &plan, &error),
        DATA_BIND_OK);
    check_true(plan_reader_init(
        &raw, &source, tokens,
        sizeof(tokens) / sizeof(tokens[0])));
    check_equal(
        data_bind_format_canonical_reader_init(
            plan, &raw, &canonical, &error),
        DATA_BIND_OK);
    reader = data_bind_format_canonical_reader_reader(&canonical);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(cserde_reader_next(reader, &token), CSERDE_UNSUPPORTED);
    }
    data_bind_format_plan_free(plan);
    plan = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_format_plan_compile(
            codec, "NamedNested", DATA_BIND_FORMAT_JSON, &plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_contains(error.message, "nested type");

    /* Text naming is orthogonal to Binary layout and does not gate Binary. */
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_format_plan_compile(
            codec, "NamedNested", DATA_BIND_FORMAT_BINARY, &plan, &error),
        DATA_BIND_OK);
    data_bind_format_plan_free(plan);
    data_bind_free(codec);

    codec = NULL;
    {
      DataBindStatus collision_status =
          projection_collision_codec(&codec);
      if (collision_status == DATA_BIND_OK) {
        plan = NULL;
        error = (DataBindError)DATA_BIND_ERROR_INIT;
        check_equal(
            data_bind_format_plan_compile(
                codec, "Collision", DATA_BIND_FORMAT_JSON, &plan, &error),
            DATA_BIND_ERR_SCHEMA);
        check_null(plan);
        check_contains(error.message, "collides");
        data_bind_free(codec);
      } else {
        /* The schema layer may reject the ambiguous input namespace earlier. */
        check(collision_status != DATA_BIND_OK);
        check_null(codec);
      }
    }
  }

  it("fails closed when the selected format cannot preserve explicit NULL") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_CSV, &plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_contains(error.message, "NULL");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_XML, &plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(plan);

    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_YAML, &plan, &error),
        DATA_BIND_OK);
    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

  it("admits only flat scalar/enum CSV record shapes") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    static const char *const rejected[] = {
        "CsvNested", "CsvList", "CsvSet", "CsvMap", "CsvUnion", "CsvChoice"};
    size_t i;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "CsvFlat", DATA_BIND_FORMAT_CSV, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    data_bind_format_plan_free(plan);
    plan = NULL;

    for (i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      check_equal(
          data_bind_format_plan_compile(
              codec, rejected[i], DATA_BIND_FORMAT_CSV, &plan, &error),
          DATA_BIND_ERR_SCHEMA);
      check_null(plan);
      check_contains(error.message, "CSV FormatPlan");
    }

    check_equal(
        data_bind_format_plan_compile(
            codec, "CsvNested", DATA_BIND_FORMAT_JSON, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

  it("admits nested XML objects but rejects collection and variant shapes") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindTransportPlan *transport = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    static const char *const rejected[] = {
        "CsvList", "CsvSet", "CsvMap", "CsvUnion", "CsvChoice"};
    size_t i;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "CsvNested", DATA_BIND_FORMAT_XML, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    data_bind_format_plan_free(plan);
    plan = NULL;

    for (i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      check_equal(
          data_bind_format_plan_compile(
              codec, rejected[i], DATA_BIND_FORMAT_XML, &plan, &error),
          DATA_BIND_ERR_SCHEMA);
      check_null(plan);
      check_contains(error.message, "XML FormatPlan");
    }

    check_equal(
        data_bind_format_plan_compile(
            codec, "CsvList", DATA_BIND_FORMAT_JSON, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    data_bind_format_plan_free(plan);
    plan = NULL;

    check_equal(
        data_bind_transport_plan_compile_service(
            codec, "ShapeStore", "Nested", DATA_BIND_TRANSPORT_HTTP,
            DATA_BIND_FORMAT_XML, DATA_BIND_FORMAT_XML,
            &transport, &error),
        DATA_BIND_OK);
    check_not_null(transport);
    data_bind_transport_plan_free(transport);
    transport = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_transport_plan_compile_service(
            codec, "ShapeStore", "List", DATA_BIND_TRANSPORT_RPC,
            DATA_BIND_FORMAT_XML, DATA_BIND_FORMAT_XML,
            &transport, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(transport);
    check_contains(error.message, "XML FormatPlan");

    data_bind_free(codec);
  }

  it("composes independent ingress and egress FormatPlans into one transport") {
    DataBind *codec = projection_plan_codec();
    DataBindTransportPlan *transport = NULL;
    DataBindTransportPlanInfo info = DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
    DataBindFormatPlanInfo ingress = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindFormatPlanInfo egress = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_transport_plan_compile_service(
            codec, "Store", "Read", DATA_BIND_TRANSPORT_HTTP,
            DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_XML,
            &transport, &error),
        DATA_BIND_OK);
    check_not_null(transport);
    check(data_bind_transport_plan_info(transport, &info));
    check_equal(info.kind, DATA_BIND_TRANSPORT_HTTP);
    check_equal(info.service_name, "Store");
    check_equal(info.operation_name, "Read");
    check_not_null(info.ingress);
    check_not_null(info.egress);
    check(data_bind_format_plan_info(info.ingress, &ingress));
    check(data_bind_format_plan_info(info.egress, &egress));
    check_equal(ingress.type_name, "Request");
    check_equal(ingress.format, DATA_BIND_FORMAT_JSON);
    check_equal(egress.type_name, "Response");
    check_equal(egress.format, DATA_BIND_FORMAT_XML);

    data_bind_transport_plan_free(transport);

    transport = NULL;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_transport_plan_compile_service(
            codec, "Store", "Read", DATA_BIND_TRANSPORT_RPC,
            DATA_BIND_FORMAT_CSV, DATA_BIND_FORMAT_JSON,
            &transport, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(transport);

    data_bind_free(codec);
  }
}
