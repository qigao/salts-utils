#include "data_bind_value_reader.h"
#include "data_bind_value_reader_internal.h"
#include "data_bind_value_internal.h"

#include <salts_cmeta_data.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct ValueReaderFrame {
  const DataBindValue *value;
  size_t index;
  size_t count;
  int value_pending;
} ValueReaderFrame;
typedef struct ValueReader {
  cserde_reader reader;
  const DataBindValue *root;
  DataBindValueReaderLimits limits;
  size_t depth;
  size_t items;
  size_t view_bytes;
  int typed_integers;
  ValueReaderFrame frames[];
} ValueReader;

static DataBindStatus reader_error(DataBindError *error, DataBindStatus status,
                                  const char *message) {
  if (error != NULL && error->size >= sizeof(error->size)) {
    if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
      error->code = status;
    if (error->size >= offsetof(DataBindError, line) + sizeof(error->line))
      error->line = -1;
    if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
      error->column = -1;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
      error->path[0] = '\0';
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
      snprintf(error->message, sizeof(error->message), "%s", message);
  }
  return status;
}
static cserde_status reader_slice(ValueReader *state, cserde_token *out,
                                  cserde_token_kind kind, const void *data,
                                  size_t size) {
  if (size != 0u && data == NULL) return CSERDE_SOURCE_ERROR;
  if (size > state->limits.max_view_bytes - state->view_bytes)
    return CSERDE_LIMIT_EXCEEDED;
  state->view_bytes += size;
  out->kind = kind;
  out->value.slice = (cserde_slice){data, size, CSERDE_VIEW_STABLE};
  return CSERDE_OK;
}

static cserde_status reader_integer(
    const ValueReader *state, const DataBindValue *value, cserde_token *out) {
  static const cmeta_data_desc *const types[] = {
      &cmeta_data_int8, &cmeta_data_int16, &cmeta_data_int32, &cmeta_data_int64,
      &cmeta_data_uint8, &cmeta_data_uint16, &cmeta_data_uint32, &cmeta_data_uint64};
  const int source_signed = value->kind != DATA_BIND_VALUE_UINT64;
  const int64_t signed_value = value->kind == DATA_BIND_VALUE_INT ? value->data.int_val :
                              (source_signed ? value->data.int64_val : 0);
  const uint64_t unsigned_value = source_signed ? (uint64_t)signed_value : value->data.uint64_val;
  size_t i;
  out->kind = source_signed ? CSERDE_SINT : CSERDE_UINT;
  if (source_signed) out->value.sint = signed_value;
  else out->value.uint = unsigned_value;
  if (!state->typed_integers) return CSERDE_OK;
  if (value->type_identity == NULL) return CSERDE_SOURCE_ERROR;
  for (i = 0u; i < sizeof(types) / sizeof(types[0]); ++i) {
    const cmeta_data_desc *data = types[i];
    const cmeta_data_integer_shape *shape = (const cmeta_data_integer_shape *)data->shape;
    const unsigned bits = shape->bits;
    if (!cmeta_type_identity_equal(value->type_identity, data->storage_type->identity)) continue;
    if (data->kind == CMETA_DATA_UINT) {
      const uint64_t maximum = bits == 64u ? UINT64_MAX : (UINT64_C(1) << bits) - 1u;
      if ((source_signed && signed_value < 0) || unsigned_value > maximum) return CSERDE_SOURCE_ERROR;
      out->kind = CSERDE_UINT;
      out->value.uint = unsigned_value;
    } else {
      const int64_t maximum = bits == 64u ? INT64_MAX : (int64_t)((UINT64_C(1) << (bits - 1u)) - 1u);
      const int64_t minimum = bits == 64u ? INT64_MIN : -(int64_t)(UINT64_C(1) << (bits - 1u));
      if ((source_signed && (signed_value < minimum || signed_value > maximum)) ||
          (!source_signed && unsigned_value > (uint64_t)maximum)) return CSERDE_SOURCE_ERROR;
      out->kind = CSERDE_SINT;
      out->value.sint = source_signed ? signed_value : (int64_t)unsigned_value;
    }
    break;
  }
  return CSERDE_OK;
}

static cserde_status reader_value(ValueReader *state, const DataBindValue *value,
                                  cserde_token *out) {
  ValueReaderFrame *frame;
  if (value == NULL) return CSERDE_SOURCE_ERROR;
  if (state->items == state->limits.max_items) return CSERDE_LIMIT_EXCEEDED;
  ++state->items;
  switch (value->kind) {
  case DATA_BIND_VALUE_NULL: out->kind = CSERDE_NULL; return CSERDE_OK;
  case DATA_BIND_VALUE_BOOL:
    out->kind = CSERDE_BOOL; out->value.boolean = value->data.bool_val != 0;
    return CSERDE_OK;
  case DATA_BIND_VALUE_INT:
  case DATA_BIND_VALUE_INT64:
  case DATA_BIND_VALUE_UINT64:
    return reader_integer(state, value, out);
  case DATA_BIND_VALUE_DOUBLE:
    out->kind = CSERDE_FLOAT; out->value.floating = value->data.double_val;
    return CSERDE_OK;
  case DATA_BIND_VALUE_STRING:
    return reader_slice(state, out, CSERDE_STRING, value->data.string_val.ptr,
                         value->data.string_val.len);
  case DATA_BIND_VALUE_BYTES:
    return reader_slice(state, out, CSERDE_BYTES, value->data.bytes_val.ptr,
                         value->data.bytes_val.len);
  case DATA_BIND_VALUE_UUID:
    return reader_slice(state, out, CSERDE_BYTES, value->data.uuid_val.bytes,
                         DATA_BIND_UUID_SIZE);
  case DATA_BIND_VALUE_OBJECT:
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
  case DATA_BIND_VALUE_MAP:
    if (data_bind_internal_storage_kind(value) == DB_INTERNAL_STORAGE_SCALAR)
      return CSERDE_SOURCE_ERROR;
    if (state->depth == state->limits.max_depth) return CSERDE_LIMIT_EXCEEDED;
    frame = &state->frames[state->depth++];
    *frame = (ValueReaderFrame){value, 0u,
        value->kind == DATA_BIND_VALUE_OBJECT ? data_bind_value_field_count(value)
                                             : data_bind_value_count(value), 0};
    out->kind = value->kind == DATA_BIND_VALUE_OBJECT || value->kind == DATA_BIND_VALUE_MAP
        ? CSERDE_MAP_BEGIN : CSERDE_ARRAY_BEGIN;
    return CSERDE_OK;
  default: return CSERDE_UNSUPPORTED;
  }
}
static cserde_status reader_next(void *context, cserde_token *out) {
  ValueReader *state = context;
  const DataBindValue *child;
  ValueReaderFrame *frame;
  *out = (cserde_token){0};
  if (state->root != NULL) {
    child = state->root;
    state->root = NULL;
    return reader_value(state, child, out);
  }
  if (state->depth == 0u) return CSERDE_DONE;
  frame = &state->frames[state->depth - 1u];
  if (frame->index == frame->count) {
    out->kind = frame->value->kind == DATA_BIND_VALUE_OBJECT ||
                        frame->value->kind == DATA_BIND_VALUE_MAP
        ? CSERDE_MAP_END : CSERDE_ARRAY_END;
    --state->depth;
    return CSERDE_OK;
  }
  if (frame->value->kind == DATA_BIND_VALUE_OBJECT) {
    if (!frame->value_pending) {
      const char *name = data_bind_value_field_name(frame->value, frame->index);
      size_t size = 0u;
      const size_t remaining = state->limits.max_view_bytes - state->view_bytes;
      if (name == NULL) return CSERDE_SOURCE_ERROR;
      /* Bound scanning as well as the emitted view; no unbounded strlen. */
      while (name[size] != '\0') {
        if (size == remaining) return CSERDE_LIMIT_EXCEEDED;
        ++size;
      }
      frame->value_pending = 1;
      return reader_slice(state, out, CSERDE_STRING, name, size);
    }
    child = data_bind_value_field_at(frame->value, frame->index++);
    frame->value_pending = 0;
  } else if (frame->value->kind == DATA_BIND_VALUE_MAP) {
    const db_map_entry_slot_t *entry = vec_at_const(
        &frame->value->data.map.ordered_entries, frame->index);
    if (entry == NULL) return CSERDE_SOURCE_ERROR;
    child = frame->value_pending ? entry->value : entry->key_value;
    if (frame->value_pending) ++frame->index;
    frame->value_pending = !frame->value_pending;
  } else {
    child = data_bind_value_at(frame->value, frame->index++);
  }
  return reader_value(state, child, out);
}
static const cserde_reader_ops READER_OPS = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, reader_next};

static DataBindStatus value_reader_open(
    const DataBindValue *value, const DataBindValueReaderLimits *limits,
    cserde_reader **out_reader, DataBindError *error, int typed_integers) {
  const DataBindValueReaderLimits defaults = DATA_BIND_VALUE_READER_LIMITS_INIT;
  ValueReader *state;
  if (out_reader == NULL)
    return reader_error(error, DATA_BIND_ERR_INVALID_ARG, "Missing value reader output");
  *out_reader = NULL;
  if (limits == NULL) limits = &defaults;
  if (value == NULL || limits->size < sizeof(*limits) ||
      limits->abi_version != DATA_BIND_VALUE_READER_ABI_VERSION || limits->max_items == 0u)
    return reader_error(error, DATA_BIND_ERR_INVALID_ARG, "Invalid value reader source or limits");
  if (limits->max_depth > (SIZE_MAX - sizeof(*state)) / sizeof(ValueReaderFrame))
    return reader_error(error, DATA_BIND_ERR_LIMIT, "Value reader stack size overflow");
  state = calloc(1u, sizeof(*state) + limits->max_depth * sizeof(ValueReaderFrame));
  if (state == NULL)
    return reader_error(error, DATA_BIND_ERR_OOM, "Unable to allocate bounded value reader stack");
  state->root = value;
  state->limits = *limits;
  state->typed_integers = typed_integers;
  if (cserde_reader_init(&state->reader, &READER_OPS, state) != CSERDE_OK) {
    free(state);
    return reader_error(error, DATA_BIND_ERR_RUNTIME, "Unable to initialize value reader");
  }
  *out_reader = &state->reader;
  return reader_error(error, DATA_BIND_OK, "");
}
DataBindStatus data_bind_value_reader_open(
    const DataBindValue *value, const DataBindValueReaderLimits *limits,
    cserde_reader **out_reader, DataBindError *error) {
  return value_reader_open(value, limits, out_reader, error, 0);
}
DataBindStatus data_bind_internal_value_reader_open_typed(
    const DataBindValue *value, const DataBindValueReaderLimits *limits,
    cserde_reader **out_reader, DataBindError *error) {
  return value_reader_open(value, limits, out_reader, error, 1);
}
void data_bind_value_reader_close(cserde_reader *reader) {
  if (reader != NULL) free(reader->context);
}
