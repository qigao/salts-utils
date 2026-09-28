#ifndef DATABIND_TBE_FORMAT_PLAN_H
#define DATABIND_TBE_FORMAT_PLAN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum databind_tbe_field_plan_kind {
  DATABIND_TBE_FIELD_FIXED = 0,
  DATABIND_TBE_FIELD_GROUP = 1,
  DATABIND_TBE_FIELD_VAR_DATA = 2
} databind_tbe_field_plan_kind;

enum {
  DATABIND_TBE_FIELD_OPTIONAL = 1u << 0,
  DATABIND_TBE_FIELD_NULLABLE = 1u << 1
};

typedef struct databind_tbe_field_plan {
  char *name;
  databind_tbe_field_plan_kind kind;
  size_t wire_offset;
  size_t wire_extent;
  size_t child_fixed_block_size;
  size_t tail_prefix_bytes;
  unsigned optional_bit;
  unsigned nullable_bit;
  unsigned flags;
} databind_tbe_field_plan;

typedef struct databind_tbe_type_plan {
  char *name;
  size_t fixed_block_size;
  size_t presence_size;
  size_t null_size;
  int wire_big_endian;
  databind_tbe_field_plan *fields;
  size_t field_count;
} databind_tbe_type_plan;

typedef struct databind_tbe_format_plan {
  databind_tbe_type_plan *types;
  size_t type_count;
} databind_tbe_format_plan;

const databind_tbe_type_plan *databind_tbe_format_plan_find_type(
    const databind_tbe_format_plan *plan,
    const char *type_name);

void databind_tbe_format_plan_destroy(databind_tbe_format_plan *plan);

#ifdef __cplusplus
}
#endif

#endif
