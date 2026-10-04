#include "data_bind_binary_reader.h"

#include "data_bind_binary_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum DataBindBinaryReaderStage {
  DATA_BIND_BINARY_READER_MAP_BEGIN = 0,
  DATA_BIND_BINARY_READER_FIELD_KEY,
  DATA_BIND_BINARY_READER_FIELD_VALUE,
  DATA_BIND_BINARY_READER_MAP_END,
  DATA_BIND_BINARY_READER_DONE
} DataBindBinaryReaderStage;

typedef struct DataBindBinaryReaderOwner {
  cserde_reader reader;
  const DataBindBinaryLayoutPlan *plan;
  const unsigned char *payload;
  size_t payload_bytes;
  size_t field_index;
  const DataBindBinaryFieldPlan *current;
  DataBindBinaryReaderStage stage;
} DataBindBinaryReaderOwner;

static void binary_error_clear(DataBindError *error) {
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

static DataBindStatus binary_fail(
    DataBindError *error,
    DataBindStatus status,
    const char *path,
    const char *message) {
  size_t size;
  if (error == NULL) return status;
  size = error->size != 0u && error->size < sizeof(*error)
             ? error->size
             : sizeof(*error);
  binary_error_clear(error);
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = status;
  if (size >= offsetof(DataBindError, path) + sizeof(error->path))
    snprintf(error->path, sizeof(error->path), "%s",
             path != NULL ? path : "");
  if (size >= offsetof(DataBindError, message) + sizeof(error->message))
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "Binary reader failed");
  return status;
}

static int binary_size_add(size_t left, size_t right, size_t *out) {
  if (out == NULL || right > SIZE_MAX - left) return 0;
  *out = left + right;
  return 1;
}

static int binary_ranges_overlap(
    size_t left_offset, size_t left_size,
    size_t right_offset, size_t right_size) {
  size_t left_end;
  size_t right_end;
  if (left_size == 0u || right_size == 0u) return 0;
  if (!binary_size_add(left_offset, left_size, &left_end) ||
      !binary_size_add(right_offset, right_size, &right_end))
    return 1;
  return left_offset < right_end && right_offset < left_end;
}

static int binary_token_width_valid(
    cserde_token_kind kind, unsigned bits) {
  switch (kind) {
  case CSERDE_BOOL:
    return bits == 8u;
  case CSERDE_SINT:
  case CSERDE_UINT:
    return bits == 8u || bits == 16u || bits == 32u || bits == 64u;
  case CSERDE_FLOAT:
    return bits == 32u || bits == 64u;
  default:
    return 0;
  }
}

static size_t binary_field_representation(
    const DataBindBinaryFieldPlan *field) {
  if (field == NULL ||
      field->size <
          offsetof(DataBindBinaryFieldPlan, representation) +
              sizeof(field->representation))
    return DATA_BIND_BINARY_REP_FIXED;
  return field->representation;
}

static int binary_field_has_var_data_tail(
    const DataBindBinaryFieldPlan *field) {
  return field != NULL &&
         field->size >=
             offsetof(DataBindBinaryFieldPlan, tail_prefix_bytes) +
                 sizeof(field->tail_prefix_bytes);
}

DataBindStatus data_bind_binary_layout_plan_validate(
    const DataBindBinaryLayoutPlan *plan,
    DataBindError *error) {
  size_t state_end;
  size_t i;
  int saw_var_data = 0;

  binary_error_clear(error);
  if (plan == NULL ||
      plan->size < sizeof(*plan) ||
      plan->abi_version != DATA_BIND_BINARY_LAYOUT_PLAN_ABI_VERSION ||
      plan->type_name == NULL || plan->type_name[0] == '\0' ||
      (plan->field_count != 0u && plan->fields == NULL))
    return binary_fail(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Binary reader plan is incomplete");

  if (!binary_size_add(
          plan->presence_size, plan->null_size, &state_end) ||
      plan->presence_offset != 0u ||
      plan->null_offset != plan->presence_size ||
      state_end > plan->fixed_block_size)
    return binary_fail(
        error, DATA_BIND_ERR_SCHEMA, plan->type_name,
        "Binary reader state exceeds the fixed block");

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindBinaryFieldPlan *field = &plan->fields[i];
    size_t end;
    size_t j;

    const size_t representation = binary_field_representation(field);

    if (field->size < DATA_BIND_BINARY_FIELD_PLAN_V1_SIZE ||
        field->field_name == NULL || field->field_name[0] == '\0')
      return binary_fail(
          error, DATA_BIND_ERR_SCHEMA,
          field->field_name != NULL ? field->field_name : plan->type_name,
          "Binary reader field metadata is incomplete");

    if (representation == DATA_BIND_BINARY_REP_FIXED) {
      if (saw_var_data ||
          !binary_token_width_valid(field->token_kind, field->scalar_bits) ||
          field->wire_extent != (size_t)(field->scalar_bits / 8u) ||
          !binary_size_add(field->wire_offset, field->wire_extent, &end) ||
          end > plan->fixed_block_size ||
          binary_ranges_overlap(
              field->wire_offset, field->wire_extent, 0u, state_end))
        return binary_fail(
            error, DATA_BIND_ERR_SCHEMA, field->field_name,
            "Binary scalar field layout is invalid");
    } else if (representation == DATA_BIND_BINARY_REP_VAR_DATA) {
      saw_var_data = 1;
      if (!binary_field_has_var_data_tail(field) ||
          (field->token_kind != CSERDE_STRING &&
           field->token_kind != CSERDE_BYTES) ||
          field->scalar_bits != 0u ||
          field->wire_offset != 0u || field->wire_extent != 0u ||
          field->tail_prefix_bytes != sizeof(uint32_t))
        return binary_fail(
            error, DATA_BIND_ERR_SCHEMA, field->field_name,
            "Binary VAR_DATA field layout is invalid");
    } else {
      return binary_fail(
          error, DATA_BIND_ERR_SCHEMA, field->field_name,
          "Binary reader field representation is unsupported");
    }

    if ((field->flags & DATA_BIND_BINARY_FIELD_OPTIONAL) != 0u) {
      if (plan->presence_size == 0u ||
          field->optional_bit / 8u >= plan->presence_size)
        return binary_fail(
            error, DATA_BIND_ERR_SCHEMA, field->field_name,
            "Binary optional state bit is outside the presence bitmap");
    }
    if ((field->flags & DATA_BIND_BINARY_FIELD_NULLABLE) != 0u) {
      if (plan->null_size == 0u ||
          field->nullable_bit / 8u >= plan->null_size)
        return binary_fail(
            error, DATA_BIND_ERR_SCHEMA, field->field_name,
            "Binary nullable state bit is outside the null bitmap");
    }
    if ((field->flags &
         ~(DATA_BIND_BINARY_FIELD_OPTIONAL |
           DATA_BIND_BINARY_FIELD_NULLABLE)) != 0u)
      return binary_fail(
          error, DATA_BIND_ERR_SCHEMA, field->field_name,
          "Binary reader field contains unknown state flags");

    for (j = 0u; j < i; ++j) {
      const DataBindBinaryFieldPlan *prior = &plan->fields[j];
      if (strcmp(prior->field_name, field->field_name) == 0 ||
          (representation == DATA_BIND_BINARY_REP_FIXED &&
           binary_field_representation(prior) ==
               DATA_BIND_BINARY_REP_FIXED &&
           binary_ranges_overlap(
               prior->wire_offset, prior->wire_extent,
               field->wire_offset, field->wire_extent)))
        return binary_fail(
            error, DATA_BIND_ERR_SCHEMA, field->field_name,
            "Binary reader fields overlap or duplicate canonical identity");
    }
  }

  if (plan->fixed_block_size == 0u && !saw_var_data)
    return binary_fail(
        error, DATA_BIND_ERR_INVALID_ARG, plan->type_name,
        "Binary reader plan has no fixed block or VAR_DATA tail");
  return DATA_BIND_OK;
}

static int binary_state_bit(
    const unsigned char *payload,
    size_t offset,
    unsigned bit) {
  return (payload[offset + bit / 8u] &
          (unsigned char)(1u << (bit % 8u))) != 0u;
}

static DataBindStatus binary_wire_state_validate(
    const DataBindBinaryLayoutPlan *plan,
    const unsigned char *payload,
    DataBindError *error) {
  size_t i;
  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindBinaryFieldPlan *field = &plan->fields[i];
    const int optional =
        (field->flags & DATA_BIND_BINARY_FIELD_OPTIONAL) != 0u;
    const int nullable =
        (field->flags & DATA_BIND_BINARY_FIELD_NULLABLE) != 0u;
    const int present =
        !optional ||
        binary_state_bit(
            payload, plan->presence_offset, field->optional_bit);
    const int is_null =
        nullable &&
        binary_state_bit(
            payload, plan->null_offset, field->nullable_bit);

    if (!present && is_null)
      return binary_fail(
          error, DATA_BIND_ERR_PARSE, field->field_name,
          "Binary null state is set while optional field is absent");
  }
  return DATA_BIND_OK;
}

static DataBindStatus binary_tail_preflight(
    const DataBindBinaryLayoutPlan *plan,
    const unsigned char *payload,
    size_t payload_bytes,
    DataBindError *error) {
  size_t cursor;
  size_t i;

  if (plan == NULL || payload == NULL ||
      payload_bytes < plan->fixed_block_size)
    return binary_fail(
        error, DATA_BIND_ERR_PARSE,
        plan != NULL ? plan->type_name : NULL,
        "Binary payload is shorter than the fixed block");

  cursor = plan->fixed_block_size;
  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindBinaryFieldPlan *field = &plan->fields[i];
    uint32_t length;
    size_t remaining;
    const int optional =
        (field->flags & DATA_BIND_BINARY_FIELD_OPTIONAL) != 0u;
    const int nullable =
        (field->flags & DATA_BIND_BINARY_FIELD_NULLABLE) != 0u;
    const int present =
        !optional ||
        binary_state_bit(
            payload, plan->presence_offset, field->optional_bit);
    const int is_null =
        nullable &&
        binary_state_bit(
            payload, plan->null_offset, field->nullable_bit);

    if (binary_field_representation(field) !=
        DATA_BIND_BINARY_REP_VAR_DATA)
      continue;

    if (cursor > payload_bytes ||
        payload_bytes - cursor < sizeof(uint32_t))
      return binary_fail(
          error, DATA_BIND_ERR_PARSE, field->field_name,
          "Binary VAR_DATA length prefix is truncated");

    remaining = payload_bytes - cursor;
    length = data_bind_binary_wire_read_u32(
        payload + cursor, plan->wire_big_endian);
    if ((size_t)length > remaining - sizeof(uint32_t))
      return binary_fail(
          error, DATA_BIND_ERR_PARSE, field->field_name,
          "Binary VAR_DATA payload is truncated");

    /*
     * Binary tail entries are positional. An optional ABSENT field still owns
     * and consumes its encoded tail entry; its bytes are simply not published
     * through CSerde. This preserves the existing Binary wire contract.
     * Explicit NULL is different: its canonical tail payload must be empty.
     */
    if (is_null && length != 0u)
      return binary_fail(
          error, DATA_BIND_ERR_PARSE, field->field_name,
          "Binary NULL VAR_DATA field has a nonzero payload length");

    cursor += sizeof(uint32_t) + (size_t)length;
  }

  if (cursor != payload_bytes)
    return binary_fail(
        error, DATA_BIND_ERR_PARSE, plan->type_name,
        "Binary payload has trailing bytes after the VAR_DATA tail");

  return DATA_BIND_OK;
}

static int binary_field_present(
    const DataBindBinaryReaderOwner *owner,
    const DataBindBinaryFieldPlan *field) {
  if ((field->flags & DATA_BIND_BINARY_FIELD_OPTIONAL) == 0u)
    return 1;
  return binary_state_bit(
      owner->payload, owner->plan->presence_offset, field->optional_bit);
}

static int binary_field_null(
    const DataBindBinaryReaderOwner *owner,
    const DataBindBinaryFieldPlan *field) {
  if ((field->flags & DATA_BIND_BINARY_FIELD_NULLABLE) == 0u)
    return 0;
  return binary_state_bit(
      owner->payload, owner->plan->null_offset, field->nullable_bit);
}

static cserde_status binary_var_data_view_at(
    const DataBindBinaryReaderOwner *owner,
    size_t target_index,
    DataBindBinaryVarData *out) {
  size_t cursor;
  size_t i;

  if (owner == NULL || owner->plan == NULL || out == NULL ||
      target_index >= owner->plan->field_count)
    return CSERDE_INVALID_ARGUMENT;

  out->data = NULL;
  out->size = 0u;
  cursor = owner->plan->fixed_block_size;

  for (i = 0u; i < owner->plan->field_count; ++i) {
    const DataBindBinaryFieldPlan *field = &owner->plan->fields[i];
    uint32_t length;

    if (binary_field_representation(field) !=
        DATA_BIND_BINARY_REP_VAR_DATA)
      continue;
    if (cursor > owner->payload_bytes ||
        owner->payload_bytes - cursor < sizeof(uint32_t))
      return CSERDE_INVALID_STATE;

    length = data_bind_binary_wire_read_u32(
        owner->payload + cursor, owner->plan->wire_big_endian);
    if ((size_t)length >
        owner->payload_bytes - cursor - sizeof(uint32_t))
      return CSERDE_INVALID_STATE;

    if (i == target_index) {
      out->data = owner->payload + cursor + sizeof(uint32_t);
      out->size = (size_t)length;
      return CSERDE_OK;
    }

    cursor += sizeof(uint32_t) + (size_t)length;
  }

  return CSERDE_INVALID_STATE;
}

static cserde_status binary_var_data_token(
    const DataBindBinaryReaderOwner *owner,
    size_t field_index,
    const DataBindBinaryFieldPlan *field,
    cserde_token *out) {
  DataBindBinaryVarData value = {0};

  if (field == NULL || out == NULL ||
      (field->token_kind != CSERDE_STRING &&
       field->token_kind != CSERDE_BYTES))
    return CSERDE_INVALID_ARGUMENT;
  if (binary_var_data_view_at(owner, field_index, &value) != CSERDE_OK)
    return CSERDE_INVALID_STATE;

  memset(out, 0, sizeof(*out));
  out->kind = field->token_kind;
  out->value.slice.data = value.data;
  out->value.slice.size = value.size;
  out->value.slice.lifetime = CSERDE_VIEW_STABLE;
  return CSERDE_OK;
}

static cserde_token binary_field_key(
    const DataBindBinaryFieldPlan *field) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data =
      (const unsigned char *)field->field_name;
  token.value.slice.size = strlen(field->field_name);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static cserde_status binary_scalar_token(
    const DataBindBinaryReaderOwner *owner,
    const DataBindBinaryFieldPlan *field,
    cserde_token *out) {
  const unsigned char *source =
      owner->payload + field->wire_offset;
  memset(out, 0, sizeof(*out));

  switch (field->token_kind) {
  case CSERDE_BOOL:
    out->kind = CSERDE_BOOL;
    out->value.boolean =
        data_bind_binary_wire_read_u8(source, owner->plan->wire_big_endian) != 0u;
    return CSERDE_OK;

  case CSERDE_SINT:
    out->kind = CSERDE_SINT;
    switch (field->scalar_bits) {
    case 8u:
      out->value.sint =
          (int64_t)data_bind_binary_wire_read_i8(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    case 16u:
      out->value.sint =
          (int64_t)data_bind_binary_wire_read_i16(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    case 32u:
      out->value.sint =
          (int64_t)data_bind_binary_wire_read_i32(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    case 64u:
      out->value.sint =
          data_bind_binary_wire_read_i64(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    default:
      return CSERDE_INVALID_STATE;
    }

  case CSERDE_UINT:
    out->kind = CSERDE_UINT;
    switch (field->scalar_bits) {
    case 8u:
      out->value.uint =
          (uint64_t)data_bind_binary_wire_read_u8(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    case 16u:
      out->value.uint =
          (uint64_t)data_bind_binary_wire_read_u16(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    case 32u:
      out->value.uint =
          (uint64_t)data_bind_binary_wire_read_u32(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    case 64u:
      out->value.uint =
          data_bind_binary_wire_read_u64(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    default:
      return CSERDE_INVALID_STATE;
    }

  case CSERDE_FLOAT:
    out->kind = CSERDE_FLOAT;
    if (field->scalar_bits == 32u) {
      out->value.floating =
          (double)data_bind_binary_wire_read_f32(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    }
    if (field->scalar_bits == 64u) {
      out->value.floating =
          data_bind_binary_wire_read_f64(
              source, owner->plan->wire_big_endian);
      return CSERDE_OK;
    }
    return CSERDE_INVALID_STATE;

  default:
    return CSERDE_INVALID_STATE;
  }
}

static cserde_status binary_reader_next(
    void *context, cserde_token *out) {
  DataBindBinaryReaderOwner *owner =
      (DataBindBinaryReaderOwner *)context;

  if (owner == NULL || out == NULL || owner->plan == NULL)
    return CSERDE_INVALID_ARGUMENT;

  for (;;) {
    switch (owner->stage) {
    case DATA_BIND_BINARY_READER_MAP_BEGIN:
      memset(out, 0, sizeof(*out));
      out->kind = CSERDE_MAP_BEGIN;
      owner->stage = DATA_BIND_BINARY_READER_FIELD_KEY;
      return CSERDE_OK;

    case DATA_BIND_BINARY_READER_FIELD_KEY:
      while (owner->field_index < owner->plan->field_count &&
             !binary_field_present(
                 owner, &owner->plan->fields[owner->field_index]))
        ++owner->field_index;

      if (owner->field_index >= owner->plan->field_count) {
        owner->stage = DATA_BIND_BINARY_READER_MAP_END;
        continue;
      }

      owner->current = &owner->plan->fields[owner->field_index];
      *out = binary_field_key(owner->current);
      owner->stage = DATA_BIND_BINARY_READER_FIELD_VALUE;
      return CSERDE_OK;

    case DATA_BIND_BINARY_READER_FIELD_VALUE:
      if (owner->current == NULL)
        return CSERDE_INVALID_STATE;
      if (binary_field_null(owner, owner->current)) {
        memset(out, 0, sizeof(*out));
        out->kind = CSERDE_NULL;
      } else {
        cserde_status status;
        if (binary_field_representation(owner->current) ==
            DATA_BIND_BINARY_REP_VAR_DATA)
          status = binary_var_data_token(
              owner, owner->field_index, owner->current, out);
        else
          status = binary_scalar_token(owner, owner->current, out);
        if (status != CSERDE_OK) return status;
      }
      ++owner->field_index;
      owner->current = NULL;
      owner->stage = DATA_BIND_BINARY_READER_FIELD_KEY;
      return CSERDE_OK;

    case DATA_BIND_BINARY_READER_MAP_END:
      memset(out, 0, sizeof(*out));
      out->kind = CSERDE_MAP_END;
      owner->stage = DATA_BIND_BINARY_READER_DONE;
      return CSERDE_OK;

    case DATA_BIND_BINARY_READER_DONE:
      return CSERDE_DONE;

    default:
      return CSERDE_INVALID_STATE;
    }
  }
}

static const cserde_reader_ops BINARY_READER_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    binary_reader_next};

DataBindStatus data_bind_binary_reader_open(
    const DataBindBinaryLayoutPlan *plan,
    const void *payload,
    size_t payload_bytes,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  DataBindBinaryReaderOwner *owner;
  DataBindStatus status;
  (void)max_depth;

  binary_error_clear(error);
  if (out_reader != NULL) *out_reader = NULL;
  if (out_owner != NULL) *out_owner = NULL;

  if (out_reader == NULL || out_owner == NULL ||
      (payload == NULL && payload_bytes != 0u))
    return binary_fail(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid Binary reader open arguments");

  status = data_bind_binary_layout_plan_validate(plan, error);
  if (status != DATA_BIND_OK) return status;

  if (payload_bytes < plan->fixed_block_size)
    return binary_fail(
        error, DATA_BIND_ERR_PARSE, plan->type_name,
        "Binary payload is shorter than the fixed block");

  status = binary_wire_state_validate(
      plan, (const unsigned char *)payload, error);
  if (status != DATA_BIND_OK) return status;

  status = binary_tail_preflight(
      plan, (const unsigned char *)payload, payload_bytes, error);
  if (status != DATA_BIND_OK) return status;

  owner = (DataBindBinaryReaderOwner *)calloc(1u, sizeof(*owner));
  if (owner == NULL)
    return binary_fail(
        error, DATA_BIND_ERR_OOM, plan->type_name,
        "Could not allocate bounded Binary reader lease");

  owner->plan = plan;
  owner->payload = (const unsigned char *)payload;
  owner->payload_bytes = payload_bytes;
  owner->stage = DATA_BIND_BINARY_READER_MAP_BEGIN;

  if (cserde_reader_init(
          &owner->reader, &BINARY_READER_OPS, owner) != CSERDE_OK) {
    free(owner);
    return binary_fail(
        error, DATA_BIND_ERR_RUNTIME, plan->type_name,
        "Could not initialize Binary CSerde reader");
  }

  *out_reader = &owner->reader;
  *out_owner = owner;
  binary_error_clear(error);
  return DATA_BIND_OK;
}

void data_bind_binary_reader_close(
    cserde_reader *reader,
    void *owner) {
  (void)reader;
  free(owner);
}
