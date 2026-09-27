#include "data_bind_socket_execution_plan.h"
#include "data_bind_json_provider.h"

#include "tinytest.h"

#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct SocketEvent {
  uint32_t sequence;
  uint32_t sample;
  uint8_t presence;
} SocketEvent;

static const cmeta_type_identity SOCKET_EVENT_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.socket.SocketEvent");
static const cmeta_type_desc SOCKET_EVENT_TYPE = {
    "SocketEvent", sizeof(SocketEvent), _Alignof(SocketEvent),
    CMETA_T_OBJECT, NULL, NULL, &SOCKET_EVENT_ID};

static const cmeta_field_desc SOCKET_EVENT_LAYOUT_FIELDS[] = {
    {"sequence", "uint32_t", offsetof(SocketEvent, sequence),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"sample", "uint32_t", offsetof(SocketEvent, sample),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc SOCKET_EVENT_LAYOUT = {
    "SocketEvent", sizeof(SocketEvent), _Alignof(SocketEvent),
    SOCKET_EVENT_LAYOUT_FIELDS, 2u};
static const cmeta_data_field_desc SOCKET_EVENT_FIELDS[] = {
    {"test.socket.SocketEvent.sequence", "sequence",
     offsetof(SocketEvent, sequence), &cmeta_data_uint32},
    {"test.socket.SocketEvent.sample", "sample",
     offsetof(SocketEvent, sample), &cmeta_data_uint32}};
static const cmeta_data_struct_shape SOCKET_EVENT_SHAPE = {
    &SOCKET_EVENT_LAYOUT, SOCKET_EVENT_FIELDS, 2u};
static const cmeta_data_desc SOCKET_EVENT_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.socket.SocketEvent.data",
    .display_name = "Event",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &SOCKET_EVENT_TYPE,
    .shape = &SOCKET_EVENT_SHAPE};

static const DataBindNativeStateBinding SOCKET_EVENT_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "sample",
     offsetof(SocketEvent, presence), 0u}};

static DataBindStatus socket_event_binding(
    DataBindNativeTypeBinding *out, DataBindError *error) {
  (void)error;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out = (DataBindNativeTypeBinding){
      sizeof(DataBindNativeTypeBinding),
      DATA_BIND_NATIVE_BINDING_ABI_VERSION,
      "Event",
      &SOCKET_EVENT_DATA,
      SOCKET_EVENT_PRESENCE,
      1u,
      NULL,
      0u};
  return DATA_BIND_OK;
}

static DataBindStatus socket_wrong_binding(
    DataBindNativeTypeBinding *out, DataBindError *error) {
  DataBindStatus status = socket_event_binding(out, error);
  if (status == DATA_BIND_OK) out->idl_type_name = "Other";
  return status;
}

typedef struct SocketTokenReader {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} SocketTokenReader;

static cserde_status socket_token_next(void *context, cserde_token *out) {
  SocketTokenReader *reader = (SocketTokenReader *)context;
  if (reader == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (reader->index >= reader->count) return CSERDE_DONE;
  *out = reader->tokens[reader->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops SOCKET_TOKEN_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    socket_token_next};

static cserde_token socket_key(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static int socket_reader_init(
    cserde_reader *reader,
    SocketTokenReader *state,
    const cserde_token *tokens,
    size_t count) {
  if (reader == NULL || state == NULL || tokens == NULL) return 0;
  *reader = (cserde_reader){0};
  state->tokens = tokens;
  state->count = count;
  state->index = 0u;
  return cserde_reader_init(reader, &SOCKET_TOKEN_OPS, state) == CSERDE_OK;
}

static DataBindNativeOptions socket_native_options(
    unsigned char *workspace, size_t workspace_bytes) {
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  options.workspace = workspace;
  options.workspace_bytes = workspace_bytes;
  options.max_depth = 16u;
  options.max_items = 64u;
  options.max_owned_bytes = 1024u;
  return options;
}

typedef struct BinaryProviderOwner {
  cserde_reader reader;
  SocketTokenReader state;
  cserde_token tokens[4];
} BinaryProviderOwner;

static size_t BINARY_PROVIDER_CLOSE_CALLS = 0u;

static DataBindStatus binary_provider_error(
    DataBindError *error, DataBindStatus status, const char *message) {
  if (error != NULL) {
    *error = (DataBindError)DATA_BIND_ERROR_INIT;
    error->code = status;
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "");
  }
  return status;
}

static DataBindStatus binary_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  BinaryProviderOwner *owner;
  uint32_t sequence;
  (void)max_depth;

  if (out_reader == NULL || out_owner == NULL ||
      data == NULL || len != 1u)
    return binary_provider_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Test Binary provider requires exactly one payload byte");

  owner = (BinaryProviderOwner *)calloc(1u, sizeof(*owner));
  if (owner == NULL)
    return binary_provider_error(
        error, DATA_BIND_ERR_OOM,
        "Could not allocate test Binary provider owner");

  sequence = (uint8_t)data[0];
  owner->tokens[0] = (cserde_token){.kind = CSERDE_MAP_BEGIN};
  owner->tokens[1] = socket_key("sequence");
  owner->tokens[2] =
      (cserde_token){.kind = CSERDE_UINT, .value.uint = sequence};
  owner->tokens[3] = (cserde_token){.kind = CSERDE_MAP_END};

  owner->state.tokens = owner->tokens;
  owner->state.count = 4u;
  owner->state.index = 0u;
  if (cserde_reader_init(
          &owner->reader, &SOCKET_TOKEN_OPS, &owner->state) != CSERDE_OK) {
    free(owner);
    return binary_provider_error(
        error, DATA_BIND_ERR_RUNTIME,
        "Could not initialize test Binary provider reader");
  }

  *out_reader = &owner->reader;
  *out_owner = owner;
  if (error != NULL) *error = (DataBindError)DATA_BIND_ERROR_INIT;
  return DATA_BIND_OK;
}

static void binary_provider_close(cserde_reader *reader, void *owner) {
  (void)reader;
  if (owner != NULL) {
    ++BINARY_PROVIDER_CLOSE_CALLS;
    free(owner);
  }
}

static const DataBindFormatProvider TEST_BINARY_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_INIT(
        DATA_BIND_FORMAT_BINARY,
        binary_provider_open,
        binary_provider_close);

static DataBind *socket_codec(void) {
  static const char schema[] =
      "message Event {"
      " @Min(1) @Max(10) [name(seq), alias(oldSeq)] uint32 sequence;"
      " optional uint32 sample default 7;"
      "}"
      "channel Events: Event;";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(
      data_bind_create_from_text(
          schema, sizeof(schema) - 1u, &codec, &error),
      DATA_BIND_OK);
  return codec;
}

spec("DataBind SocketExecutionPlan") {
  it("decodes framed JSON payloads through compiled FormatPlan aliases") {
    DataBindSocketPlan socket = {
        sizeof(DataBindSocketPlan),
        DATA_BIND_SOCKET_PLAN_ABI_VERSION,
        "Events",
        "Event",
        DATA_BIND_FORMAT_JSON,
        DATA_BIND_SOCKET_MODE_STREAM,
        DATA_BIND_SOCKET_FRAMING_LENGTH32_BE,
        4096u,
        socket_event_binding};
    static const char payload[] = "{\"seq\":3}";
    DataBind *codec = socket_codec();
    DataBindSocketExecutionPlan *execution = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[2048] = {0};
    DataBindNativeOptions options =
        socket_native_options(workspace, sizeof(workspace));
    SocketEvent value = {0};

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_socket_execution_plan_compile(
            codec, &socket, &execution, &error),
        DATA_BIND_OK);
    check_not_null(execution);
    data_bind_free(codec);
    codec = NULL;

    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, data_bind_json_format_provider(),
            payload, sizeof(payload) - 1u, 16u,
            &options, &value, sizeof(value),
            &diagnostic, &error),
        DATA_BIND_OK);
    check_equal(value.sequence, (uint32_t)3u);
    check_equal(value.sample, (uint32_t)7u);
    check_equal(value.presence, (uint8_t)1u);

    data_bind_socket_execution_plan_free(execution);
  }

  it("uses an explicit Binary provider and closes its lease on all paths") {
    DataBindSocketPlan socket = {
        sizeof(DataBindSocketPlan),
        DATA_BIND_SOCKET_PLAN_ABI_VERSION,
        "Events",
        "Event",
        DATA_BIND_FORMAT_BINARY,
        DATA_BIND_SOCKET_MODE_DATAGRAM,
        DATA_BIND_SOCKET_FRAMING_NONE,
        4096u,
        socket_event_binding};
    DataBind *codec = socket_codec();
    DataBindSocketExecutionPlan *execution = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[2048] = {0};
    DataBindNativeOptions options =
        socket_native_options(workspace, sizeof(workspace));
    SocketEvent valid = {0};
    SocketEvent invalid = {0};
    const unsigned char valid_payload[] = {3u};
    const unsigned char invalid_payload[] = {0u};
    size_t close_before;

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_socket_execution_plan_compile(
            codec, &socket, &execution, &error),
        DATA_BIND_OK);
    check_not_null(execution);
    data_bind_free(codec);
    codec = NULL;

    close_before = BINARY_PROVIDER_CLOSE_CALLS;
    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, &TEST_BINARY_PROVIDER,
            valid_payload, sizeof(valid_payload), 16u,
            &options, &valid, sizeof(valid),
            &diagnostic, &error),
        DATA_BIND_OK);
    check_equal(valid.sequence, (uint32_t)3u);
    check_equal(valid.sample, (uint32_t)7u);
    check_equal(valid.presence, (uint8_t)1u);
    check_equal(BINARY_PROVIDER_CLOSE_CALLS, close_before + 1u);

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, &TEST_BINARY_PROVIDER,
            invalid_payload, sizeof(invalid_payload), 16u,
            &options, &invalid, sizeof(invalid),
            &diagnostic, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(invalid.sequence, (uint32_t)0u);
    check_equal(invalid.sample, (uint32_t)0u);
    check_equal(invalid.presence, (uint8_t)0u);
    check_equal(diagnostic.schema_field, "sequence");
    check_equal(BINARY_PROVIDER_CLOSE_CALLS, close_before + 2u);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, data_bind_json_format_provider(),
            valid_payload, sizeof(valid_payload), 16u,
            &options, &valid, sizeof(valid),
            &diagnostic, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_contains(error.message, "does not match");

    data_bind_socket_execution_plan_free(execution);
  }

  it("survives codec release and delegates lookup-free MessagePlan decode") {
    char channel[] = "Events";
    char message[] = "Event";
    DataBindSocketPlan socket = {
        sizeof(DataBindSocketPlan),
        DATA_BIND_SOCKET_PLAN_ABI_VERSION,
        channel,
        message,
        DATA_BIND_FORMAT_JSON,
        DATA_BIND_SOCKET_MODE_STREAM,
        DATA_BIND_SOCKET_FRAMING_LENGTH32_BE,
        4096u,
        socket_event_binding};
    DataBind *codec = socket_codec();
    DataBindSocketExecutionPlan *execution = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const DataBindSocketPlan *compiled_socket;
    const DataBindNativeTypeBinding *native;
    unsigned char workspace[2048] = {0};
    DataBindNativeOptions options =
        socket_native_options(workspace, sizeof(workspace));
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    SocketEvent value = {0};
    SocketEvent invalid = {0};
    SocketTokenReader source = {0};
    cserde_reader reader = {0};
    const cserde_token valid_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        socket_key("sequence"),
        {.kind = CSERDE_UINT, .value.uint = 3u},
        {.kind = CSERDE_MAP_END},
    };
    const cserde_token invalid_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        socket_key("sequence"),
        {.kind = CSERDE_UINT, .value.uint = 0u},
        {.kind = CSERDE_MAP_END},
    };

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_socket_execution_plan_compile(
            codec, &socket, &execution, &error),
        DATA_BIND_OK);
    check_not_null(execution);

    /* Control-plane source records and codec may disappear after compilation. */
    channel[0] = 'X';
    message[0] = 'X';
    data_bind_free(codec);
    codec = NULL;

    compiled_socket =
        data_bind_socket_execution_plan_socket(execution);
    native =
        data_bind_socket_execution_plan_native_binding(execution);
    check_not_null(compiled_socket);
    check_not_null(native);
    check_equal(compiled_socket->channel_name, "Events");
    check_equal(compiled_socket->message_type, "Event");
    check_equal(native->idl_type_name, "Event");
    check_true(native->data == &SOCKET_EVENT_DATA);

    check_true(socket_reader_init(
        &reader, &source, valid_tokens,
        sizeof(valid_tokens) / sizeof(valid_tokens[0])));
    check_equal(
        data_bind_socket_execution_plan_decode_native(
            execution, &options, &reader,
            &value, sizeof(value), &diagnostic),
        DATA_BIND_OK);
    check_equal(value.sequence, (uint32_t)3u);
    check_equal(value.sample, (uint32_t)7u);
    check_equal(value.presence, (uint8_t)1u);

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(socket_reader_init(
        &reader, &source, invalid_tokens,
        sizeof(invalid_tokens) / sizeof(invalid_tokens[0])));
    check_equal(
        data_bind_socket_execution_plan_decode_native(
            execution, &options, &reader,
            &invalid, sizeof(invalid), &diagnostic),
        DATA_BIND_ERR_VALIDATION);
    check_equal(invalid.sequence, (uint32_t)0u);
    check_equal(invalid.sample, (uint32_t)0u);
    check_equal(invalid.presence, (uint8_t)0u);
    check_equal(diagnostic.schema_field, "sequence");

    data_bind_socket_execution_plan_free(execution);
  }

  it("rejects a resolver whose native identity disagrees with the Channel") {
    DataBindSocketPlan socket = {
        sizeof(DataBindSocketPlan),
        DATA_BIND_SOCKET_PLAN_ABI_VERSION,
        "Events",
        "Event",
        DATA_BIND_FORMAT_BINARY,
        DATA_BIND_SOCKET_MODE_DATAGRAM,
        DATA_BIND_SOCKET_FRAMING_NONE,
        4096u,
        socket_wrong_binding};
    DataBind *codec = socket_codec();
    DataBindSocketExecutionPlan *execution = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_socket_execution_plan_compile(
            codec, &socket, &execution, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(execution);
    check_contains(error.message, "does not match");
    data_bind_free(codec);
  }
}
