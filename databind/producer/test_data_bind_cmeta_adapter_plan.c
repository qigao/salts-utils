#include "data_bind_cmeta_adapter_plan.h"

#include <stdio.h>
#include <string.h>

typedef struct test_buffer {
  char data[16384];
  size_t size;
} test_buffer;

typedef struct test_manifest {
  const cmeta_function_desc *const *functions;
  size_t count;
} test_manifest;

static bool test_write(void *context, const char *data, size_t size) {
  test_buffer *buffer = (test_buffer *)context;
  if (buffer == NULL || data == NULL ||
      size > sizeof(buffer->data) - buffer->size - 1u)
    return false;
  memcpy(buffer->data + buffer->size, data, size);
  buffer->size += size;
  buffer->data[buffer->size] = '\0';
  return true;
}

static const cmeta_function_desc *test_at(void *context, size_t index) {
  const test_manifest *manifest = (const test_manifest *)context;
  if (manifest == NULL || index >= manifest->count) return NULL;
  return manifest->functions[index];
}

static int expect_status(
    DataBindCMetaAdapterPlanStatus actual,
    DataBindCMetaAdapterPlanStatus expected,
    const char *case_name) {
  if (actual == expected) return 0;
  fprintf(stderr, "%s: expected %s, got %s\n",
          case_name,
          data_bind_cmeta_adapter_plan_status_name(expected),
          data_bind_cmeta_adapter_plan_status_name(actual));
  return 1;
}

int main(void) {
  static const cmeta_param_desc read_params[] = {
      {sizeof(cmeta_param_desc), "fd", &cmeta_type_uint32, CMETA_PARAM_IN},
      {sizeof(cmeta_param_desc), "offset", &cmeta_type_uint64, CMETA_PARAM_IN}};
  static const cmeta_param_desc close_params[] = {
      {sizeof(cmeta_param_desc), "fd", &cmeta_type_uint32, CMETA_PARAM_IN}};
  static const cmeta_function_desc read_function = {
      sizeof(cmeta_function_desc), "wasi.fd_read", &cmeta_type_uint32,
      read_params, 2u, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE};
  static const cmeta_function_desc close_function = {
      sizeof(cmeta_function_desc), "wasi.fd_close", &cmeta_type_void,
      close_params, 1u, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE};
  static const cmeta_param_desc unsupported_params[] = {
      {sizeof(cmeta_param_desc), "signed_value", &cmeta_type_int32,
       CMETA_PARAM_IN}};
  static const cmeta_function_desc unsupported_function = {
      sizeof(cmeta_function_desc), "unsupported", &cmeta_type_uint32,
      unsupported_params, 1u, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE};
  static const cmeta_function_desc invalid_function = {
      0u, "invalid", &cmeta_type_uint32, NULL, 0u,
      CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE};

  const cmeta_function_desc *functions[] = {
      &read_function, &close_function};
  const cmeta_function_desc *duplicate_functions[] = {
      &read_function, &read_function};
  const cmeta_function_desc *unsupported_functions[] = {
      &unsupported_function};
  const cmeta_function_desc *invalid_functions[] = {
      &invalid_function};
  test_manifest source = {functions, 2u};
  test_manifest duplicate_source = {duplicate_functions, 2u};
  test_manifest unsupported_source = {unsupported_functions, 1u};
  test_manifest invalid_source = {invalid_functions, 1u};
  DataBindCMetaFunctionManifest manifest = {2u, test_at, &source};
  DataBindCMetaAdapterPlanConfig config = {"preview1"};
  test_buffer first = {{0}, 0u};
  test_buffer second = {{0}, 0u};
  size_t error_index = SIZE_MAX;
  int failures = 0;

  failures += expect_status(
      data_bind_cmeta_adapter_plan_emit(
          &manifest, &config, test_write, &first, &error_index),
      DATA_BIND_CMETA_ADAPTER_PLAN_OK, "canonical manifest");
  if (error_index != SIZE_MAX) {
    fprintf(stderr, "success unexpectedly reported error index %zu\n",
            error_index);
    ++failures;
  }

  failures += expect_status(
      data_bind_cmeta_adapter_plan_emit(
          &manifest, &config, test_write, &second, NULL),
      DATA_BIND_CMETA_ADAPTER_PLAN_OK, "deterministic regeneration");
  if (first.size != second.size ||
      memcmp(first.data, second.data, first.size) != 0) {
    fprintf(stderr, "identical input produced different output\n");
    ++failures;
  }

  if (strstr(first.data, "preview1_adapter_carrier_u32") == NULL ||
      strstr(first.data, "preview1_adapter_carrier_u64") == NULL ||
      strstr(first.data, "\"wasi.fd_read\"") == NULL ||
      strstr(first.data, "source_ordinal") == NULL ||
      strstr(first.data, "cmeta_function_desc") != NULL ||
      strstr(first.data, "#include <data_bind") != NULL) {
    fprintf(stderr, "generated plan did not preserve the producer boundary\n");
    ++failures;
  }

  first.size = 0u;
  first.data[0] = '\0';
  manifest.count = duplicate_source.count;
  manifest.context = &duplicate_source;
  error_index = SIZE_MAX;
  failures += expect_status(
      data_bind_cmeta_adapter_plan_emit(
          &manifest, &config, test_write, &first, &error_index),
      DATA_BIND_CMETA_ADAPTER_PLAN_DUPLICATE_FUNCTION, "duplicate name");
  if (error_index != 1u) {
    fprintf(stderr, "duplicate error index mismatch: %zu\n", error_index);
    ++failures;
  }

  manifest.count = unsupported_source.count;
  manifest.context = &unsupported_source;
  error_index = SIZE_MAX;
  failures += expect_status(
      data_bind_cmeta_adapter_plan_emit(
          &manifest, &config, test_write, &first, &error_index),
      DATA_BIND_CMETA_ADAPTER_PLAN_UNSUPPORTED_TYPE, "unsupported carrier");
  if (error_index != 0u) {
    fprintf(stderr, "unsupported error index mismatch: %zu\n", error_index);
    ++failures;
  }

  manifest.count = invalid_source.count;
  manifest.context = &invalid_source;
  error_index = SIZE_MAX;
  failures += expect_status(
      data_bind_cmeta_adapter_plan_emit(
          &manifest, &config, test_write, &first, &error_index),
      DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_DESCRIPTOR, "invalid descriptor");
  if (error_index != 0u) {
    fprintf(stderr, "invalid descriptor error index mismatch: %zu\n",
            error_index);
    ++failures;
  }

  return failures == 0 ? 0 : 1;
}
