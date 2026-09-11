from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


source_path = Path("tbe/data_bind/tbe_typed.c")
header_path = Path("tbe/data_bind/tbe_typed.h")
source = source_path.read_text(encoding="utf-8")
header = header_path.read_text(encoding="utf-8")

source = replace_once(
    source,
    """static int typed_add_fits(size_t left, size_t right, size_t *total) {
  if (total == NULL || right > SIZE_MAX - left) return 0;
  *total = left + right;
  return 1;
}
""",
    """static int typed_add_fits(size_t left, size_t right, size_t *total) {
  if (total == NULL || right > SIZE_MAX - left) return 0;
  *total = left + right;
  return 1;
}

static int typed_ranges_overlap(size_t left_offset, size_t left_size, size_t right_offset,
                                size_t right_size) {
  size_t left_end;
  size_t right_end;
  if (left_size == 0 || right_size == 0) return 0;
  if (!typed_add_fits(left_offset, left_size, &left_end) ||
      !typed_add_fits(right_offset, right_size, &right_end))
    return 1;
  return left_offset < right_end && right_offset < left_end;
}
""",
    "range helper",
)

source = replace_once(
    source,
    """static int typed_type_has_tail(const TbeTypedType *type) {
""",
    """static int typed_kind_owns_storage(TbeTypedKind kind) {
  return kind == TBE_TYPED_STRING || kind == TBE_TYPED_BYTES || kind == TBE_TYPED_OBJECT ||
         kind == TBE_TYPED_LIST || kind == TBE_TYPED_SET || kind == TBE_TYPED_MAP;
}

static int typed_field_owns_storage(const TbeTypedField *field) {
  if (field == NULL) return 0;
  if (field->kind == TBE_TYPED_FIXED_ARRAY) return typed_kind_owns_storage(field->element_kind);
  return typed_kind_owns_storage(field->kind);
}

static int typed_type_has_tail(const TbeTypedType *type) {
""",
    "ownership helpers",
)

source = replace_once(
    source,
    """    size_t host_extent;
    const TbeTypedType *nested_type = NULL;
""",
    """    size_t host_extent;
    size_t j;
    const TbeTypedType *nested_type = NULL;
""",
    "descriptor loop locals",
)

source = replace_once(
    source,
    """    if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0 &&
        (type->presence_size == 0 || field->optional_bit / 8u >= type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed optional bit exceeds the presence bitmap");
    if (field->kind == TBE_TYPED_OBJECT) {
""",
    """    if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0 &&
        (type->presence_size == 0 || field->optional_bit / 8u >= type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed optional bit exceeds the presence bitmap");
    if (typed_field_owns_storage(field) && type->presence_size != 0 &&
        typed_ranges_overlap(field->offset, host_extent, type->presence_offset,
                             type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed owning field overlaps the presence bitmap");
    for (j = 0; j < i; ++j) {
      const TbeTypedField *previous = &type->fields[j];
      size_t previous_extent;
      if (!typed_field_owns_storage(field) && !typed_field_owns_storage(previous)) continue;
      if (!typed_field_host_extent(previous, &previous_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, previous->name,
                           "Typed field has invalid host storage");
      if (typed_ranges_overlap(field->offset, host_extent, previous->offset, previous_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed host field storage overlaps owning storage");
    }
    if (field->kind == TBE_TYPED_OBJECT) {
""",
    "host overlap validation",
)

source = replace_once(
    source,
    """      if (field->map_value_kind == TBE_TYPED_OBJECT) nested_type = field->map_value_type;
""",
    """      if (typed_ranges_overlap(field->map_key_offset, sizeof(tstr), field->map_value_offset,
                               value_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed map key and value storage overlap");
      if (field->map_value_kind == TBE_TYPED_OBJECT) nested_type = field->map_value_type;
""",
    "map overlap validation",
)

source = replace_once(
    source,
    """  DataBindStatus status = typed_validate_descriptor_at(type, depth, error);
  if (status != DATA_BIND_OK) return status;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    size_t wire_extent;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0 &&
        (!typed_field_wire_extent(field, &wire_extent) ||
         !typed_size_fits(field->wire_offset, wire_extent, type->fixed_block_size)))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field exceeds the fixed wire block");
""",
    """  DataBindStatus status = typed_validate_descriptor_at(type, depth, error);
  if (status != DATA_BIND_OK) return status;
  if (type->presence_size > type->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed presence bitmap exceeds the fixed wire block");
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    size_t wire_extent;
    size_t j;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0) {
      if (!typed_field_wire_extent(field, &wire_extent) ||
          !typed_size_fits(field->wire_offset, wire_extent, type->fixed_block_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed field exceeds the fixed wire block");
      if (field->wire_size != wire_extent)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed declared wire size does not match the field layout");
      if (typed_ranges_overlap(field->wire_offset, wire_extent, 0u, type->presence_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed field overlaps the wire presence bitmap");
      for (j = 0; j < i; ++j) {
        const TbeTypedField *previous = &type->fields[j];
        size_t previous_extent;
        if ((previous->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
        if (!typed_field_wire_extent(previous, &previous_extent))
          return typed_error(error, DATA_BIND_ERR_SCHEMA, previous->name,
                             "Typed field has invalid wire storage");
        if (typed_ranges_overlap(field->wire_offset, wire_extent, previous->wire_offset,
                                 previous_extent))
          return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                             "Typed fixed wire fields overlap");
      }
    }
""",
    "wire overlap validation",
)

header = replace_once(
    header,
    """typedef struct TbeTypedField {
""",
    """/**
 * Host and wire layout supplied by a typed descriptor.
 *
 * Owning host fields (strings, vectors, maps, objects, and owning fixed arrays)
 * must not overlap any other field storage, and must not overlap the host
 * presence bitmap. Map key/value storage must be disjoint. For binary fields,
 * the wire presence bitmap occupies [0, presence_size), fixed wire ranges must
 * be pairwise disjoint from it and from each other, and wire_size must exactly
 * match the extent derived from the field kind. Invalid layouts are rejected
 * as DATA_BIND_ERR_SCHEMA before direct binary access.
 */
typedef struct TbeTypedField {
""",
    "descriptor contract comment",
)

header = replace_once(
    header,
    """  size_t wire_size;
""",
    """  size_t wire_size; /* Exact derived extent when TBE_TYPED_FIELD_WIRE_OFFSET is set. */
""",
    "wire size comment",
)

source_path.write_text(source, encoding="utf-8")
header_path.write_text(header, encoding="utf-8")
