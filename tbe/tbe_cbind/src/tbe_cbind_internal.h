#ifndef TBE_CBIND_INTERNAL_H
#define TBE_CBIND_INTERNAL_H

#include "tbe_cbind/tbe_cbind.h"

#include <stddef.h>

typedef void *(*tbe_cbind_calloc_fn)(void *context, size_t count, size_t size);
typedef void (*tbe_cbind_free_fn)(void *context, void *pointer);

typedef struct tbe_cbind_allocator {
  void *context;
  tbe_cbind_calloc_fn calloc_fn;
  tbe_cbind_free_fn free_fn;
} tbe_cbind_allocator;

typedef struct tbe_cbind_build_context {
  const tbe_cbind_plan_options *options;
  tbe_cbind_plan_error *error;
  tbe_cbind_allocator allocator;
  size_t plan_bytes;
  int plan_limit_hit;
} tbe_cbind_build_context;

typedef enum tbe_cbind_semantic_kind {
  TBE_CBIND_SEMANTIC_INT32,
  TBE_CBIND_SEMANTIC_INT64,
  TBE_CBIND_SEMANTIC_UINT64,
  TBE_CBIND_SEMANTIC_FLOAT,
  TBE_CBIND_SEMANTIC_DOUBLE,
  TBE_CBIND_SEMANTIC_STRING,
  TBE_CBIND_SEMANTIC_RECORD
} tbe_cbind_semantic_kind;

struct tbe_cbind_semantic_type;

typedef struct tbe_cbind_semantic_field {
  char *name;
  char *semantic_name;
  char *native_name;
  char *type_name;
  tbe_cbind_semantic_kind kind;
  struct tbe_cbind_semantic_type *record_type;
} tbe_cbind_semantic_field;

typedef struct tbe_cbind_semantic_type {
  char *name;
  tbe_cbind_semantic_field *fields;
  size_t field_count;
  unsigned char visit_state;
} tbe_cbind_semantic_type;

typedef struct tbe_cbind_schema_model {
  tbe_cbind_allocator allocator;
  tbe_cbind_semantic_type *types;
  size_t type_count;
  tbe_cbind_semantic_type **type_slots;
  size_t type_slot_count;
  tbe_cbind_semantic_type *root;
} tbe_cbind_schema_model;

typedef struct tbe_cbind_native_binding {
  const cmeta_field_desc *layout_field;
  const cmeta_data_field_desc *data_field;
} tbe_cbind_native_binding;

typedef struct tbe_cbind_plan_node {
  const cmeta_data_desc *native_shape;
  cmeta_field_desc *layout_fields;
  cmeta_data_field_desc *data_fields;
  cmeta_struct_desc layout;
  cmeta_data_struct_shape shape;
  cmeta_data_desc data;
  unsigned char build_state;
} tbe_cbind_plan_node;

struct tbe_cbind_plan {
  tbe_cbind_allocator allocator;
  tbe_cbind_plan_node *nodes;
  size_t node_count;
  const cmeta_data_desc *shape;
  uint32_t state;
};

enum { TBE_CBIND_PLAN_READY = UINT32_C(0x54424342) };

int tbe_cbind_size_add(size_t left, size_t right, size_t *out);
int tbe_cbind_size_mul(size_t left, size_t right, size_t *out);
void *tbe_cbind_alloc_array(tbe_cbind_build_context *context, size_t count,
                            size_t size);
void *tbe_cbind_plan_alloc_array(tbe_cbind_build_context *context,
                                 size_t count, size_t size);
void tbe_cbind_free(tbe_cbind_build_context *context, void *pointer);
char *tbe_cbind_strdup(tbe_cbind_build_context *context, const char *text);
tbe_cbind_status tbe_cbind_set_error(
    tbe_cbind_build_context *context, tbe_cbind_status status,
    tbe_cbind_error_phase phase, size_t field_index, cmeta_status target_status,
    const char *path, const char *message);

tbe_cbind_status tbe_cbind_schema_model_build(
    tbe_cbind_build_context *context, const char *schema_text,
    size_t schema_size, const char *type_name,
    tbe_cbind_schema_model **out_model);
void tbe_cbind_schema_model_destroy(tbe_cbind_schema_model *model);

int tbe_cbind_c_identifier_valid(const char *name);
tbe_cbind_status tbe_cbind_native_bind_record(
    tbe_cbind_build_context *context, const tbe_cbind_semantic_type *semantic,
    const cmeta_data_desc *native_shape, tbe_cbind_native_binding *bindings);
tbe_cbind_status tbe_cbind_plan_build(
    tbe_cbind_build_context *context, const tbe_cbind_schema_model *model,
    const cmeta_data_desc *native_shape, tbe_cbind_plan **out);
void tbe_cbind_plan_release(tbe_cbind_plan *plan);

tbe_cbind_status tbe_cbind_plan_create_from_text_with_allocator(
    const char *schema_text, size_t schema_size, const char *type_name,
    size_t type_name_size, const cmeta_data_desc *native_shape,
    const tbe_cbind_plan_options *options, tbe_cbind_plan **out,
    tbe_cbind_plan_error *error, const tbe_cbind_allocator *allocator);

#endif /* TBE_CBIND_INTERNAL_H */
