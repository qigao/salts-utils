#include "data_bind_projection_plan.h"
#include "tinytest.h"

#include <tstr.h>
#include <stdio.h>
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
      "message XmlOptionalList { optional list<uint32> values; }"
      "message XmlNestedList { list<list<uint32>> values; }"
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
      "message OtherNamedChild { [name(childValue), alias(legacyChild)] uint32 other; }"
      "message NamedBranches { NamedChild child; OtherNamedChild other; uint32 qty; }"
      "message NamedList { list<NamedChild> children; }"
      "service Store {"
      " Read: Request -> Response;"
      "}"
      "service ShapeStore {"
      " Nested: CsvNested -> Response;"
      " List: CsvList -> Response;"
      " ReadNames: NamedRoot -> CsvMap;"
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


typedef struct PlanTokenWriter {
  cserde_token tokens[16];
  size_t count;
} PlanTokenWriter;

static cserde_status plan_token_write(void *opaque, const cserde_token *token) {
  PlanTokenWriter *sink = (PlanTokenWriter *)opaque;
  if (sink == NULL || token == NULL || sink->count >= 16u)
    return CSERDE_LIMIT_EXCEEDED;
  sink->tokens[sink->count++] = *token;
  return CSERDE_OK;
}

static cserde_status plan_token_finish(void *opaque) {
  return opaque != NULL ? CSERDE_OK : CSERDE_SINK_ERROR;
}

static const cserde_writer_ops PLAN_TOKEN_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    plan_token_write, plan_token_finish};

static int plan_writer_init(
    cserde_writer *writer, PlanTokenWriter *sink) {
  if (writer == NULL || sink == NULL) return 0;
  *writer = (cserde_writer){0};
  memset(sink, 0, sizeof(*sink));
  return cserde_writer_init(
             writer, &PLAN_TOKEN_WRITER_OPS, sink) == CSERDE_OK;
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
  it("projects nested names by record scope with independent cursors after codec destruction") {
    const DataBindFormat formats[] = {
        DATA_BIND_FORMAT_XML, DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_YAML};
    const cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN}, plan_key("child"),
        {.kind = CSERDE_MAP_BEGIN}, plan_key("legacyChild"),
        {.kind = CSERDE_UINT, .value.uint = 7u}, {.kind = CSERDE_MAP_END},
        plan_key("other"), {.kind = CSERDE_MAP_BEGIN}, plan_key("legacyChild"),
        {.kind = CSERDE_UINT, .value.uint = 9u}, {.kind = CSERDE_MAP_END},
        plan_key("qty"), {.kind = CSERDE_UINT, .value.uint = 2u},
        {.kind = CSERDE_MAP_END}};
    size_t f;
    for (f = 0u; f < sizeof(formats) / sizeof(formats[0]); ++f) {
      DataBind *codec = projection_plan_codec();
      DataBindFormatPlan *plan = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;
      DataBindFormatCanonicalReader ingress = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
      DataBindFormatCanonicalWriter egress = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
      DataBindFormatCursor read_cursor = DATA_BIND_FORMAT_CURSOR_INIT;
      DataBindFormatCursor write_cursor = DATA_BIND_FORMAT_CURSOR_INIT;
      PlanTokenReader source = {0};
      PlanTokenWriter sink = {0};
      cserde_reader raw = {0};
      cserde_writer target = {0};
      cserde_token token = {0};
      cserde_slice retained_key = {0};
      size_t i;
      check_not_null(codec);
      check_equal(data_bind_format_plan_compile(codec, "NamedBranches", formats[f], &plan, &error),
                  DATA_BIND_OK);
      data_bind_free(codec);
      check_true(plan_reader_init(&raw, &source, tokens, sizeof(tokens) / sizeof(tokens[0])));
      check_true(plan_writer_init(&target, &sink));
      check_equal(data_bind_format_canonical_writer_init(plan, &target, &egress, &error),
                  DATA_BIND_ERR_SCHEMA);
      check_equal(sink.count, (size_t)0u);
      check_equal(data_bind_format_canonical_reader_init_recursive(
                      plan, &raw, &ingress, &read_cursor, &error), DATA_BIND_OK);
      check_equal(data_bind_format_canonical_writer_init_recursive(
                      plan, &target, &egress, &write_cursor, &error), DATA_BIND_OK);
      for (i = 0u; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_OK);
        if (i == 3u) {
          check_true(token_key_equal(&token, "value"));
          retained_key = token.value.slice;
        }
        if (i == 8u) check_true(token_key_equal(&token, "other"));
        check_equal(cserde_writer_write(&egress.writer, &token), CSERDE_OK);
      }
      check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_DONE);
      check_equal(cserde_writer_finish(&egress.writer), CSERDE_OK);
      check_equal(target.state, CSERDE_WRITER_READY);
      check_true(token_key_equal(&sink.tokens[3], "childValue"));
      check_true(token_key_equal(&sink.tokens[8], "childValue"));
      check_true(token_key_equal(&sink.tokens[11], "qty"));
      check_equal(retained_key.size, (size_t)5u);
      check_equal(memcmp(retained_key.data, "value", 5u), 0);
      data_bind_format_plan_free(plan);
    }
  }

  it("normalizes nested duplicates and rejects unknown keys and malformed container ends") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const char *keys[] = {"legacyChild", "childValue", "value", "unknown"};
    size_t k;
    check_equal(data_bind_format_plan_compile(codec, "NamedNested", DATA_BIND_FORMAT_XML, &plan, &error),
                DATA_BIND_OK);
    for (k = 0u; k < sizeof(keys) / sizeof(keys[0]); ++k) {
      const cserde_token tokens[] = {
          {.kind = CSERDE_MAP_BEGIN}, plan_key("child"), {.kind = CSERDE_MAP_BEGIN},
          plan_key("childValue"), {.kind = CSERDE_UINT, .value.uint = 1u},
          plan_key(keys[k]), {.kind = CSERDE_UINT, .value.uint = 2u},
          {.kind = CSERDE_ARRAY_END}};
      DataBindFormatCanonicalReader ingress = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
      DataBindFormatCursor cursor = DATA_BIND_FORMAT_CURSOR_INIT;
      PlanTokenReader source = {0};
      cserde_reader raw = {0};
      cserde_token token = {0};
      size_t i;
      check_true(plan_reader_init(&raw, &source, tokens, sizeof(tokens) / sizeof(tokens[0])));
      check_equal(data_bind_format_canonical_reader_init_recursive(
                      plan, &raw, &ingress, &cursor, &error), DATA_BIND_OK);
      for (i = 0u; i < 5u; ++i)
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_OK);
      if (k == 3u) {
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_UNSUPPORTED);
      } else {
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_OK);
        check_true(token_key_equal(&token, "value"));
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_OK);
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_UNSUPPORTED);
      }
    }
    data_bind_format_plan_free(plan);
    plan = NULL;
    check_equal(data_bind_format_plan_compile(codec, "NamedList", DATA_BIND_FORMAT_JSON, &plan, &error),
                DATA_BIND_OK);
    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

  it("bounds recursive cursor depth including forwarded collection containers") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    cserde_token tokens[2u * DATA_BIND_FORMAT_CURSOR_MAX_DEPTH + 2u] = {{0}};
    size_t excessive;
    check_equal(data_bind_format_plan_compile(codec, "CsvList", DATA_BIND_FORMAT_JSON, &plan, &error),
                DATA_BIND_OK);
    for (excessive = 0u; excessive <= 1u; ++excessive) {
      DataBindFormatCanonicalReader ingress = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
      DataBindFormatCursor cursor = DATA_BIND_FORMAT_CURSOR_INIT;
      PlanTokenReader source = {0};
      cserde_reader raw = {0};
      cserde_token token = {0};
      size_t count = 0u, i;
      tokens[count++].kind = CSERDE_MAP_BEGIN;
      tokens[count++] = plan_key("values");
      for (i = 1u; i < DATA_BIND_FORMAT_CURSOR_MAX_DEPTH + excessive; ++i)
        tokens[count++].kind = CSERDE_ARRAY_BEGIN;
      for (i = 1u; i < DATA_BIND_FORMAT_CURSOR_MAX_DEPTH + excessive; ++i)
        tokens[count++].kind = CSERDE_ARRAY_END;
      tokens[count++].kind = CSERDE_MAP_END;
      check_true(plan_reader_init(&raw, &source, tokens, count));
      check_equal(data_bind_format_canonical_reader_init_recursive(
                      plan, &raw, &ingress, &cursor, &error), DATA_BIND_OK);
      for (i = 0u; i < (excessive ? DATA_BIND_FORMAT_CURSOR_MAX_DEPTH + 1u : count); ++i)
        check_equal(cserde_reader_next(&ingress.reader, &token), CSERDE_OK);
      check_equal(cserde_reader_next(&ingress.reader, &token),
                  excessive ? CSERDE_LIMIT_EXCEEDED : CSERDE_DONE);
    }
    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

  it("rejects incomplete recursive output and input-only aliases without finishing the target") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cserde_token prefix[] = {
        {.kind = CSERDE_MAP_BEGIN}, plan_key("child"), {.kind = CSERDE_MAP_BEGIN}};
    size_t attempt;
    check_equal(data_bind_format_plan_compile(codec, "NamedNested", DATA_BIND_FORMAT_XML, &plan, &error),
                DATA_BIND_OK);
    for (attempt = 0u; attempt < 3u; ++attempt) {
      DataBindFormatCanonicalWriter egress = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
      DataBindFormatCursor cursor = DATA_BIND_FORMAT_CURSOR_INIT;
      PlanTokenWriter sink = {0};
      cserde_writer target = {0};
      cserde_token key = plan_key(attempt == 1u ? "legacyChild" : "value");
      size_t i;
      check_true(plan_writer_init(&target, &sink));
      check_equal(data_bind_format_canonical_writer_init_recursive(
                      plan, &target, &egress, &cursor, &error), DATA_BIND_OK);
      for (i = 0u; i < sizeof(prefix) / sizeof(prefix[0]); ++i)
        check_equal(cserde_writer_write(&egress.writer, &prefix[i]), CSERDE_OK);
      if (attempt == 2u) {
        /* A failing borrowed sink must propagate its limit without a retry. */
        sink.count = sizeof(sink.tokens) / sizeof(sink.tokens[0]);
        check_equal(cserde_writer_write(&egress.writer, &key), CSERDE_LIMIT_EXCEEDED);
        check_equal(cserde_writer_finish(&egress.writer), CSERDE_LIMIT_EXCEEDED);
      } else {
        check_equal(cserde_writer_write(&egress.writer, &key),
                    attempt == 1u ? CSERDE_UNSUPPORTED : CSERDE_OK);
        check_equal(cserde_writer_finish(&egress.writer), CSERDE_UNSUPPORTED);
        check_equal(target.state, CSERDE_WRITER_READY);
        check_equal(sink.count, attempt == 1u ? (size_t)3u : (size_t)4u);
      }
    }
    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

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


  it("retains every mapping across managed storage growth and codec destruction") {
    enum { FIELD_COUNT = 20, NAME_BYTES = 32 };
    static const char *const prefixes[] = {"field", "wire", "old"};
    DataBind *codec = NULL;
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    tstr schema = tstr_dup("message ManyNames {");
    size_t i, spelling;
    check_not_null(schema);
    if (schema == NULL) return;
    for (i = 0u; i < FIELD_COUNT; ++i) {
      tstr next = tstr_cat_fmt(schema,
          "[name(wire%zu), alias(old%zu)] uint32 field%zu;", i, i, i);
      check_not_null(next);
      if (next == NULL) {
        tstr_free(schema);
        return;
      }
      schema = next;
    }
    {
      tstr next = tstr_cat(schema, "}");
      check_not_null(next);
      if (next == NULL) {
        tstr_free(schema);
        return;
      }
      schema = next;
    }
    check_equal(data_bind_create_from_text(schema, tstr_len(schema), &codec, &error),
                DATA_BIND_OK);
    tstr_free(schema);
    if (codec == NULL) return;
    check_equal(data_bind_format_plan_compile(
        codec, "ManyNames", DATA_BIND_FORMAT_JSON, &plan, &error), DATA_BIND_OK);
    data_bind_free(codec);
    if (plan == NULL) return;

    for (i = 0u; i < FIELD_COUNT; ++i) {
      for (spelling = 0u; spelling < sizeof(prefixes) / sizeof(prefixes[0]); ++spelling) {
        char input_name[NAME_BYTES], canonical_name[NAME_BYTES], output_name[NAME_BYTES];
        cserde_token tokens[] = {
            {.kind = CSERDE_MAP_BEGIN}, {0},
            {.kind = CSERDE_UINT, .value.uint = i}, {.kind = CSERDE_MAP_END}};
        cserde_token token = {0};
        cserde_reader raw = {0};
        cserde_writer target = {0};
        PlanTokenReader source = {0};
        PlanTokenWriter sink = {0};
        DataBindFormatCanonicalReader ingress = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
        DataBindFormatCanonicalWriter egress = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
        cserde_reader *reader;
        cserde_writer *writer;
        size_t n;
        snprintf(input_name, sizeof(input_name), "%s%zu", prefixes[spelling], i);
        snprintf(canonical_name, sizeof(canonical_name), "field%zu", i);
        snprintf(output_name, sizeof(output_name), "wire%zu", i);
        tokens[1] = plan_key(input_name);
        /* Input tokens are length-delimited; lookup cannot require a C string. */
        input_name[tokens[1].value.slice.size] = '#';
        check_true(plan_reader_init(&raw, &source, tokens, sizeof(tokens) / sizeof(tokens[0])));
        check_true(plan_writer_init(&target, &sink));
        check_equal(data_bind_format_canonical_reader_init(plan, &raw, &ingress, &error),
                    DATA_BIND_OK);
        check_equal(data_bind_format_canonical_writer_init(plan, &target, &egress, &error),
                    DATA_BIND_OK);
        reader = data_bind_format_canonical_reader_reader(&ingress);
        writer = data_bind_format_canonical_writer_writer(&egress);
        for (n = 0u; n < sizeof(tokens) / sizeof(tokens[0]); ++n) {
          check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
          if (n == 1u) check_true(token_key_equal(&token, canonical_name));
          check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
        }
        check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
        check_equal(cserde_writer_finish(writer), CSERDE_OK);
        check_equal(cserde_writer_finish(&target), CSERDE_OK);
        check_equal(sink.count, sizeof(tokens) / sizeof(tokens[0]));
        check_true(token_key_equal(&sink.tokens[1], output_name));
        check_equal(sink.tokens[2].value.uint, (uint64_t)i);
      }
    }
    data_bind_format_plan_free(plan);
  }

  it("projects canonical egress names to primary external names only") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindFormatCanonicalWriter canonical =
        DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    PlanTokenWriter sink = {0};
    cserde_writer target = {0};
    cserde_writer *writer;
    cserde_token token = {0};

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_format_plan_compile(
            codec, "NamedRoot", DATA_BIND_FORMAT_JSON, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    data_bind_free(codec);
    codec = NULL;

    check_true(plan_writer_init(&target, &sink));
    check_equal(
        data_bind_format_canonical_writer_init(
            plan, &target, &canonical, &error),
        DATA_BIND_OK);
    writer = data_bind_format_canonical_writer_writer(&canonical);
    check_not_null(writer);
    if (writer == NULL) {
      data_bind_format_plan_free(plan);
      return;
    }

    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = plan_key("id");
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_UINT, .value.uint = 7u};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = plan_key("qty");
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_UINT, .value.uint = 2u};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_MAP_END};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    check_equal(cserde_writer_finish(writer), CSERDE_OK);
    check_equal(target.state, CSERDE_WRITER_READY);
    check_equal(cserde_writer_finish(&target), CSERDE_OK);

    check_equal(sink.count, (size_t)6u);
    check_equal(sink.tokens[0].kind, CSERDE_MAP_BEGIN);
    check_true(token_key_equal(&sink.tokens[1], "orderId"));
    check_equal(sink.tokens[2].kind, CSERDE_UINT);
    check_true(token_key_equal(&sink.tokens[3], "qty"));
    check_equal(sink.tokens[5].kind, CSERDE_MAP_END);

    canonical = (DataBindFormatCanonicalWriter)
        DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
    target = (cserde_writer){0};
    check_true(plan_writer_init(&target, &sink));
    check_equal(
        data_bind_format_canonical_writer_init(
            plan, &target, &canonical, &error),
        DATA_BIND_OK);
    writer = data_bind_format_canonical_writer_writer(&canonical);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = plan_key("legacyId");
    check_equal(cserde_writer_write(writer, &token), CSERDE_UNSUPPORTED);

    data_bind_format_plan_free(plan);
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

  it("rejects unknown names legacy nested cursors and cross-field collisions") {
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
        DATA_BIND_OK);
    check_equal(data_bind_format_canonical_reader_init(plan, &raw, &canonical, &error),
                DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "recursive");
    data_bind_format_plan_free(plan);
    plan = NULL;

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

    /* Reader admission preserves the format's actual token states without
     * requiring it to publish every logical output state. XML/CSV may read a
     * nullable contract when the concrete source supplies ABSENT/VALUE only. */
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_format_plan_compile_reader(
            codec, "Request", DATA_BIND_FORMAT_XML, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan != NULL) {
      DataBindFormatPlanInfo reader_info = DATA_BIND_FORMAT_PLAN_INFO_INIT;
      check(data_bind_format_plan_info(plan, &reader_info));
      check_true(reader_info.has_nullable);
      check((reader_info.value_states & DATA_BIND_FORMAT_STATE_NULL) == 0u);
      data_bind_format_plan_free(plan);
      plan = NULL;
    }

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_format_plan_compile_reader(
            codec, "Request", DATA_BIND_FORMAT_CSV, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    data_bind_format_plan_free(plan);
    plan = NULL;

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

  it("admits XML records and required sequences while rejecting ambiguous collection shapes") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindTransportPlan *transport = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    static const char *const rejected[] = {
        "CsvMap", "CsvUnion", "CsvChoice", "XmlOptionalList", "XmlNestedList"};
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
        DATA_BIND_OK);
    data_bind_transport_plan_free(transport);

    data_bind_free(codec);
  }

  it("publishes no partial transport when egress fails and owns retry snapshots") {
    DataBind *codec = projection_plan_codec();
    DataBindTransportPlan *transport = NULL;
    DataBindTransportPlanInfo info = DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
    DataBindFormatPlanInfo egress = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindFormatCanonicalReader canonical = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
    PlanTokenReader source = {0};
    cserde_reader raw = {0};
    cserde_reader *reader;
    cserde_token token = {0};
    const cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN}, plan_key("legacyId"),
        {.kind = CSERDE_UINT, .value.uint = 7u}, {.kind = CSERDE_MAP_END}};

    check_not_null(codec);
    if (codec == NULL) return;
    /* JSON ingress already owns alias strings when XML egress is rejected. */
    check_equal(data_bind_transport_plan_compile_service(
        codec, "ShapeStore", "ReadNames", DATA_BIND_TRANSPORT_RPC,
        DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_XML, &transport, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(transport);
    check_equal(error.code, DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "XML FormatPlan");
    data_bind_transport_plan_free(transport);

    check_equal(data_bind_transport_plan_compile_service(
        codec, "ShapeStore", "ReadNames", DATA_BIND_TRANSPORT_RPC,
        DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_JSON, &transport, NULL),
        DATA_BIND_OK);
    check_not_null(transport);
    data_bind_free(codec);

    check_true(data_bind_transport_plan_info(transport, &info));
    check_equal(info.service_name, "ShapeStore");
    check_equal(info.operation_name, "ReadNames");
    check_true(data_bind_format_plan_info(info.egress, &egress));
    check_equal(egress.type_name, "CsvMap");
    check_equal(egress.format, DATA_BIND_FORMAT_JSON);
    check_true(plan_reader_init(&raw, &source, tokens,
        sizeof(tokens) / sizeof(tokens[0])));
    check_equal(data_bind_format_canonical_reader_init(
        info.ingress, &raw, &canonical, NULL), DATA_BIND_OK);
    reader = data_bind_format_canonical_reader_reader(&canonical);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_MAP_BEGIN);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_true(token_key_equal(&token, "id"));
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.value.uint, (uint64_t)7u);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_MAP_END);
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
    }
    data_bind_transport_plan_free(transport);
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
