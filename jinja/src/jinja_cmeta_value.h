#ifndef JINJA_CMETA_VALUE_H
#define JINJA_CMETA_VALUE_H

#include "jinja_cmeta_internal.h"
#include <salts_cmeta_data.h>
#include <stdbool.h>

/* Either a render-local serial or a stable source address and type. Borrowed
 * sources and compiled metadata remain immutable until render cleanup. */
typedef struct JINJA_CMETA_IDENTITY {
  size_t serial;
  const void *source;
  const void *type;
} JINJA_CMETA_IDENTITY;

typedef struct JINJA_CMETA_CLOSURE {
  struct JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  struct JINJA_CMETA_CLOSURE *next;
  struct JINJA_CMETA_ACTIVATION *activation;
  struct JINJA_CMETA_CONTEXT *context;
  size_t function;
  int autoescape;
} JINJA_CMETA_CLOSURE;

typedef struct JINJA_CMETA_RANGE {
  int64_t start;
  int64_t stop;
  int64_t step;
  uint64_t count;
  size_t identity;
} JINJA_CMETA_RANGE;

typedef struct JINJA_CMETA_LOOP_STATE {
  struct JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  struct JINJA_CMETA_CONTEXT *template_context;
  struct JINJA_CMETA_ACTIVATION *activation;
  size_t lexical_scope;
  struct JINJA_CMETA_LOOP_STATE *next;
  struct JINJA_CMETA_NODE *source;
  size_t opener;
  size_t cursor;
  int autoescape;
  struct JINJA_CMETA_NODE *context;
  size_t recursive_depth;
} JINJA_CMETA_LOOP_STATE;

typedef struct JINJA_CMETA_NODE {
  struct JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  struct JINJA_CMETA_CONTEXT *template_context;
  JINJA_CMETA_IDENTITY value_identity;
  /* A value's receiver survives being used as an element of another loop. */
  struct JINJA_CMETA_NODE *loop_receiver;
  JINJA_CMETA_CLOSURE *closure;
  struct JINJA_CMETA_HELPER *helper;
  const JINJA_CMETA_CALLABLE *host_callable;
  JINJA_CMETA_EXPRESSION_KIND callable_kind;
  JINJA_CMETA_LOOP_STATE *loop_state;
  struct JINJA_CMETA_VALUE *namespace_dict;
  vstr loop_alias;
  struct JINJA_CMETA_VALUE *iterator;
  struct JINJA_CMETA_VALUE *iterator_cache;
  size_t iterator_cached_count;
  size_t iterator_cache_capacity;
  int iterator_initialized;
  int iterator_done;
  JINJA_CMETA_RANGE range;
  const void *object;
  const cmeta_data_desc *desc;
  struct JINJA_CMETA_NODE *parent;
  bool owned_bool;
  int64_t owned_integer;
  double owned_float;
  vstr owned_string;
  int string_safe;
  JINJA_CMETA_SEQUENCE_VIEW owned_sequence;
  size_t first_collection_item;
  size_t collection_item_count;
  const struct JINJA_CMETA_VALUE *collection_values;
  size_t loop_index;
  size_t loop_length;
  struct JINJA_CMETA_NODE *loop_sequence;
  /* Render-owned sequence publishes only real iterations here, never lookahead. */
  struct JINJA_CMETA_NODE *loop_current;
  struct JINJA_CMETA_NODE *loop_previous;
  struct JINJA_CMETA_NODE *loop_peek;
  size_t loop_next_index;
  size_t loop_reported_length;
  int loop_length_known;
  size_t changed_value_offset;
  size_t changed_value_count;
  int has_loop_info;
  int is_loop_object;
  int is_missing;
  int is_undefined;
  int is_none;
  int changed_initialized;
  JINJA_CMETA_EXPRESSION_KIND expression_kind;
} JINJA_CMETA_NODE;

typedef enum JINJA_CMETA_VALUE_KIND {
  JINJA_CMETA_VALUE_UNDEFINED,
  JINJA_CMETA_VALUE_NONE,
  JINJA_CMETA_VALUE_BOOL,
  JINJA_CMETA_VALUE_INTEGER,
  JINJA_CMETA_VALUE_FLOAT,
  JINJA_CMETA_VALUE_STRING,
  JINJA_CMETA_VALUE_NODE,
  JINJA_CMETA_VALUE_LIST,
  JINJA_CMETA_VALUE_TUPLE,
  JINJA_CMETA_VALUE_DICT,
  JINJA_CMETA_VALUE_RANGE,
  JINJA_CMETA_VALUE_ITERATOR,
  JINJA_CMETA_VALUE_NAMESPACE,
  JINJA_CMETA_VALUE_CYCLER,
  JINJA_CMETA_VALUE_JOINER,
  JINJA_CMETA_VALUE_CALLABLE,
  JINJA_CMETA_VALUE_LOOP,
  JINJA_CMETA_VALUE_MISSING,
  JINJA_CMETA_VALUE_MODULE,
  JINJA_CMETA_VALUE_TEMPLATE
} JINJA_CMETA_VALUE_KIND;

typedef enum JINJA_CMETA_ITERATOR_KIND {
  JINJA_CMETA_ITERATOR_LOOP,
  JINJA_CMETA_ITERATOR_ITEMS,
  JINJA_CMETA_ITERATOR_REVERSE,
  JINJA_CMETA_ITERATOR_BATCH,
  JINJA_CMETA_ITERATOR_SLICE,
  JINJA_CMETA_ITERATOR_TRANSFORM
} JINJA_CMETA_ITERATOR_KIND;

typedef struct JINJA_CMETA_VALUE {
  struct JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  struct JINJA_CMETA_CONTEXT *template_context;
  JINJA_CMETA_IDENTITY identity;
  struct JINJA_CMETA_BATCH *batch;
  struct JINJA_CMETA_SLICER *slicer;
  struct JINJA_CMETA_TRANSFORM *transform;
  JINJA_CMETA_NODE *loop;
  JINJA_CMETA_CLOSURE *closure;
  struct JINJA_CMETA_HELPER *helper;
  JINJA_CMETA_EXPRESSION_KIND callable_kind;
  const JINJA_CMETA_CALLABLE *host_callable;
  struct JINJA_CMETA_VALUE *namespace_dict;
  struct JINJA_CMETA_VALUE *iterator;
  size_t iterator_cursor;
  JINJA_CMETA_RANGE range;
  JINJA_CMETA_VALUE_KIND kind;
  bool boolean;
  int64_t integer;
  double floating;
  vstr string;
  int string_safe;
  JINJA_CMETA_ITERATOR_KIND iterator_kind;
  JINJA_CMETA_NODE node;
  size_t first_collection_item;
  size_t collection_item_count;
  const struct JINJA_CMETA_VALUE *collection_values;
} JINJA_CMETA_VALUE;

/* Render-owned state shared by aliases and bound methods. value is the cycler's
 * immutable options tuple or the joiner's original separator, never call scratch. */
typedef struct JINJA_CMETA_HELPER {
  struct JINJA_CMETA_HELPER *next;
  JINJA_CMETA_VALUE value;
  size_t position;
} JINJA_CMETA_HELPER;

/* Render-owned derived context. Entry pairs borrow immutable names and retain
 * existing VALUE identities in the render's fixed-address value workspace. */
typedef struct JINJA_CMETA_CONTEXT {
  struct JINJA_CMETA_CONTEXT *next;
  struct JINJA_CMETA_TEMPLATE_INSTANCE *owner;
  const JINJA_CMETA_VALUE *entries;
  size_t count;
} JINJA_CMETA_CONTEXT;

typedef struct JINJA_CMETA_ITERATION {
  JINJA_CMETA_VALUE input;
  size_t position, string_cursor, length, max_items;
  int initialized;
} JINJA_CMETA_ITERATION;

typedef struct JINJA_CMETA_BATCH {
  struct JINJA_CMETA_BATCH *next;
  JINJA_CMETA_ITERATION source;
  JINJA_CMETA_VALUE linecount, fill, pending;
  size_t row_limit;
  int has_pending, done;
} JINJA_CMETA_BATCH;

typedef struct JINJA_CMETA_SLICER {
  struct JINJA_CMETA_SLICER *next;
  JINJA_CMETA_VALUE input, columns, fill;
  size_t count, cursor;
  int initialized;
} JINJA_CMETA_SLICER;

#endif
