#include "data_bind_message_plan.h"
#include "tinytest.h"

#include <cmeta/object.h>
#include <cmeta/struct.h>
#include <salts_cmeta_data.h>

#include <string.h>

typedef struct DynamicRecord {
  int marker;
  int64_t id_slot;
  unsigned char gap[19];
  double score_slot;
} DynamicRecord;

typedef struct DynamicProvider {
  size_t reads;
  size_t assigns;
  int reject_assign;
} DynamicProvider;

static const cmeta_type_identity DYNAMIC_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.databind.dynamic-record");
static const cmeta_type_desc DYNAMIC_TYPE = {
    .name = "DynamicRecord",
    .size = sizeof(DynamicRecord),
    .align = _Alignof(DynamicRecord),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &DYNAMIC_IDENTITY
};

static const cmeta_field_desc DYNAMIC_LAYOUT_FIELDS[] = {
    {
        .name = "id",
        .type_name = "int64_t",
        .offset = CMETA_FIELD_DYNAMIC_OFFSET,
        .size = sizeof(int64_t),
        .align = _Alignof(int64_t),
        .type = &cmeta_type_int64,
        .declared_type = NULL
    },
    {
        .name = "score",
        .type_name = "double",
        .offset = CMETA_FIELD_DYNAMIC_OFFSET,
        .size = sizeof(double),
        .align = _Alignof(double),
        .type = &cmeta_type_double,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc DYNAMIC_LAYOUT = {
    .name = "DynamicRecord",
    .size = sizeof(DynamicRecord),
    .align = _Alignof(DynamicRecord),
    .fields = DYNAMIC_LAYOUT_FIELDS,
    .field_count = 2u
};

static const cmeta_data_field_desc DYNAMIC_FIELDS[] = {
    {
        .stable_id = "test.databind.dynamic-record.id",
        .name = "id",
        .offset = CMETA_FIELD_DYNAMIC_OFFSET,
        .value = &cmeta_data_int64
    },
    {
        .stable_id = "test.databind.dynamic-record.score",
        .name = "score",
        .offset = CMETA_FIELD_DYNAMIC_OFFSET,
        .value = &cmeta_data_double
    }
};

static const cmeta_data_struct_shape DYNAMIC_SHAPE = {
    .layout = &DYNAMIC_LAYOUT,
    .fields = DYNAMIC_FIELDS,
    .field_count = 2u
};

static const cmeta_data_desc DYNAMIC_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.databind.dynamic-record.data",
    .display_name = "DynamicRecord",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &DYNAMIC_TYPE,
    .shape = &DYNAMIC_SHAPE,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static cmeta_status dynamic_read(
    void *context, const void *object,
    const cmeta_data_field_desc *field, const void **out_value) {
  DynamicProvider *provider = (DynamicProvider *)context;
  const DynamicRecord *record = (const DynamicRecord *)object;
  if (!provider || !record || !field || !out_value)
    return CMETA_INVALID_ARGUMENT;
  ++provider->reads;
  if (strcmp(field->name, "id") == 0) {
    *out_value = &record->id_slot;
    return CMETA_OK;
  }
  if (strcmp(field->name, "score") == 0) {
    *out_value = &record->score_slot;
    return CMETA_OK;
  }
  *out_value = NULL;
  return CMETA_TRAIT_MISSING;
}

static cmeta_status dynamic_assign(
    void *context, void *object,
    const cmeta_data_field_desc *field, const void *value) {
  DynamicProvider *provider = (DynamicProvider *)context;
  DynamicRecord *record = (DynamicRecord *)object;
  if (!provider || !record || !field || !value)
    return CMETA_INVALID_ARGUMENT;
  if (provider->reject_assign) return CMETA_CALLBACK_ERROR;
  ++provider->assigns;
  if (strcmp(field->name, "id") == 0) {
    record->id_slot = *(const int64_t *)value;
    return CMETA_OK;
  }
  if (strcmp(field->name, "score") == 0) {
    record->score_slot = *(const double *)value;
    return CMETA_OK;
  }
  return CMETA_TRAIT_MISSING;
}

typedef struct TextDynamicRecord {
  int marker;
  tstr name_slot;
} TextDynamicRecord;

typedef struct TextDynamicProvider {
  size_t reads;
  size_t assigns;
} TextDynamicProvider;

static const cmeta_type_identity TEXT_DYNAMIC_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.databind.text-dynamic-record");
static const cmeta_type_desc TEXT_DYNAMIC_TYPE = {
    .name = "TextDynamicRecord",
    .size = sizeof(TextDynamicRecord),
    .align = _Alignof(TextDynamicRecord),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &TEXT_DYNAMIC_IDENTITY
};
static const cmeta_field_desc TEXT_DYNAMIC_LAYOUT_FIELDS[] = {{
    .name = "name",
    .type_name = "string",
    .offset = CMETA_FIELD_DYNAMIC_OFFSET,
    .size = sizeof(tstr),
    .align = _Alignof(tstr),
    .type = &salts_tstr_cmeta_type,
    .declared_type = NULL
}};
static const cmeta_struct_desc TEXT_DYNAMIC_LAYOUT = {
    .name = "TextDynamicRecord",
    .size = sizeof(TextDynamicRecord),
    .align = _Alignof(TextDynamicRecord),
    .fields = TEXT_DYNAMIC_LAYOUT_FIELDS,
    .field_count = 1u
};
static const cmeta_data_field_desc TEXT_DYNAMIC_FIELDS[] = {{
    .stable_id = "test.databind.text-dynamic-record.name",
    .name = "name",
    .offset = CMETA_FIELD_DYNAMIC_OFFSET,
    .value = &salts_tstr_cmeta_data
}};
static const cmeta_data_struct_shape TEXT_DYNAMIC_SHAPE = {
    .layout = &TEXT_DYNAMIC_LAYOUT,
    .fields = TEXT_DYNAMIC_FIELDS,
    .field_count = 1u
};
static const cmeta_data_desc TEXT_DYNAMIC_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.databind.text-dynamic-record.data",
    .display_name = "TextDynamicRecord",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &TEXT_DYNAMIC_TYPE,
    .shape = &TEXT_DYNAMIC_SHAPE,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static cmeta_status text_dynamic_read(
    void *context, const void *object,
    const cmeta_data_field_desc *field, const void **out_value) {
  TextDynamicProvider *provider = (TextDynamicProvider *)context;
  const TextDynamicRecord *record = (const TextDynamicRecord *)object;
  if (!provider || !record || !field || !out_value)
    return CMETA_INVALID_ARGUMENT;
  if (strcmp(field->name, "name") != 0) return CMETA_TRAIT_MISSING;
  ++provider->reads;
  *out_value = &record->name_slot;
  return CMETA_OK;
}

static cmeta_status text_dynamic_assign(
    void *context, void *object,
    const cmeta_data_field_desc *field, const void *value) {
  TextDynamicProvider *provider = (TextDynamicProvider *)context;
  TextDynamicRecord *record = (TextDynamicRecord *)object;
  if (!provider || !record || !field || !value)
    return CMETA_INVALID_ARGUMENT;
  if (strcmp(field->name, "name") != 0) return CMETA_TRAIT_MISSING;
  ++provider->assigns;
  return cmeta_data_value_copy(
      &salts_tstr_cmeta_data, &record->name_slot, value);
}

typedef struct TokenReader {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} TokenReader;

static cserde_status token_reader_next(void *context, cserde_token *out) {
  TokenReader *reader = (TokenReader *)context;
  if (!reader || !out) return CSERDE_INVALID_ARGUMENT;
  if (reader->index >= reader->count) return CSERDE_DONE;
  *out = reader->tokens[reader->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops TOKEN_READER_OPS = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
    token_reader_next
};

typedef struct TokenWriter {
  cserde_token tokens[16];
  size_t count;
} TokenWriter;

static cserde_status token_writer_write(
    void *context, const cserde_token *token) {
  TokenWriter *writer = (TokenWriter *)context;
  if (!writer || !token) return CSERDE_INVALID_ARGUMENT;
  if (writer->count >= 16u) return CSERDE_LIMIT_EXCEEDED;
  writer->tokens[writer->count++] = *token;
  return CSERDE_OK;
}

static cserde_status token_writer_finish(void *context) {
  return context != NULL ? CSERDE_OK : CSERDE_INVALID_ARGUMENT;
}

static const cserde_writer_ops TOKEN_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    token_writer_write, token_writer_finish
};

static cserde_token key_token(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static DataBind *make_codec(void) {
  static const char schema[] =
      "schema DynamicObject [version(1)];"
      "message Dynamic { @Min(1) int64 id; double score; }";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(
      data_bind_create_from_text(
          schema, sizeof(schema) - 1u, &codec, &error),
      DATA_BIND_OK);
  return codec;
}

static DataBindMessagePlan *make_plan(DataBind *codec) {
  DataBindMessagePlan *plan = NULL;
  DataBindMessagePlanDiagnostic diagnostic =
      DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
  check_true(cmeta_data_desc_valid(&DYNAMIC_DATA));
  check_equal(
      data_bind_message_plan_compile_object(
          codec, "Dynamic", &DYNAMIC_DATA, &plan, &diagnostic),
      DATA_BIND_OK);
  return plan;
}

static cmeta_object_ref make_object(
    DynamicRecord *record, DynamicProvider *provider,
    cmeta_object_field_provider *field_provider) {
  cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
  *field_provider = (cmeta_object_field_provider){
      .size = sizeof(cmeta_object_field_provider),
      .data = &DYNAMIC_DATA,
      .context = provider,
      .assign = dynamic_assign,
      .read = dynamic_read
  };
  check_equal(
      cmeta_object_borrow_with_providers(
          &object, record, &DYNAMIC_DATA, field_provider, NULL),
      CMETA_OK);
  return object;
}

spec("DataBind provider-backed object MessagePlan") {
  it("decodes and encodes dynamic slots without native offsets") {
    DataBind *codec = make_codec();
    DataBindMessagePlan *plan = make_plan(codec);
    DynamicRecord record = {.marker = 77, .id_slot = -1, .score_slot = -1.0};
    DynamicProvider provider = {0};
    cmeta_object_field_provider field_provider;
    cmeta_object_ref object = make_object(&record, &provider, &field_provider);
    unsigned char workspace[2048] = {0};
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    const cserde_token input[] = {
        {.kind = CSERDE_MAP_BEGIN},
        key_token("score"),
        {.kind = CSERDE_FLOAT, .value.floating = 3.5},
        key_token("id"),
        {.kind = CSERDE_SINT, .value.sint = 7},
        {.kind = CSERDE_MAP_END}
    };
    TokenReader source = {input, sizeof(input) / sizeof(input[0]), 0u};
    cserde_reader reader = {0};
    TokenWriter sink = {0};
    cserde_writer writer = {0};
    DataBindError validation = DATA_BIND_ERROR_INIT;

    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    options.max_depth = 8u;
    options.max_items = 32u;
    options.max_owned_bytes = 1024u;

    check_equal(
        cserde_reader_init(&reader, &TOKEN_READER_OPS, &source), CSERDE_OK);
    check_equal(
        data_bind_message_plan_decode_object(
            plan, &options, &reader, &object, NULL, &diagnostic),
        DATA_BIND_OK);
    check_equal(record.marker, 77);
    check_equal(record.id_slot, INT64_C(7));
    check_true(record.score_slot == 3.5);
    check_equal(provider.assigns, 2u);

    check_equal(
        data_bind_message_plan_validate_object(
            plan, &object, NULL, &validation),
        DATA_BIND_OK);

    check_equal(
        cserde_writer_init(&writer, &TOKEN_WRITER_OPS, &sink), CSERDE_OK);
    check_equal(
        data_bind_message_plan_encode_object(
            plan, &options, &object, NULL, &writer, &diagnostic),
        DATA_BIND_OK);
    check_equal(sink.count, 6u);
    check_equal(sink.tokens[0].kind, CSERDE_MAP_BEGIN);
    check_equal(sink.tokens[1].kind, CSERDE_STRING);
    check_equal(sink.tokens[2].kind, CSERDE_SINT);
    check_equal(sink.tokens[2].value.sint, INT64_C(7));
    check_equal(sink.tokens[3].kind, CSERDE_STRING);
    check_equal(sink.tokens[4].kind, CSERDE_FLOAT);
    check_true(sink.tokens[4].value.floating == 3.5);
    check_equal(sink.tokens[5].kind, CSERDE_MAP_END);
    check_equal(provider.reads, 3u);

    cmeta_object_release(&object);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("runs compiled validation against provider-backed field values") {
    DataBind *codec = make_codec();
    DataBindMessagePlan *plan = make_plan(codec);
    DynamicRecord record = {.marker = 9, .id_slot = 0, .score_slot = 1.0};
    DynamicProvider provider = {0};
    cmeta_object_field_provider field_provider;
    cmeta_object_ref object = make_object(&record, &provider, &field_provider);
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        data_bind_message_plan_validate_object(
            plan, &object, NULL, &error),
        DATA_BIND_ERR_VALIDATION);
    check_contains(error.path, "id");

    cmeta_object_release(&object);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("initializes managed field staging before decode even with poisoned workspace") {
    static const char schema[] =
        "schema TextDynamicObject [version(1)];"
        "message TextDynamic { string name; }";
    DataBind *codec = NULL;
    DataBindError codec_error = DATA_BIND_ERROR_INIT;
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    TextDynamicRecord record = {.marker = 31, .name_slot = NULL};
    TextDynamicProvider provider = {0};
    cmeta_object_field_provider field_provider = {
        .size = sizeof(cmeta_object_field_provider),
        .data = &TEXT_DYNAMIC_DATA,
        .context = &provider,
        .assign = text_dynamic_assign,
        .read = text_dynamic_read
    };
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    unsigned char workspace[2048];
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    const cserde_token input[] = {
        {.kind = CSERDE_MAP_BEGIN},
        key_token("name"),
        key_token("Ada"),
        {.kind = CSERDE_MAP_END}
    };
    TokenReader source = {input, sizeof(input) / sizeof(input[0]), 0u};
    cserde_reader reader = {0};

    memset(workspace, 0xA5, sizeof(workspace));
    check_equal(
        cmeta_data_value_init_zero(
            &salts_tstr_cmeta_data, &record.name_slot),
        CMETA_OK);
    check_equal(
        data_bind_create_from_text(
            schema, sizeof(schema) - 1u, &codec, &codec_error),
        DATA_BIND_OK);
    check_equal(
        data_bind_message_plan_compile_object(
            codec, "TextDynamic", &TEXT_DYNAMIC_DATA, &plan, &diagnostic),
        DATA_BIND_OK);
    check_equal(
        cmeta_object_borrow_with_providers(
            &object, &record, &TEXT_DYNAMIC_DATA, &field_provider, NULL),
        CMETA_OK);

    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    options.max_depth = 8u;
    options.max_items = 32u;
    options.max_owned_bytes = 1024u;

    check_equal(
        cserde_reader_init(&reader, &TOKEN_READER_OPS, &source), CSERDE_OK);
    check_equal(
        data_bind_message_plan_decode_object(
            plan, &options, &reader, &object, NULL, &diagnostic),
        DATA_BIND_OK);
    check_equal(record.marker, 31);
    check_not_null(record.name_slot);
    check_equal(tstr_len(record.name_slot), 3u);
    check_true(memcmp(record.name_slot, "Ada", 3u) == 0);
    check_equal(provider.assigns, 1u);

    cmeta_object_release(&object);
    check_equal(
        cmeta_data_value_restore_zero(
            &salts_tstr_cmeta_data, &record.name_slot),
        CMETA_OK);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("fails closed when the CMeta field provider rejects assignment") {
    DataBind *codec = make_codec();
    DataBindMessagePlan *plan = make_plan(codec);
    DynamicRecord record = {.marker = 5, .id_slot = 2, .score_slot = 4.0};
    DynamicProvider provider = {.reject_assign = 1};
    cmeta_object_field_provider field_provider;
    cmeta_object_ref object = make_object(&record, &provider, &field_provider);
    unsigned char workspace[1024] = {0};
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    const cserde_token input[] = {
        {.kind = CSERDE_MAP_BEGIN},
        key_token("id"),
        {.kind = CSERDE_SINT, .value.sint = 8},
        key_token("score"),
        {.kind = CSERDE_FLOAT, .value.floating = 2.0},
        {.kind = CSERDE_MAP_END}
    };
    TokenReader source = {input, sizeof(input) / sizeof(input[0]), 0u};
    cserde_reader reader = {0};

    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    options.max_depth = 8u;
    options.max_items = 32u;
    options.max_owned_bytes = 1024u;

    check_equal(
        cserde_reader_init(&reader, &TOKEN_READER_OPS, &source), CSERDE_OK);
    check_equal(
        data_bind_message_plan_decode_object(
            plan, &options, &reader, &object, NULL, &diagnostic),
        DATA_BIND_ERR_RUNTIME);
    check_equal(record.marker, 5);
    check_equal(record.id_slot, INT64_C(2));
    check_true(record.score_slot == 4.0);
    check_equal(provider.assigns, 0u);

    cmeta_object_release(&object);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }
}
