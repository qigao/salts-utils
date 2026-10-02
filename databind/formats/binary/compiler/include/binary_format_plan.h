#ifndef DATABIND_BINARY_FORMAT_PLAN_H
#define DATABIND_BINARY_FORMAT_PLAN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum databind_binary_format_field_plan_kind {
  DATABIND_BINARY_FORMAT_FIELD_FIXED = 0,
  DATABIND_BINARY_FORMAT_FIELD_GROUP = 1,
  DATABIND_BINARY_FORMAT_FIELD_VAR_DATA = 2
} databind_binary_format_field_plan_kind;

enum {
  DATABIND_BINARY_FORMAT_FIELD_OPTIONAL = 1u << 0,
  DATABIND_BINARY_FORMAT_FIELD_NULLABLE = 1u << 1
};

typedef struct databind_binary_format_field_plan {
  char *name;
  databind_binary_format_field_plan_kind kind;
  size_t wire_offset;
  size_t wire_extent;
  size_t child_fixed_block_size;
  size_t tail_prefix_bytes;
  unsigned optional_bit;
  unsigned nullable_bit;
  unsigned flags;
} databind_binary_format_field_plan;

typedef struct databind_binary_format_type_plan {
  char *name;
  size_t fixed_block_size;
  size_t presence_size;
  size_t null_size;
  int wire_big_endian;
  databind_binary_format_field_plan *fields;
  size_t field_count;
} databind_binary_format_type_plan;

typedef struct databind_binary_format_plan {
  databind_binary_format_type_plan *types;
  size_t type_count;
} databind_binary_format_plan;

const databind_binary_format_type_plan *databind_binary_format_plan_find_type(
    const databind_binary_format_plan *plan,
    const char *type_name);

void databind_binary_format_plan_destroy(databind_binary_format_plan *plan);

#ifdef __cplusplus
}
#endif

#endif
