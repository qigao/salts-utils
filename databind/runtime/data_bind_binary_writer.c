#include "data_bind_binary_writer.h"

#include "data_bind_binary_wire.h"

#include <limits.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum DataBindBinaryWriterStage {
  DATA_BIND_BINARY_WRITER_ROOT = 0,
  DATA_BIND_BINARY_WRITER_KEY,
  DATA_BIND_BINARY_WRITER_VALUE,
  DATA_BIND_BINARY_WRITER_DONE
} DataBindBinaryWriterStage;

typedef struct DataBindBinaryWriterOwner {
  cserde_writer writer;
  const DataBindBinaryLayoutPlan *plan;
  DataBindWriteFn write;
  void *write_user;
  unsigned char *buffer;
  size_t length;
  size_t capacity;
  size_t next_field;
  size_t current_field;
  DataBindBinaryWriterStage stage;
  int committed;
  int finish_attempted;
  cserde_status finish_status;
} DataBindBinaryWriterOwner;

static void binary_writer_error_clear(DataBindError *error) {
  size_t size;
  if (error == NULL) return;
  size = error->size != 0u && error->size < sizeof(*error)
             ? error->size
             : sizeof(*error);
  memset(error, 0, size);
  if (size >= sizeof(size_t)) error->size = size;
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  if (size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
}

static DataBindStatus binary_writer_fail(
    DataBindError *error, DataBindStatus status,
    const char *path, const char *message) {
  size_t size;
  if (error == NULL) return status;
  size = error->size != 0u && error->size < sizeof(*error)
             ? error->size
             : sizeof(*error);
  binary_writer_error_clear(error);
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = status;
  if (size >= offsetof(DataBindError, path) + sizeof(error->path))
    snprintf(error->path, sizeof(error->path), "%s",
             path != NULL ? path : "");
  if (size >= offsetof(DataBindError, message) + sizeof(error->message))
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "Binary writer failed");
  return status;
}

static size_t binary_writer_representation(
    const DataBindBinaryFieldPlan *field) {
  if (field == NULL ||
      field->size <
          offsetof(DataBindBinaryFieldPlan, representation) +
              sizeof(field->representation))
    return DATA_BIND_BINARY_REP_FIXED;
  return field->representation;
}

static int binary_writer_optional(
    const DataBindBinaryFieldPlan *field) {
  return field != NULL &&
         (field->flags & DATA_BIND_BINARY_FIELD_OPTIONAL) != 0u;
}

static int binary_writer_nullable(
    const DataBindBinaryFieldPlan *field) {
  return field != NULL &&
         (field->flags & DATA_BIND_BINARY_FIELD_NULLABLE) != 0u;
}

static void binary_writer_state_bit(
    DataBindBinaryWriterOwner *owner,
    size_t offset, unsigned bit, int value) {
  unsigned char *state = owner->buffer + offset + bit / 8u;
  const unsigned char mask = (unsigned char)(1u << (bit % 8u));
  if (value)
    *state |= mask;
  else
    *state &= (unsigned char)~mask;
}

static int binary_writer_reserve(
    DataBindBinaryWriterOwner *owner, size_t extra) {
  size_t required;
  size_t capacity;
  unsigned char *next;
  if (owner == NULL || extra > SIZE_MAX - owner->length) return 0;
  required = owner->length + extra;
  if (required <= owner->capacity) return 1;
  capacity = owner->capacity != 0u ? owner->capacity : 1u;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2u) {
      capacity = required;
      break;
    }
    capacity *= 2u;
  }
  next = (unsigned char *)realloc(owner->buffer, capacity);
  if (next == NULL) return 0;
  if (capacity > owner->capacity)
    memset(next + owner->capacity, 0, capacity - owner->capacity);
  owner->buffer = next;
  owner->capacity = capacity;
  return 1;
}

static int binary_writer_append_var_data(
    DataBindBinaryWriterOwner *owner,
    const void *data, size_t size) {
  size_t bytes;
  if (owner == NULL || size > (size_t)UINT32_MAX ||
      size > SIZE_MAX - sizeof(uint32_t))
    return 0;
  bytes = sizeof(uint32_t) + size;
  if (!binary_writer_reserve(owner, bytes)) return 0;
  if (!data_bind_binary_wire_write_var_data(
          owner->buffer + owner->length, bytes,
          owner->plan->wire_big_endian, data, size))
    return 0;
  owner->length += bytes;
  return 1;
}

static int binary_writer_skip_field(
    DataBindBinaryWriterOwner *owner,
    const DataBindBinaryFieldPlan *field) {
  if (!binary_writer_optional(field)) return 0;
  binary_writer_state_bit(
      owner, owner->plan->presence_offset, field->optional_bit, 0);
  if (binary_writer_nullable(field))
    binary_writer_state_bit(
        owner, owner->plan->null_offset, field->nullable_bit, 0);
  if (binary_writer_representation(field) == DATA_BIND_BINARY_REP_VAR_DATA)
    return binary_writer_append_var_data(owner, NULL, 0u);
  return 1;
}

static int binary_writer_key_equal(
    const cserde_token *token,
    const char *field_name) {
  size_t size;
  if (token == NULL || field_name == NULL ||
      token->kind != CSERDE_STRING)
    return 0;
  size = strlen(field_name);
  return token->value.slice.size == size &&
         (size == 0u ||
          (token->value.slice.data != NULL &&
           memcmp(token->value.slice.data, field_name, size) == 0));
}

static int binary_writer_find_field(
    DataBindBinaryWriterOwner *owner,
    const cserde_token *token,
    size_t *out_index) {
  size_t i;
  if (owner == NULL || token == NULL || out_index == NULL) return 0;
  for (i = owner->next_field; i < owner->plan->field_count; ++i) {
    const DataBindBinaryFieldPlan *field = &owner->plan->fields[i];
    if (binary_writer_key_equal(token, field->field_name)) {
      size_t skipped;
      for (skipped = owner->next_field; skipped < i; ++skipped)
        if (!binary_writer_skip_field(owner, &owner->plan->fields[skipped]))
          return 0;
      *out_index = i;
      return 1;
    }
    if (!binary_writer_optional(field)) return 0;
  }
  return 0;
}

static int binary_writer_sint_in_range(int64_t value, unsigned bits) {
  switch (bits) {
  case 8u: return value >= INT8_MIN && value <= INT8_MAX;
  case 16u: return value >= INT16_MIN && value <= INT16_MAX;
  case 32u: return value >= INT32_MIN && value <= INT32_MAX;
  case 64u: return 1;
  default: return 0;
  }
}

static int binary_writer_uint_in_range(uint64_t value, unsigned bits) {
  switch (bits) {
  case 8u: return value <= UINT8_MAX;
  case 16u: return value <= UINT16_MAX;
  case 32u: return value <= UINT32_MAX;
  case 64u: return 1;
  default: return 0;
  }
}

static int binary_writer_fixed_value(
    DataBindBinaryWriterOwner *owner,
    const DataBindBinaryFieldPlan *field,
    const cserde_token *token) {
  unsigned char *destination;
  if (owner == NULL || field == NULL || token == NULL ||
      field->wire_offset > owner->plan->fixed_block_size ||
      field->wire_extent >
          owner->plan->fixed_block_size - field->wire_offset)
    return 0;
  destination = owner->buffer + field->wire_offset;

  if (token->kind != field->token_kind) return 0;
  switch (field->token_kind) {
  case CSERDE_BOOL:
    if (field->scalar_bits != 8u) return 0;
    data_bind_binary_wire_write_u8(
        destination, owner->plan->wire_big_endian,
        token->value.boolean ? 1u : 0u);
    return 1;
  case CSERDE_SINT:
    if (!binary_writer_sint_in_range(
            token->value.sint, field->scalar_bits))
      return 0;
    switch (field->scalar_bits) {
    case 8u:
      data_bind_binary_wire_write_i8(
          destination, owner->plan->wire_big_endian,
          (int8_t)token->value.sint);
      return 1;
    case 16u:
      data_bind_binary_wire_write_i16(
          destination, owner->plan->wire_big_endian,
          (int16_t)token->value.sint);
      return 1;
    case 32u:
      data_bind_binary_wire_write_i32(
          destination, owner->plan->wire_big_endian,
          (int32_t)token->value.sint);
      return 1;
    case 64u:
      data_bind_binary_wire_write_i64(
          destination, owner->plan->wire_big_endian,
          token->value.sint);
      return 1;
    default:
      return 0;
    }
  case CSERDE_UINT:
    if (!binary_writer_uint_in_range(
            token->value.uint, field->scalar_bits))
      return 0;
    switch (field->scalar_bits) {
    case 8u:
      data_bind_binary_wire_write_u8(
          destination, owner->plan->wire_big_endian,
          (uint8_t)token->value.uint);
      return 1;
    case 16u:
      data_bind_binary_wire_write_u16(
          destination, owner->plan->wire_big_endian,
          (uint16_t)token->value.uint);
      return 1;
    case 32u:
      data_bind_binary_wire_write_u32(
          destination, owner->plan->wire_big_endian,
          (uint32_t)token->value.uint);
      return 1;
    case 64u:
      data_bind_binary_wire_write_u64(
          destination, owner->plan->wire_big_endian,
          token->value.uint);
      return 1;
    default:
      return 0;
    }
  case CSERDE_FLOAT:
    if (field->scalar_bits == 32u) {
      const float value = (float)token->value.floating;
      if (token->value.floating > (double)FLT_MAX ||
          token->value.floating < -(double)FLT_MAX)
        return 0;
      data_bind_binary_wire_write_f32(
          destination, owner->plan->wire_big_endian, value);
      return 1;
    }
    if (field->scalar_bits == 64u) {
      data_bind_binary_wire_write_f64(
          destination, owner->plan->wire_big_endian,
          token->value.floating);
      return 1;
    }
    return 0;
  default:
    return 0;
  }
}

static cserde_status binary_writer_value(
    DataBindBinaryWriterOwner *owner,
    const cserde_token *token) {
  const DataBindBinaryFieldPlan *field;
  const size_t representation =
      owner != NULL && owner->current_field < owner->plan->field_count
          ? binary_writer_representation(
                &owner->plan->fields[owner->current_field])
          : SIZE_MAX;

  if (owner == NULL || token == NULL ||
      owner->current_field >= owner->plan->field_count)
    return CSERDE_UNSUPPORTED;
  field = &owner->plan->fields[owner->current_field];

  if (binary_writer_optional(field))
    binary_writer_state_bit(
        owner, owner->plan->presence_offset, field->optional_bit, 1);

  if (token->kind == CSERDE_NULL) {
    if (!binary_writer_nullable(field)) return CSERDE_UNSUPPORTED;
    binary_writer_state_bit(
        owner, owner->plan->null_offset, field->nullable_bit, 1);
    if (representation == DATA_BIND_BINARY_REP_VAR_DATA &&
        !binary_writer_append_var_data(owner, NULL, 0u))
      return CSERDE_LIMIT_EXCEEDED;
  } else {
    if (binary_writer_nullable(field))
      binary_writer_state_bit(
          owner, owner->plan->null_offset, field->nullable_bit, 0);
    if (representation == DATA_BIND_BINARY_REP_FIXED) {
      if (!binary_writer_fixed_value(owner, field, token))
        return CSERDE_UNSUPPORTED;
    } else if (representation == DATA_BIND_BINARY_REP_VAR_DATA) {
      if (token->kind != field->token_kind ||
          (token->kind != CSERDE_STRING &&
           token->kind != CSERDE_BYTES) ||
          (token->value.slice.size != 0u &&
           token->value.slice.data == NULL))
        return CSERDE_UNSUPPORTED;
      if (!binary_writer_append_var_data(
              owner, token->value.slice.data,
              token->value.slice.size))
        return CSERDE_LIMIT_EXCEEDED;
    } else {
      return CSERDE_UNSUPPORTED;
    }
  }

  owner->next_field = owner->current_field + 1u;
  owner->stage = DATA_BIND_BINARY_WRITER_KEY;
  return CSERDE_OK;
}

static cserde_status binary_writer_write(
    void *opaque, const cserde_token *token) {
  DataBindBinaryWriterOwner *owner =
      (DataBindBinaryWriterOwner *)opaque;
  size_t field_index;

  if (owner == NULL || token == NULL || owner->committed)
    return CSERDE_UNSUPPORTED;

  switch (owner->stage) {
  case DATA_BIND_BINARY_WRITER_ROOT:
    if (token->kind != CSERDE_MAP_BEGIN)
      return CSERDE_UNSUPPORTED;
    owner->stage = DATA_BIND_BINARY_WRITER_KEY;
    return CSERDE_OK;

  case DATA_BIND_BINARY_WRITER_KEY:
    if (token->kind == CSERDE_MAP_END) {
      while (owner->next_field < owner->plan->field_count) {
        if (!binary_writer_skip_field(
                owner, &owner->plan->fields[owner->next_field]))
          return CSERDE_UNSUPPORTED;
        ++owner->next_field;
      }
      owner->stage = DATA_BIND_BINARY_WRITER_DONE;
      return CSERDE_OK;
    }
    if (!binary_writer_find_field(owner, token, &field_index))
      return CSERDE_UNSUPPORTED;
    owner->current_field = field_index;
    owner->stage = DATA_BIND_BINARY_WRITER_VALUE;
    return CSERDE_OK;

  case DATA_BIND_BINARY_WRITER_VALUE:
    return binary_writer_value(owner, token);

  case DATA_BIND_BINARY_WRITER_DONE:
  default:
    return CSERDE_UNSUPPORTED;
  }
}

static cserde_status binary_writer_finish(void *opaque) {
  DataBindBinaryWriterOwner *owner =
      (DataBindBinaryWriterOwner *)opaque;
  if (owner == NULL) return CSERDE_UNSUPPORTED;
  if (owner->finish_attempted) return owner->finish_status;
  owner->finish_attempted = 1;
  if (owner->write == NULL ||
      owner->stage != DATA_BIND_BINARY_WRITER_DONE) {
    owner->finish_status = CSERDE_UNSUPPORTED;
    return owner->finish_status;
  }
  if (owner->write(owner->buffer, owner->length, owner->write_user) != 0) {
    owner->finish_status = CSERDE_SINK_ERROR;
    return owner->finish_status;
  }
  owner->committed = 1;
  owner->finish_status = CSERDE_OK;
  return owner->finish_status;
}

static const cserde_writer_ops BINARY_WRITER_OPS = {
    sizeof(cserde_writer_ops),
    CSERDE_WRITER_OPS_ABI_VERSION,
    binary_writer_write,
    binary_writer_finish};

static DataBindStatus binary_writer_status(
    cserde_status status, const DataBindBinaryLayoutPlan *plan,
    DataBindError *error) {
  switch (status) {
  case CSERDE_OK:
    binary_writer_error_clear(error);
    return DATA_BIND_OK;
  case CSERDE_LIMIT_EXCEEDED:
    return binary_writer_fail(
        error, DATA_BIND_ERR_LIMIT,
        plan != NULL ? plan->type_name : NULL,
        "Binary writer exceeded a bounded size");
  case CSERDE_SINK_ERROR:
    return binary_writer_fail(
        error, DATA_BIND_ERR_IO,
        plan != NULL ? plan->type_name : NULL,
        "Binary writer byte sink rejected output");
  case CSERDE_INVALID_ARGUMENT:
    return binary_writer_fail(
        error, DATA_BIND_ERR_INVALID_ARG,
        plan != NULL ? plan->type_name : NULL,
        "Binary writer received invalid arguments");
  case CSERDE_UNSUPPORTED:
  case CSERDE_INVALID_STATE:
  default:
    return binary_writer_fail(
        error, DATA_BIND_ERR_SCHEMA,
        plan != NULL ? plan->type_name : NULL,
        "Binary writer token stream does not match the layout plan");
  }
}

DataBindStatus data_bind_binary_writer_open(
    const DataBindBinaryLayoutPlan *plan,
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    cserde_writer **out_writer,
    void **out_owner,
    DataBindError *error) {
  DataBindBinaryWriterOwner *owner;
  DataBindStatus status;
  size_t capacity;
  (void)max_depth;

  if (out_writer == NULL || out_owner == NULL || write == NULL)
    return binary_writer_fail(
        error, DATA_BIND_ERR_INVALID_ARG,
        plan != NULL ? plan->type_name : NULL,
        "Invalid Binary writer open arguments");
  *out_writer = NULL;
  *out_owner = NULL;

  status = data_bind_binary_layout_plan_validate(plan, error);
  if (status != DATA_BIND_OK) return status;

  owner = (DataBindBinaryWriterOwner *)calloc(1u, sizeof(*owner));
  if (owner == NULL)
    return binary_writer_fail(
        error, DATA_BIND_ERR_OOM, plan->type_name,
        "Could not allocate Binary writer state");

  capacity = plan->fixed_block_size != 0u
                 ? plan->fixed_block_size
                 : 1u;
  owner->buffer = (unsigned char *)calloc(1u, capacity);
  if (owner->buffer == NULL) {
    free(owner);
    return binary_writer_fail(
        error, DATA_BIND_ERR_OOM, plan->type_name,
        "Could not allocate Binary writer buffer");
  }

  owner->plan = plan;
  owner->write = write;
  owner->write_user = write_user;
  owner->length = plan->fixed_block_size;
  owner->capacity = capacity;
  owner->stage = DATA_BIND_BINARY_WRITER_ROOT;

  if (cserde_writer_init(
          &owner->writer, &BINARY_WRITER_OPS, owner) != CSERDE_OK) {
    free(owner->buffer);
    free(owner);
    return binary_writer_fail(
        error, DATA_BIND_ERR_RUNTIME, plan->type_name,
        "Could not initialize Binary CSerde writer");
  }

  *out_writer = &owner->writer;
  *out_owner = owner;
  binary_writer_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_binary_writer_close(
    cserde_writer *writer,
    void *opaque,
    DataBindError *error) {
  DataBindBinaryWriterOwner *owner =
      (DataBindBinaryWriterOwner *)opaque;
  const DataBindBinaryLayoutPlan *plan =
      owner != NULL ? owner->plan : NULL;
  cserde_status status =
      owner != NULL && owner->finish_attempted
          ? owner->finish_status
          : (writer != NULL ? cserde_writer_finish(writer)
                            : CSERDE_INVALID_ARGUMENT);
  DataBindStatus result =
      binary_writer_status(status, plan, error);
  if (owner != NULL) {
    free(owner->buffer);
    free(owner);
  }
  return result;
}
