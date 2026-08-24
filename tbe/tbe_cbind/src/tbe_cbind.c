#include "tbe_cbind/tbe_cbind.h"
#include "tbe_cbind_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  TBE_CBIND_DEFAULT_SCHEMA_BYTES = 1024u * 1024u,
  TBE_CBIND_DEFAULT_TYPES = 1024u,
  TBE_CBIND_DEFAULT_FIELDS = 65536u,
  TBE_CBIND_DEFAULT_DEPTH = 64u,
  TBE_CBIND_DEFAULT_NAME_BYTES = 255u,
  TBE_CBIND_DEFAULT_PLAN_BYTES = 16u * 1024u * 1024u
};

static void *tbe_cbind_normal_calloc(void *context, size_t count, size_t size) {
  (void)context;
  return calloc(count, size);
}

static void tbe_cbind_normal_free(void *context, void *pointer) {
  (void)context;
  free(pointer);
}

int tbe_cbind_size_add(size_t left, size_t right, size_t *out) {
  if (out == NULL || left > SIZE_MAX - right) return 0;
  *out = left + right;
  return 1;
}

int tbe_cbind_size_mul(size_t left, size_t right, size_t *out) {
  if (out == NULL || (right != 0u && left > SIZE_MAX / right)) return 0;
  *out = left * right;
  return 1;
}

void *tbe_cbind_alloc_array(tbe_cbind_build_context *context, size_t count,
                            size_t size) {
  size_t bytes;
  if (context == NULL || !tbe_cbind_size_mul(count, size, &bytes)) return NULL;
  if (bytes == 0u) return NULL;
  return context->allocator.calloc_fn(context->allocator.context, count, size);
}

void *tbe_cbind_plan_alloc_array(tbe_cbind_build_context *context,
                                 size_t count, size_t size) {
  size_t bytes;
  size_t total;
  if (context == NULL || !tbe_cbind_size_mul(count, size, &bytes) ||
      !tbe_cbind_size_add(context->plan_bytes, bytes, &total) ||
      total > context->options->max_plan_bytes) {
    if (context != NULL) context->plan_limit_hit = 1;
    return NULL;
  }
  if (bytes == 0u) return NULL;
  {
    void *pointer = context->allocator.calloc_fn(
        context->allocator.context, count, size);
    if (pointer != NULL) context->plan_bytes = total;
    return pointer;
  }
}

void tbe_cbind_free(tbe_cbind_build_context *context, void *pointer) {
  if (context != NULL && pointer != NULL)
    context->allocator.free_fn(context->allocator.context, pointer);
}

char *tbe_cbind_strdup(tbe_cbind_build_context *context, const char *text) {
  size_t size;
  char *copy;
  if (text == NULL || !tbe_cbind_size_add(strlen(text), 1u, &size)) return NULL;
  copy = tbe_cbind_alloc_array(context, size, sizeof(*copy));
  if (copy != NULL) memcpy(copy, text, size);
  return copy;
}

static int tbe_cbind_error_valid(const tbe_cbind_plan_error *error) {
  return error == NULL ||
         (error->struct_size >= sizeof(*error) &&
          error->abi_version == TBE_CBIND_ERROR_ABI_VERSION);
}

tbe_cbind_status tbe_cbind_set_error(
    tbe_cbind_build_context *context, tbe_cbind_status status,
    tbe_cbind_error_phase phase, size_t field_index, cmeta_status target_status,
    const char *path, const char *message) {
  tbe_cbind_plan_error *error = context != NULL ? context->error : NULL;
  if (error != NULL) {
    error->status = status;
    error->phase = phase;
    error->line = -1;
    error->column = -1;
    error->target_status = target_status;
    error->field_index = field_index;
    (void)snprintf(error->path, sizeof(error->path), "%s",
                   path != NULL ? path : "");
    (void)snprintf(error->message, sizeof(error->message), "%s",
                   message != NULL ? message : "");
  }
  return status;
}

static int tbe_cbind_options_valid(const tbe_cbind_plan_options *options) {
  return options != NULL && options->struct_size >= sizeof(*options) &&
         options->abi_version == TBE_CBIND_OPTIONS_ABI_VERSION &&
         options->max_schema_bytes != 0u && options->max_types != 0u &&
         options->max_fields != 0u && options->max_depth != 0u &&
         options->max_name_bytes != 0u && options->max_plan_bytes != 0u;
}

void tbe_cbind_plan_options_init(tbe_cbind_plan_options *options) {
  if (options == NULL) return;
  *options = (tbe_cbind_plan_options){
      sizeof(*options), TBE_CBIND_OPTIONS_ABI_VERSION,
      TBE_CBIND_DEFAULT_SCHEMA_BYTES, TBE_CBIND_DEFAULT_TYPES,
      TBE_CBIND_DEFAULT_FIELDS, TBE_CBIND_DEFAULT_DEPTH,
      TBE_CBIND_DEFAULT_NAME_BYTES, TBE_CBIND_DEFAULT_PLAN_BYTES};
}

void tbe_cbind_plan_error_init(tbe_cbind_plan_error *error) {
  if (error == NULL) return;
  *error = (tbe_cbind_plan_error){0};
  error->struct_size = sizeof(*error);
  error->abi_version = TBE_CBIND_ERROR_ABI_VERSION;
  error->status = TBE_CBIND_OK;
  error->phase = TBE_CBIND_PHASE_NONE;
  error->line = -1;
  error->column = -1;
  error->target_status = CMETA_OK;
}

tbe_cbind_status tbe_cbind_plan_create_from_text_with_allocator(
    const char *schema_text, size_t schema_size, const char *type_name,
    size_t type_name_size, const cmeta_data_desc *native_shape,
    const tbe_cbind_plan_options *options, tbe_cbind_plan **out,
    tbe_cbind_plan_error *error, const tbe_cbind_allocator *allocator) {
  tbe_cbind_build_context context;
  tbe_cbind_schema_model *model = NULL;
  char *schema_copy = NULL;
  char *type_copy = NULL;
  size_t schema_copy_size;
  size_t type_copy_size;
  tbe_cbind_status status;
  if (out != NULL) *out = NULL;
  if (!tbe_cbind_error_valid(error)) return TBE_CBIND_INVALID_ARGUMENT;
  context = (tbe_cbind_build_context){0};
  context.options = options;
  context.error = error;
  if (allocator != NULL) context.allocator = *allocator;
  if (error != NULL) tbe_cbind_plan_error_init(error);
  if (out == NULL || schema_text == NULL || schema_size == 0u ||
      type_name == NULL || type_name_size == 0u || native_shape == NULL) {
    return tbe_cbind_set_error(&context, TBE_CBIND_INVALID_ARGUMENT,
                               TBE_CBIND_PHASE_NONE, 0u, CMETA_OK, NULL,
                               "required factory argument is missing");
  }
  if (allocator == NULL || allocator->calloc_fn == NULL ||
      allocator->free_fn == NULL) {
    return tbe_cbind_set_error(&context, TBE_CBIND_INVALID_ARGUMENT,
                               TBE_CBIND_PHASE_NONE, 0u, CMETA_OK, NULL,
                               "allocator contract is invalid");
  }
  if (!tbe_cbind_options_valid(options)) {
    return tbe_cbind_set_error(&context, TBE_CBIND_INVALID_OPTIONS,
                               TBE_CBIND_PHASE_NONE, 0u, CMETA_OK, NULL,
                               "options ABI or resource limit is invalid");
  }
  if (schema_size == SIZE_MAX || type_name_size == SIZE_MAX ||
      schema_size > options->max_schema_bytes ||
      type_name_size > options->max_name_bytes) {
    return tbe_cbind_set_error(&context, TBE_CBIND_LIMIT_EXCEEDED,
                               TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK, NULL,
                               "input exceeds a configured byte limit");
  }
  if (memchr(schema_text, '\0', schema_size) != NULL ||
      memchr(type_name, '\0', type_name_size) != NULL) {
    return tbe_cbind_set_error(&context, TBE_CBIND_INVALID_ARGUMENT,
                               TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK, NULL,
                               "input slice contains an embedded NUL");
  }
  if (!tbe_cbind_size_add(schema_size, 1u, &schema_copy_size) ||
      !tbe_cbind_size_add(type_name_size, 1u, &type_copy_size)) {
    return tbe_cbind_set_error(&context, TBE_CBIND_LIMIT_EXCEEDED,
                               TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK, NULL,
                               "input slice size overflow");
  }
  schema_copy = tbe_cbind_alloc_array(&context, schema_copy_size,
                                      sizeof(*schema_copy));
  type_copy = tbe_cbind_alloc_array(&context, type_copy_size,
                                    sizeof(*type_copy));
  if (schema_copy == NULL || type_copy == NULL) {
    status = tbe_cbind_set_error(&context, TBE_CBIND_OUT_OF_MEMORY,
                                 TBE_CBIND_PHASE_SCHEMA, 0u,
                                 CMETA_OUT_OF_MEMORY, NULL,
                                 "input copy allocation failed");
    goto cleanup;
  }
  memcpy(schema_copy, schema_text, schema_size);
  schema_copy[schema_size] = '\0';
  memcpy(type_copy, type_name, type_name_size);
  type_copy[type_name_size] = '\0';
  status = tbe_cbind_schema_model_build(&context, schema_copy, schema_size,
                                        type_copy, &model);
  if (status == TBE_CBIND_OK) {
    status = tbe_cbind_plan_build(&context, model, native_shape, out);
  }
cleanup:
  tbe_cbind_schema_model_destroy(model);
  tbe_cbind_free(&context, type_copy);
  tbe_cbind_free(&context, schema_copy);
  return status;
}

tbe_cbind_status tbe_cbind_plan_create_from_text(
    const char *schema_text, size_t schema_size, const char *type_name,
    size_t type_name_size, const cmeta_data_desc *native_shape,
    const tbe_cbind_plan_options *options, tbe_cbind_plan **out,
    tbe_cbind_plan_error *error) {
  const tbe_cbind_allocator allocator = {
      NULL, tbe_cbind_normal_calloc, tbe_cbind_normal_free};
  return tbe_cbind_plan_create_from_text_with_allocator(
      schema_text, schema_size, type_name, type_name_size, native_shape,
      options, out, error, &allocator);
}

void tbe_cbind_plan_destroy(tbe_cbind_plan *plan) {
  tbe_cbind_plan_release(plan);
}

const cmeta_data_desc *tbe_cbind_plan_shape(const tbe_cbind_plan *plan) {
  return plan != NULL && plan->state == TBE_CBIND_PLAN_READY ? plan->shape
                                                             : NULL;
}

cbind_status tbe_cbind_plan_decode(const tbe_cbind_plan *plan,
                                   const cbind_context *context,
                                   cserde_reader *reader, void *out,
                                   cbind_error *error) {
  if (plan == NULL || plan->state != TBE_CBIND_PLAN_READY ||
      plan->shape == NULL)
    return CBIND_INVALID_ARGUMENT;
  return cbind_decode(context, plan->shape, reader, out, error);
}
