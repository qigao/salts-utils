#include "data_bind_binding_plan.h"

#include <cmeta/interface.h>
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProbeBuffer {
  unsigned char *data;
  size_t size;
} ProbeBuffer;

typedef union ProbeErrorPayload {
  ProbeBuffer text;
  ProbeBuffer bytes;
} ProbeErrorPayload;

typedef struct ProbeErrorEnvelope {
  uint32_t kind;
  ProbeErrorPayload payload;
} ProbeErrorEnvelope;

static size_t text_restore_calls;
static size_t bytes_restore_calls;

static bool probe_buffer_is_zero(const void *object) {
  const ProbeBuffer *value = (const ProbeBuffer *)object;
  return value != NULL && value->data == NULL && value->size == 0u;
}

static cmeta_status probe_buffer_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  ProbeBuffer *value = (ProbeBuffer *)object;
  unsigned char *copy = NULL;
  if (value == NULL || (size != 0u && data == NULL))
    return CMETA_INVALID_ARGUMENT;
  if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
  if (size != 0u) {
    copy = (unsigned char *)malloc(size);
    if (copy == NULL) return CMETA_OUT_OF_MEMORY;
    memcpy(copy, data, size);
  }
  free(value->data);
  value->data = copy;
  value->size = size;
  return CMETA_OK;
}

static cmeta_status probe_buffer_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  const ProbeBuffer *value = (const ProbeBuffer *)object;
  if (value == NULL || out_data == NULL || out_size == NULL)
    return CMETA_INVALID_ARGUMENT;
  *out_data = value->data;
  *out_size = value->size;
  return CMETA_OK;
}

static cmeta_status probe_buffer_init_zero(void *object) {
  ProbeBuffer *value = (ProbeBuffer *)object;
  if (value == NULL) return CMETA_INVALID_ARGUMENT;
  value->data = NULL;
  value->size = 0u;
  return CMETA_OK;
}

static void probe_text_restore_zero(void *object) {
  ProbeBuffer *value = (ProbeBuffer *)object;
  if (value == NULL) return;
  free(value->data);
  value->data = NULL;
  value->size = 0u;
  ++text_restore_calls;
}

static void probe_bytes_restore_zero(void *object) {
  ProbeBuffer *value = (ProbeBuffer *)object;
  if (value == NULL) return;
  free(value->data);
  value->data = NULL;
  value->size = 0u;
  ++bytes_restore_calls;
}

static void probe_buffer_move(void *destination, void *source) {
  ProbeBuffer *to = (ProbeBuffer *)destination;
  ProbeBuffer *from = (ProbeBuffer *)source;
  if (to == NULL || from == NULL) return;
  *to = *from;
  from->data = NULL;
  from->size = 0u;
}

static const cmeta_type_identity PROBE_BUFFER_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.databind.ServiceErrorBuffer");
static const cmeta_type_desc PROBE_BUFFER_TYPE = {
    .name = "ProbeBuffer",
    .size = sizeof(ProbeBuffer),
    .align = _Alignof(ProbeBuffer),
    .kind = CMETA_T_OBJECT,
    .identity = &PROBE_BUFFER_ID};
static const cmeta_data_buffer_shape PROBE_BUFFER_SHAPE = {
    .ownership = CMETA_DATA_BUFFER_OWNED};

static const cmeta_data_buffer_ops PROBE_TEXT_OPS = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &PROBE_BUFFER_TYPE,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = probe_buffer_is_zero,
    .assign = probe_buffer_assign,
    .restore_zero = probe_text_restore_zero,
    .read = probe_buffer_read,
    .init_zero = probe_buffer_init_zero,
    .move = probe_buffer_move};

static const cmeta_data_buffer_ops PROBE_BYTES_OPS = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &PROBE_BUFFER_TYPE,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = probe_buffer_is_zero,
    .assign = probe_buffer_assign,
    .restore_zero = probe_bytes_restore_zero,
    .read = probe_buffer_read,
    .init_zero = probe_buffer_init_zero,
    .move = probe_buffer_move};

static const cmeta_data_desc PROBE_TEXT_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.databind.ServiceTextError.data",
    .display_name = "Service text error",
    .kind = CMETA_DATA_STRING,
    .storage_type = &PROBE_BUFFER_TYPE,
    .shape = &PROBE_BUFFER_SHAPE,
    .buffer_ops = &PROBE_TEXT_OPS};

static const cmeta_data_desc PROBE_BYTES_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.databind.ServiceBytesError.data",
    .display_name = "Service bytes error",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &PROBE_BUFFER_TYPE,
    .shape = &PROBE_BUFFER_SHAPE,
    .buffer_ops = &PROBE_BYTES_OPS};

static DataBindStatus resolve_text(
    const cmeta_data_desc **out, DataBindError *error) {
  (void)error;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out = &PROBE_TEXT_DATA;
  return DATA_BIND_OK;
}

static DataBindStatus resolve_bytes(
    const cmeta_data_desc **out, DataBindError *error) {
  (void)error;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out = &PROBE_BYTES_DATA;
  return DATA_BIND_OK;
}

static const DataBindNativeErrorBinding ERROR_BINDINGS[] = {
    {sizeof(DataBindNativeErrorBinding), "TextError", 1u, resolve_text,
     offsetof(ProbeErrorEnvelope, payload.text)},
    {sizeof(DataBindNativeErrorBinding), "BytesError", 2u, resolve_bytes,
     offsetof(ProbeErrorEnvelope, payload.bytes)}};

static DataBindServiceNativeBinding probe_binding(void) {
  DataBindServiceNativeBinding binding =
      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(NULL, NULL, NULL);
  binding.errors = ERROR_BINDINGS;
  binding.error_count = 2u;
  binding.error_param_index = 0u;
  binding.error_envelope_bytes = sizeof(ProbeErrorEnvelope);
  binding.error_kind_offset = offsetof(ProbeErrorEnvelope, kind);
  binding.error_kind_bytes = sizeof(uint32_t);
  return binding;
}

static int probe_buffer_seed(
    ProbeBuffer *value, const unsigned char *data, size_t size) {
  if (value == NULL || (size != 0u && data == NULL)) return 0;
  value->data = size != 0u ? (unsigned char *)malloc(size) : NULL;
  if (size != 0u && value->data == NULL) return 0;
  if (size != 0u) memcpy(value->data, data, size);
  value->size = size;
  return 1;
}

static int envelope_is_zero(const ProbeErrorEnvelope *value) {
  const unsigned char *bytes = (const unsigned char *)value;
  size_t i;
  for (i = 0u; i < sizeof(*value); ++i)
    if (bytes[i] != 0u) return 0;
  return 1;
}

spec("Service native error-envelope cleanup") {
  it("releases owned string and bytes payloads exactly once") {
    static const unsigned char text[] = {'o', 'o', 'p', 's'};
    static const unsigned char bytes[] = {0u, 0x7fu, 0xffu};
    DataBindServiceNativeBinding binding = probe_binding();
    DataBindError error = DATA_BIND_ERROR_INIT;
    ProbeErrorEnvelope envelope = {0};

    text_restore_calls = 0u;
    bytes_restore_calls = 0u;
    envelope.kind = 1u;
    check_true(probe_buffer_seed(
        &envelope.payload.text, text, sizeof(text)));
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_OK);
    check_equal(text_restore_calls, (size_t)1u);
    check_equal(bytes_restore_calls, (size_t)0u);
    check_true(envelope_is_zero(&envelope));

    /* Repeated cleanup observes canonical NONE and never double-destroys. */
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_OK);
    check_equal(text_restore_calls, (size_t)1u);

    envelope.kind = 2u;
    check_true(probe_buffer_seed(
        &envelope.payload.bytes, bytes, sizeof(bytes)));
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_OK);
    check_equal(bytes_restore_calls, (size_t)1u);
    check_true(envelope_is_zero(&envelope));

    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_OK);
    check_equal(bytes_restore_calls, (size_t)1u);
  }

  it("treats non-throws Services and NONE as idempotent no-ops") {
    DataBindServiceNativeBinding no_errors =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(NULL, NULL, NULL);
    DataBindServiceNativeBinding binding = probe_binding();
    ProbeErrorEnvelope envelope = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        data_bind_service_native_error_restore_zero(
            &no_errors, NULL, 0u, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_OK);
  }

  it("rejects unknown kind without guessing a payload type") {
    static const unsigned char text[] = {'x'};
    DataBindServiceNativeBinding binding = probe_binding();
    ProbeErrorEnvelope envelope = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    unsigned char *original;

    text_restore_calls = 0u;
    envelope.kind = 99u;
    check_true(probe_buffer_seed(
        &envelope.payload.text, text, sizeof(text)));
    original = envelope.payload.text.data;

    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(envelope.kind, (uint32_t)99u);
    check_true(envelope.payload.text.data == original);
    check_equal(text_restore_calls, (size_t)0u);

    free(envelope.payload.text.data);
    envelope.payload.text.data = NULL;
    envelope.payload.text.size = 0u;
  }

  it("rejects malformed bounds before mutating active storage") {
    static const unsigned char text[] = {'x', 'y'};
    DataBindNativeErrorBinding malformed_errors[2] = {
        ERROR_BINDINGS[0], ERROR_BINDINGS[1]};
    DataBindServiceNativeBinding binding = probe_binding();
    ProbeErrorEnvelope envelope = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    unsigned char *original;

    text_restore_calls = 0u;
    envelope.kind = 1u;
    check_true(probe_buffer_seed(
        &envelope.payload.text, text, sizeof(text)));
    original = envelope.payload.text.data;

    malformed_errors[1].payload_offset = sizeof(envelope) - 1u;
    binding.errors = malformed_errors;
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_ERR_SCHEMA);
    check_true(envelope.payload.text.data == original);
    check_equal(text_restore_calls, (size_t)0u);

    binding = probe_binding();
    binding.error_kind_offset = sizeof(envelope);
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope), &error),
        DATA_BIND_ERR_SCHEMA);
    check_true(envelope.payload.text.data == original);
    check_equal(text_restore_calls, (size_t)0u);

    binding = probe_binding();
    check_equal(
        data_bind_service_native_error_restore_zero(
            &binding, &envelope, sizeof(envelope) - 1u, &error),
        DATA_BIND_ERR_BUFFER_TOO_SMALL);
    check_true(envelope.payload.text.data == original);
    check_equal(text_restore_calls, (size_t)0u);

    free(envelope.payload.text.data);
    envelope.payload.text.data = NULL;
    envelope.payload.text.size = 0u;
  }
}
