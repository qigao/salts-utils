#include "idl_contract.h"

#include <string.h>

typedef struct IdlTypeRefCursor {
  const char *current;
  const char *end;
  size_t nodes;
} IdlTypeRefCursor;

static int type_name_start(unsigned char value) {
  return value == '_' || (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z');
}

static int type_name_continue(unsigned char value) {
  return type_name_start(value) || (value >= '0' && value <= '9');
}

static IdlCollectionKind type_constructor(const char *name, size_t length) {
  if (length == sizeof("list") - 1u && memcmp(name, "list", length) == 0)
    return IDL_COLLECTION_LIST;
  if (length == sizeof("set") - 1u && memcmp(name, "set", length) == 0)
    return IDL_COLLECTION_SET;
  if (length == sizeof("map") - 1u && memcmp(name, "map", length) == 0)
    return IDL_COLLECTION_MAP;
  return IDL_COLLECTION_NONE;
}

static int type_ref_read(IdlTypeRefCursor *cursor, size_t depth,
                         IdlTypeRef *out_type) {
  IdlTypeRef result = {0};
  size_t arity;
  size_t index;

  if (depth > IDL_TYPE_REF_MAX_DEPTH ||
      cursor->nodes >= IDL_TYPE_REF_MAX_NODES ||
      cursor->current == cursor->end ||
      !type_name_start((unsigned char)*cursor->current))
    return 0;
  ++cursor->nodes;
  result.name = cursor->current++;
  while (cursor->current != cursor->end &&
         type_name_continue((unsigned char)*cursor->current))
    ++cursor->current;
  result.name_length = (size_t)(cursor->current - result.name);
  if (cursor->current == cursor->end || *cursor->current != '<') {
    *out_type = result;
    return 1;
  }

  result.collection_kind = type_constructor(result.name, result.name_length);
  if (result.collection_kind == IDL_COLLECTION_NONE) return 0;
  arity = result.collection_kind == IDL_COLLECTION_MAP
              ? IDL_TYPE_REF_MAX_ARGUMENTS : 1u;
  ++cursor->current;
  for (index = 0u; index < arity; ++index) {
    IdlTypeRef argument;
    const char *start = cursor->current;
    if (!type_ref_read(cursor, depth + 1u, &argument)) return 0;
    result.arguments[index] = start;
    result.argument_lengths[index] = (size_t)(cursor->current - start);
    if (cursor->current == cursor->end ||
        *cursor->current != (index + 1u == arity ? '>' : ','))
      return 0;
    ++cursor->current;
  }
  result.argument_count = arity;
  *out_type = result;
  return 1;
}

int idl_type_ref_parse(const char *text, size_t length, IdlTypeRef *out_type) {
  IdlTypeRefCursor cursor;
  IdlTypeRef result;
  if (text == NULL || out_type == NULL || length == 0u ||
      length > IDL_TYPE_REF_MAX_BYTES)
    return 0;
  cursor.current = text;
  cursor.end = text + length;
  cursor.nodes = 0u;
  if (!type_ref_read(&cursor, 1u, &result) || cursor.current != cursor.end)
    return 0;
  *out_type = result;
  return 1;
}
