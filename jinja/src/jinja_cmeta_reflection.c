#include "jinja_cmeta_reflection.h"
#include "jinja_cmeta_internal.h"

#include <limits.h>
#include <float.h>
#include <math.h>
#include <string.h>

typedef union JINJA_NATIVE_VALUE {
  bool boolean;
  int integer;
  long wide_integer;
  float narrow_float;
  double floating;
} JINJA_NATIVE_VALUE;

typedef enum JINJA_NATIVE_KIND {
  JINJA_NATIVE_UNSUPPORTED, JINJA_NATIVE_VOID, JINJA_NATIVE_BOOL,
  JINJA_NATIVE_INT, JINJA_NATIVE_LONG, JINJA_NATIVE_DOUBLE, JINJA_NATIVE_FLOAT
} JINJA_NATIVE_KIND;

static JINJA_NATIVE_KIND jinja_native_kind(const cmeta_type_desc *type) {
  if (cmeta_type_equal(type, CMETA_TYPEOF(void))) return JINJA_NATIVE_VOID;
  if (cmeta_type_equal(type, CMETA_TYPEOF(bool))) return JINJA_NATIVE_BOOL;
  if (cmeta_type_equal(type, CMETA_TYPEOF(int))) return JINJA_NATIVE_INT;
  if (cmeta_type_equal(type, CMETA_TYPEOF(long))) return JINJA_NATIVE_LONG;
  if (cmeta_type_equal(type, CMETA_TYPEOF(double))) return JINJA_NATIVE_DOUBLE;
  if (cmeta_type_equal(type, CMETA_TYPEOF(float))) return JINJA_NATIVE_FLOAT;
  return JINJA_NATIVE_UNSUPPORTED;
}

static JINJA_CMETA_STATUS jinja_native_argument(const JINJA_CMETA_CALL_ARGUMENT *input,
    const cmeta_type_desc *type, JINJA_NATIVE_VALUE *storage, const void **out) {
  if (input->descriptor != NULL && input->object != NULL &&
      cmeta_type_equal(type, input->descriptor->storage_type)) {
    *out = input->object;
    return JINJA_CMETA_OK;
  }
  const JINJA_CMETA_CALL_VALUE *value = &input->value;
  switch (jinja_native_kind(type)) {
  case JINJA_NATIVE_BOOL:
    if (value->kind != JINJA_CMETA_CALL_VALUE_BOOL) return JINJA_CMETA_ERR_RENDER;
    storage->boolean = value->boolean;
    *out = &storage->boolean;
    break;
  case JINJA_NATIVE_INT:
    if (value->kind != JINJA_CMETA_CALL_VALUE_INTEGER) return JINJA_CMETA_ERR_RENDER;
    if (value->integer < INT_MIN || value->integer > INT_MAX) return JINJA_CMETA_ERR_CAPACITY;
    storage->integer = (int)value->integer;
    *out = &storage->integer;
    break;
  case JINJA_NATIVE_LONG:
    if (value->kind != JINJA_CMETA_CALL_VALUE_INTEGER) return JINJA_CMETA_ERR_RENDER;
    if (value->integer < LONG_MIN || value->integer > LONG_MAX) return JINJA_CMETA_ERR_CAPACITY;
    storage->wide_integer = (long)value->integer;
    *out = &storage->wide_integer;
    break;
  case JINJA_NATIVE_DOUBLE:
    if (value->kind != JINJA_CMETA_CALL_VALUE_FLOAT) return JINJA_CMETA_ERR_RENDER;
    storage->floating = value->floating;
    *out = &storage->floating;
    break;
  case JINJA_NATIVE_FLOAT:
    if (value->kind != JINJA_CMETA_CALL_VALUE_FLOAT) return JINJA_CMETA_ERR_RENDER;
    if (isfinite(value->floating) && (value->floating > FLT_MAX || value->floating < -FLT_MAX))
      return JINJA_CMETA_ERR_CAPACITY;
    storage->narrow_float = (float)value->floating;
    *out = &storage->narrow_float;
    break;
  default: return JINJA_CMETA_ERR_RENDER;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_native_invoke(void *opaque,
    const JINJA_CMETA_CALL_CONTEXT *context, JINJA_CMETA_CALL_RESULT *result) {
  const cmeta_invokable *function = (const cmeta_invokable *)opaque;
  const cmeta_function_desc *signature = function->function;
  const size_t count = signature->param_count;
  const void *arguments[JINJA_CMETA_MAX_REFLECTED_ARGUMENTS] = {0};
  JINJA_NATIVE_VALUE values[JINJA_CMETA_MAX_REFLECTED_ARGUMENTS], output = {0};
  if (context->argument_count != count || context->positional_count > count)
    return JINJA_CMETA_ERR_RENDER;
  for (size_t i = 0u; i < count; ++i) {
    size_t position = i;
    const JINJA_CMETA_CALL_ARGUMENT *argument = &context->arguments[i];
    if (i >= context->positional_count) {
      for (position = 0u; position < count; ++position)
        if (vstr_eq(argument->name, vstr_from_cstr(signature->params[position].name))) break;
    }
    if (position >= count || arguments[position] != NULL) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_STATUS status = jinja_native_argument(argument,
        signature->params[position].type, &values[position], &arguments[position]);
    if (status != JINJA_CMETA_OK) return status;
  }
  cmeta_status called = cmeta_invokable_invoke_admitted(function, &output, arguments);
  if (called != CMETA_OK) {
    if (called == CMETA_OUT_OF_MEMORY) return JINJA_CMETA_ERR_OUT_OF_MEMORY;
    if (called == CMETA_CAPACITY_EXCEEDED) return JINJA_CMETA_ERR_CAPACITY;
    return JINJA_CMETA_ERR_RENDER;
  }
  switch (jinja_native_kind(signature->return_type)) {
  case JINJA_NATIVE_VOID:
    result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_NONE}; break;
  case JINJA_NATIVE_BOOL:
    result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_BOOL,
        .boolean = output.boolean}; break;
  case JINJA_NATIVE_INT:
    result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_INTEGER,
        .integer = output.integer}; break;
  case JINJA_NATIVE_LONG:
    result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_INTEGER,
        .integer = output.wide_integer}; break;
  case JINJA_NATIVE_DOUBLE:
    result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_FLOAT,
        .floating = output.floating}; break;
  case JINJA_NATIVE_FLOAT:
    result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_FLOAT,
        .floating = output.narrow_float}; break;
  default: return JINJA_CMETA_ERR_METADATA;
  }
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_callable_from_invokable(JINJA_CMETA_CALLABLE *out,
    vstr name, const cmeta_invokable *function, JINJA_CMETA_ERROR *error) {
  jinja_cmeta_error_clear(error);
  if (out != NULL) memset(out, 0, sizeof(*out));
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  if (out == NULL || !vstr_is_valid(name) || name.len == 0u ||
      name.len > JINJA_CMETA_MAX_TEMPLATE_BYTES || vstr_utf8_invalid_offset(name) != VSTR_NPOS ||
      !cmeta_invokable_valid(function)) status = JINJA_CMETA_ERR_INVALID_ARGUMENT;
  else if (function->function->param_count > JINJA_CMETA_MAX_REFLECTED_ARGUMENTS)
    status = JINJA_CMETA_ERR_CAPACITY;
  else {
    const cmeta_function_desc *signature = function->function;
    if ((signature->effects & CMETA_EFFECT_ASYNC) != 0u ||
        jinja_native_kind(signature->return_type) == JINJA_NATIVE_UNSUPPORTED ||
        (signature->result_flags & CMETA_RESULT_CLASS_MASK) != CMETA_RESULT_VALUE)
      status = JINJA_CMETA_ERR_UNSUPPORTED;
    for (size_t i = 0u; status == JINJA_CMETA_OK && i < signature->param_count; ++i) {
      const cmeta_param_desc *param = &signature->params[i];
      if (jinja_native_kind(param->type) == JINJA_NATIVE_UNSUPPORTED ||
          jinja_native_kind(param->type) == JINJA_NATIVE_VOID ||
          (param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_IN ||
          (param->flags & (CMETA_PARAM_OWNED | CMETA_PARAM_RECEIVER)) != 0u)
        status = JINJA_CMETA_ERR_UNSUPPORTED;
      else if (param->name == NULL || param->name[0] == '\0') status = JINJA_CMETA_ERR_INVALID_ARGUMENT;
      if (status != JINJA_CMETA_OK) break;
      for (size_t j = 0u; j < i; ++j)
        if (strcmp(param->name, signature->params[j].name) == 0) status = JINJA_CMETA_ERR_INVALID_ARGUMENT;
    }
  }
  if (status != JINJA_CMETA_OK) {
    jinja_cmeta_error_set(error, status, 0u, "unsupported or invalid reflected callable");
    return status;
  }
  *out = (JINJA_CMETA_CALLABLE){.name = name,
      .max_positional = function->function->param_count,
      .max_keywords = function->function->param_count,
      .invoke = jinja_native_invoke, .userdata = (void *)function};
  return JINJA_CMETA_OK;
}
