#include "jinja_cmeta_internal.h"

#include <time.h>
#include "jinja_cmeta_runtime_internal.h"
#include "jinja_cmeta_values.h"
#include "jinja_cmeta_text.h"
#include "jinja_cmeta_value.h"
#include "jinja_cmeta_cells.h"
#include "jinja_cmeta_environment.h"
#include "jinja_float.h"
#include "parser/jinja_text_lexer.h"

#include <salts_cmeta_data.h>
#include <salts_unicode.h>
#include <tstr.h>

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Unsigned arithmetic covers the full signed interval without signed overflow. */
static int64_t jinja_range_item(const JINJA_CMETA_RANGE *range, uint64_t index) {
  uint64_t bits = (uint64_t)range->start + index * (uint64_t)range->step;
  if (bits <= (uint64_t)INT64_MAX) return (int64_t)bits;
  return INT64_MIN + (int64_t)(bits - ((uint64_t)INT64_MAX + UINT64_C(1)));
}



#define JINJA_CMETA_DECIMAL_RADIX 10u
enum { JINJA_CMETA_CONTEXT_ENTRY_WIDTH = 2u };

typedef enum JINJA_CMETA_SCALAR_KIND {
  JINJA_CMETA_SCALAR_UNDEFINED,
  JINJA_CMETA_SCALAR_NONE,
  JINJA_CMETA_SCALAR_BOOL,
  JINJA_CMETA_SCALAR_SINT,
  JINJA_CMETA_SCALAR_UINT,
  JINJA_CMETA_SCALAR_FLOAT,
  JINJA_CMETA_SCALAR_STRING
} JINJA_CMETA_SCALAR_KIND;

typedef struct JINJA_CMETA_SCALAR {
  JINJA_CMETA_SCALAR_KIND kind;
  bool boolean;
  int64_t sint;
  uint64_t uint;
  double floating;
  const unsigned char *data;
  size_t size;
} JINJA_CMETA_SCALAR;



typedef struct JINJA_CMETA_RECURSIVE_INPUT {
  const JINJA_CMETA_VALUE *value;
  const JINJA_CMETA_LOOP_STATE *caller;
} JINJA_CMETA_RECURSIVE_INPUT;

typedef struct JINJA_CMETA_BINDING {
  vstr name;
  JINJA_CMETA_VALUE value;
  const JINJA_CMETA_INSTRUCTION *attribute;
} JINJA_CMETA_BINDING;

typedef enum JINJA_CMETA_MODULE_STATE {
  JINJA_CMETA_MODULE_NONE,
  JINJA_CMETA_MODULE_INITIALIZING,
  JINJA_CMETA_MODULE_READY,
  JINJA_CMETA_MODULE_FAILED
} JINJA_CMETA_MODULE_STATE;

/* Loaded metadata remains alive until all render-local borrowers are gone. */
typedef struct JINJA_CMETA_TEMPLATE_INSTANCE {
  struct JINJA_CMETA_TEMPLATE_INSTANCE *next;
  const JINJA_CMETA_TEMPLATE *templ;
  JINJA_CMETA_CONTEXT root_context;
  JINJA_CMETA_ACTIVATION *root_activation;
  struct JINJA_CMETA_TEMPLATE_INSTANCE *parent;
  struct JINJA_CMETA_TEMPLATE_INSTANCE *chain_root;
  vstr module_body;
  JINJA_CMETA_VALUE module_value;
  JINJA_CMETA_MODULE_STATE module_state;
  int cacheable;
  int root_visible;
} JINJA_CMETA_TEMPLATE_INSTANCE;

typedef struct JINJA_CMETA_SCOPE {
  JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  JINJA_CMETA_CONTEXT *template_context;
  size_t first_binding;
  size_t lexical_scope;
  size_t clear_scope;
  JINJA_CMETA_ACTIVATION *activation;
} JINJA_CMETA_SCOPE;

typedef struct JINJA_CMETA_REPR_FRAME {
  const struct JINJA_CMETA_REPR_FRAME *parent;
  const JINJA_CMETA_VALUE *value;
} JINJA_CMETA_REPR_FRAME;

/* One single-threaded owner for the complete render, including every include.
 * Fixed-address payloads and cumulative budgets survive instance switches. */
typedef struct JINJA_CMETA_RENDER_STATE {
  JINJA_CMETA_CONTEXT *contexts;
  size_t context_count;
  JINJA_CMETA_CELL_STORE cells;
  JINJA_CMETA_MEMORY *memory;
  JINJA_CMETA_CLOSURE *closures;
  size_t closure_count;
  size_t call_depth;
  size_t range_identity;
  size_t value_identity;
  JINJA_CMETA_LOOP_STATE *loop_states;
  unsigned max_render_depth;
  size_t max_value_visits;
  size_t value_visits;
  unsigned max_value_depth;
  unsigned value_depth;
  const char *value_limit_error;
  size_t active_expression_depth;
  JINJA_CMETA_VALUE *call_arguments;
  size_t call_argument_count;
  size_t *error_offset;
  const JINJA_CMETA_RENDERER *renderer;
  void *renderer_data;
  JINJA_CMETA_NODE *nodes;
  JINJA_CMETA_VALUE *changed_values;
  JINJA_CMETA_VALUE_STORE values;
  vstr *bindings;
  size_t namespace_count;
  JINJA_CMETA_HELPER *helpers;
  size_t helper_count;
  JINJA_CMETA_TEXT *capture_buffers;
  size_t capture_count;
  size_t capture_bytes;
  size_t collection_value_count;
  JINJA_CMETA_BATCH *batches;
  size_t batch_count;
  JINJA_CMETA_SLICER *slicers;
  size_t slicer_count;
  struct JINJA_CMETA_TRANSFORM *transforms;
  size_t transform_count;
  unsigned iterator_depth;
  char *slice_bytes;
  size_t slice_byte_count;
  size_t pending_string_bytes;
  size_t node_count;
  size_t node_capacity;
  size_t changed_value_count;
  size_t changed_value_capacity;
  size_t max_string_bytes;
  JINJA_CMETA_STATUS status;
  JINJA_CMETA_TEMPLATE_INSTANCE *instances;
  size_t loaded_templates;
  size_t loaded_source_bytes;
  JINJA_CMETA_ERROR *error;
} JINJA_CMETA_RENDER_STATE;

typedef struct JINJA_CMETA_OUTPUT_GUARD {
  struct JINJA_CMETA_OUTPUT_GUARD *previous;
  size_t capture_depth;
  int suppress;
} JINJA_CMETA_OUTPUT_GUARD;

typedef struct JINJA_CMETA_PROVIDER {
  JINJA_CMETA_RENDER_STATE shared;
  const JINJA_CMETA_CLOSURE *active_block;
  JINJA_CMETA_OUTPUT_GUARD *output_guard;
  JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  JINJA_CMETA_CONTEXT *context;
  JINJA_CMETA_ACTIVATION *activation;
  size_t lexical_scope;
  const JINJA_CMETA_FUNCTION *default_function;
  size_t default_parameter;
  JINJA_CMETA_CLOSURE *caller;
  size_t caller_expression;
  size_t binding_count;
  const JINJA_CMETA_REPR_FRAME *repr_frame;
  uint64_t random_state;
  JINJA_CMETA_SCOPE scopes[JINJA_CMETA_MAX_BLOCK_DEPTH];
  size_t scope_depth;
  size_t capture_stack[JINJA_CMETA_MAX_BLOCK_DEPTH];
  size_t capture_depth;
  JINJA_CMETA_VALUE capture_value;
  int capture_value_active;
  int autoescape;
  int strict_undefined;
  const JINJA_CMETA_INSTRUCTION *pending_control;
} JINJA_CMETA_PROVIDER;

static void *jinja_provider_allocate(JINJA_CMETA_PROVIDER *provider, size_t count, size_t width) {
  void *data = NULL;
  const JINJA_CMETA_STATUS status = jinja_cmeta_memory_new(provider->shared.memory, count, width, &data);
  if (status != JINJA_CMETA_OK) provider->shared.status = status;
  return data;
}

static void *jinja_provider_zero(JINJA_CMETA_PROVIDER *provider, size_t count, size_t width) {
  void *data = jinja_provider_allocate(provider, count, width);
  if (data != NULL) memset(data, 0, count * width);
  return data;
}

static void jinja_record_render_error(JINJA_CMETA_PROVIDER *provider, JINJA_CMETA_STATUS status) {
  JINJA_CMETA_ERROR *error = provider->shared.error;
  if (status == JINJA_CMETA_OK || error->status != JINJA_CMETA_OK) return;
  jinja_cmeta_error_set(error, status, *provider->shared.error_offset,
      status == JINJA_CMETA_ERR_CAPACITY && provider->shared.value_limit_error != NULL
          ? provider->shared.value_limit_error
      : status == JINJA_CMETA_ERR_CAPACITY ? "render workspace or value limit exceeded"
      : status == JINJA_CMETA_ERR_LOADER ? "include requires a template environment and loader"
      : "template rendering failed");
  jinja_cmeta_error_name(error, provider->instance->templ->name);
}
typedef struct JINJA_CMETA_STRING_OUTPUT {
  JINJA_CMETA_PROVIDER *provider;
  JINJA_CMETA_TEXT bytes;
} JINJA_CMETA_STRING_OUTPUT;

static const char jinja_template_reference_repr[] = "<TemplateReference None>";

static JINJA_CMETA_STATUS jinja_block_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TEMPLATE_INSTANCE *start, vstr name,
    JINJA_CMETA_CONTEXT *context, JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_super_value(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CLOSURE *block, JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_module_export(JINJA_CMETA_TEMPLATE_INSTANCE *instance,
    vstr name, JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_capitalize_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result);
static JINJA_CMETA_STATUS jinja_title_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result);

static int jinja_output_suppressed(const JINJA_CMETA_PROVIDER *provider) {
  for (const JINJA_CMETA_OUTPUT_GUARD *guard = provider->output_guard;
       guard != NULL; guard = guard->previous)
    if (guard->suppress && guard->capture_depth == provider->capture_depth) return 1;
  return 0;
}

static JINJA_CMETA_STATUS jinja_scope_initialize(JINJA_CMETA_PROVIDER *provider,
    size_t scope, JINJA_CMETA_ACTIVATION *parent);
static JINJA_CMETA_STATUS jinja_function_value(JINJA_CMETA_PROVIDER *provider,
    size_t function, JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_scope_at(JINJA_CMETA_PROVIDER *provider,
    size_t pc, JINJA_CMETA_SCOPE_PART part, JINJA_CMETA_ACTIVATION *parent);

static const JINJA_CMETA_CELL_BINDING *jinja_lexical_binding(
    const JINJA_CMETA_PROVIDER *provider, vstr name, int local) {
  size_t scope = provider->lexical_scope;
  while (scope != SIZE_MAX) {
    const JINJA_CMETA_LEXICAL_SCOPE *frame = &provider->instance->templ->lexical_scopes[scope];
    for (size_t i = 0u; i < frame->binding_count; ++i) {
      const JINJA_CMETA_CELL_BINDING *binding = &provider->instance->templ->cell_bindings[frame->first_binding + i];
      vstr candidate = provider->instance->templ->cells[binding->cell].name;
      if (candidate.len == name.len && memcmp(candidate.data, name.data, name.len) == 0) return binding;
    }
    if (local) break;
    scope = frame->parent;
  }
  return NULL;
}

/* Cells are the sole value owners. The render-owned name ledger only accounts
 * active writes against max_nodes; scope exit restores its count, not payloads.
 * Ledger admission is O(active bindings * name length), without reallocations. */
static int jinja_parameter_needs_missing_guard(const JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CELL_BINDING *binding) {
  if (binding->load != JINJA_CMETA_CELL_ARGUMENT) return 1;
  const JINJA_CMETA_FUNCTION *function = provider->default_function;
  const JINJA_CMETA_CELL *cell = &provider->instance->templ->cells[binding->cell];
  if (function == NULL || cell->owner != function->scope) return 0;
  for (size_t i = provider->default_parameter; i < function->parameter_count; ++i) {
    const JINJA_CMETA_PARAMETER *parameter = &provider->instance->templ->parameters[function->first_parameter + i];
    if (cell->name.len == parameter->name_length &&
        memcmp(cell->name.data, provider->instance->templ->program_strings + parameter->name_offset,
               parameter->name_length) == 0) return 1;
  }
  return 0;
}

static const JINJA_CMETA_VALUE *jinja_binding_find(JINJA_CMETA_PROVIDER *provider, vstr name) {
  static const JINJA_CMETA_VALUE missing = {.kind = JINJA_CMETA_VALUE_MISSING};
  static const JINJA_CMETA_VALUE undefined = {.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (provider->activation != NULL) {
    const JINJA_CMETA_CELL_BINDING *binding = jinja_lexical_binding(provider, name, 0);
    JINJA_CMETA_CELL_VALUE *cell;
    if (binding == NULL) return NULL;
    JINJA_CMETA_STATUS status = jinja_cmeta_activation_cell(provider->activation, binding->cell, &cell);
    if (status != JINJA_CMETA_OK) {
      provider->shared.status = status;
      return NULL;
    }
    if (!cell->bound || cell->value.kind == JINJA_CMETA_VALUE_MISSING)
      return jinja_parameter_needs_missing_guard(provider, binding) ? &undefined : &missing;
    return &cell->value;
  }
  return NULL;
}

static JINJA_CMETA_STATUS jinja_binding_write(JINJA_CMETA_PROVIDER *provider, vstr name,
                                               const JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_CELL_VALUE *cell;
  if (provider->activation == NULL) return JINJA_CMETA_ERR_METADATA;
  {
    const JINJA_CMETA_CELL_BINDING *binding = jinja_lexical_binding(provider, name, 1);
    if (binding == NULL) return JINJA_CMETA_ERR_METADATA;
    JINJA_CMETA_STATUS status = jinja_cmeta_activation_cell(provider->activation, binding->cell, &cell);
    if (status != JINJA_CMETA_OK) return status;
  }
  size_t first = provider->scope_depth == 0u ? 0u : provider->scopes[provider->scope_depth - 1u].first_binding;
  size_t i = provider->binding_count;
  while (i > first) {
    vstr binding = provider->shared.bindings[--i];
    if (binding.len == name.len && memcmp(binding.data, name.data, name.len) == 0) {
      *cell = (JINJA_CMETA_CELL_VALUE){.bound = 1, .assigned = 1,
        .exported = provider->lexical_scope == 0u && name.len != 0u && name.data[0] != '_',
        .value = *value};
      return JINJA_CMETA_OK;
    }
  }
  if (provider->binding_count == provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  if (provider->shared.bindings == NULL) {
    provider->shared.bindings = (vstr *)jinja_provider_zero(provider, provider->shared.node_capacity,
                                                        sizeof(*provider->shared.bindings));
    if (provider->shared.bindings == NULL) return provider->shared.status;
  }
  /* Active entries account for writes, while cells remain the sole value owner. */
  provider->shared.bindings[provider->binding_count++] = name;
  *cell = (JINJA_CMETA_CELL_VALUE){.bound = 1, .assigned = 1,
        .exported = provider->lexical_scope == 0u && name.len != 0u && name.data[0] != '_',
        .value = *value};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_scope_enter(JINJA_CMETA_PROVIDER *provider) {
  if (provider->scope_depth == JINJA_CMETA_MAX_BLOCK_DEPTH) return JINJA_CMETA_ERR_CAPACITY;
  provider->scopes[provider->scope_depth] =
      (JINJA_CMETA_SCOPE){.first_binding = provider->binding_count,
          .lexical_scope = provider->lexical_scope, .clear_scope = SIZE_MAX, .activation = provider->activation,
          .instance = provider->instance, .template_context = provider->context};
  ++provider->scope_depth;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_scope_leave(JINJA_CMETA_PROVIDER *provider) {
  if (provider->scope_depth == 0u) return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_SCOPE *scope = &provider->scopes[provider->scope_depth - 1u];
  if (scope->clear_scope != SIZE_MAX) {
    JINJA_CMETA_STATUS status = jinja_cmeta_activation_clear(provider->activation, scope->clear_scope);
    if (status != JINJA_CMETA_OK) return status;
  }
  provider->activation = scope->activation;
  provider->instance = scope->instance;
  provider->context = scope->template_context;
  provider->lexical_scope = scope->lexical_scope;
  provider->binding_count = provider->scopes[--provider->scope_depth].first_binding;
  return JINJA_CMETA_OK;
}

static int jinja_value_is_collection(JINJA_CMETA_VALUE_KIND kind) {
  return kind == JINJA_CMETA_VALUE_LIST || kind == JINJA_CMETA_VALUE_TUPLE;
}

static int jinja_value_is_safe(const JINJA_CMETA_VALUE *value) {
  return (value->kind == JINJA_CMETA_VALUE_STRING && value->string_safe) ||
      (value->kind == JINJA_CMETA_VALUE_NODE &&
       value->node.expression_kind == JINJA_CMETA_EXPRESSION_STRING && value->node.string_safe);
}

static int jinja_expression_is_collection(JINJA_CMETA_EXPRESSION_KIND kind) {
  return kind == JINJA_CMETA_EXPRESSION_LIST || kind == JINJA_CMETA_EXPRESSION_TUPLE;
}

static int jinja_value_is_container(JINJA_CMETA_VALUE_KIND kind) {
  return jinja_value_is_collection(kind) || kind == JINJA_CMETA_VALUE_DICT;
}

static int jinja_expression_is_container(JINJA_CMETA_EXPRESSION_KIND kind) {
  return jinja_expression_is_collection(kind) || kind == JINJA_CMETA_EXPRESSION_DICT;
}

typedef enum JINJA_CMETA_NUMBER_KIND {
  JINJA_CMETA_NUMBER_INTEGER,
  JINJA_CMETA_NUMBER_FLOAT
} JINJA_CMETA_NUMBER_KIND;

typedef struct JINJA_CMETA_NUMBER {
  JINJA_CMETA_NUMBER_KIND kind;
  int64_t integer;
  double floating;
} JINJA_CMETA_NUMBER;

static int jinja_truthy(JINJA_CMETA_PROVIDER *provider, const JINJA_CMETA_NODE *node);
static JINJA_CMETA_STATUS jinja_read_float(const JINJA_CMETA_NODE *node, double *out);
static JINJA_CMETA_STATUS jinja_expression_value(JINJA_CMETA_PROVIDER *provider,
                                                 JINJA_CMETA_NODE *context, size_t index,
                                                 size_t depth, JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_dict_unique_count(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *dict, size_t depth,
                                                  size_t *count);
static JINJA_CMETA_NODE *jinja_iteration_child_at(JINJA_CMETA_NODE *node, unsigned index,
                                                 JINJA_CMETA_PROVIDER *provider);
static JINJA_CMETA_STATUS jinja_iterator_cache_until(JINJA_CMETA_PROVIDER *provider,
                                                    JINJA_CMETA_NODE *node, size_t index);
static void jinja_normalize_call_argument(JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_value_identify(JINJA_CMETA_PROVIDER *provider, JINJA_CMETA_VALUE *value);
static int jinja_value_is_string(const JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_resolve_value_path(JINJA_CMETA_PROVIDER *provider,
                                                   JINJA_CMETA_NODE *context, vstr path,
                                                   JINJA_CMETA_VALUE *value);
static JINJA_CMETA_STATUS jinja_iterator_next(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_VALUE *source,
                                            JINJA_CMETA_VALUE *result, int *found);

static const cmeta_type_identity jinja_cmeta_sequence_identity =
    CMETA_TYPE_ID_ATOM_INIT("salts-utils.jinja-cmeta.SequenceView.v1");
static const cmeta_type_desc jinja_cmeta_sequence_type = {"JINJA_CMETA_SEQUENCE_VIEW",
                                                          sizeof(JINJA_CMETA_SEQUENCE_VIEW),
                                                          _Alignof(JINJA_CMETA_SEQUENCE_VIEW),
                                                          CMETA_T_OBJECT,
                                                          NULL,
                                                          NULL,
                                                          &jinja_cmeta_sequence_identity};
static const unsigned char jinja_cmeta_sequence_shape = 1u;
static const cmeta_data_desc jinja_cmeta_sequence_desc = {
    sizeof(cmeta_data_desc),
    CMETA_DATA_DESC_ABI_VERSION,
    "salts-utils.jinja-cmeta.SequenceView.data.v1",
    "Jinja sequence view",
    CMETA_DATA_CUSTOM,
    &jinja_cmeta_sequence_type,
    &jinja_cmeta_sequence_shape,
    NULL,
    NULL,
    NULL};

static const cmeta_type_identity jinja_cmeta_integer_identity =
    CMETA_TYPE_ID_ATOM_INIT("salts-utils.jinja-cmeta.IntegerLiteral.v1");
static const cmeta_type_desc jinja_cmeta_integer_type = {"int64_t",
                                                         sizeof(int64_t),
                                                         _Alignof(int64_t),
                                                         CMETA_T_INTEGER,
                                                         NULL,
                                                         NULL,
                                                         &jinja_cmeta_integer_identity};
static const cmeta_data_integer_shape jinja_cmeta_integer_shape = {
    (uint8_t)(sizeof(int64_t) * CHAR_BIT)};
static const cmeta_data_desc jinja_cmeta_integer_desc = {
    sizeof(cmeta_data_desc),
    CMETA_DATA_DESC_ABI_VERSION,
    "salts-utils.jinja-cmeta.IntegerLiteral.data.v1",
    "Jinja integer literal",
    CMETA_DATA_SINT,
    &jinja_cmeta_integer_type,
    &jinja_cmeta_integer_shape,
    NULL,
    NULL,
    NULL};

static const cmeta_data_buffer_shape jinja_cmeta_vstr_shape = {CMETA_DATA_BUFFER_BORROWED};
static const cmeta_data_desc jinja_cmeta_vstr_desc = {sizeof(cmeta_data_desc),
                                                      CMETA_DATA_DESC_ABI_VERSION,
                                                      "salts.vstr.data.v1",
                                                      "vstr",
                                                      CMETA_DATA_STRING,
                                                      &salts_vstr_cmeta_type,
                                                      &jinja_cmeta_vstr_shape,
                                                      &salts_vstr_cmeta_buffer_ops,
                                                      NULL,
                                                      NULL};

static int jinja_is_sequence_desc(const cmeta_data_desc *desc) {
  const cmeta_type_desc *type;
  if (!cmeta_data_desc_valid(desc) || desc->kind != CMETA_DATA_CUSTOM ||
      strcmp(desc->stable_id, jinja_cmeta_sequence_desc.stable_id) != 0)
    return 0;
  type = desc->storage_type;
  return cmeta_type_equal(type, &jinja_cmeta_sequence_type) &&
         type->kind == jinja_cmeta_sequence_type.kind &&
         type->size == jinja_cmeta_sequence_type.size &&
         type->align == jinja_cmeta_sequence_type.align;
}

const cmeta_data_desc *jinja_cmeta_vstr_data(void) { return &jinja_cmeta_vstr_desc; }

const cmeta_data_desc *jinja_cmeta_sequence_data(void) { return &jinja_cmeta_sequence_desc; }

static void jinja_provider_fail(JINJA_CMETA_PROVIDER *provider, JINJA_CMETA_STATUS status) {
  if (provider->shared.status == JINJA_CMETA_OK) provider->shared.status = status;
}

static JINJA_CMETA_NODE *jinja_provider_reserve(JINJA_CMETA_PROVIDER *provider) {
  if (provider->shared.node_count == provider->shared.node_capacity) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
    return NULL;
  }
  return &provider->shared.nodes[provider->shared.node_count++];
}

static JINJA_CMETA_NODE *jinja_provider_node(JINJA_CMETA_PROVIDER *provider, const void *object,
                                             const cmeta_data_desc *desc,
                                             JINJA_CMETA_NODE *parent) {
  JINJA_CMETA_NODE *node;
  if (object == NULL || !cmeta_data_desc_valid(desc)) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
    return NULL;
  }
  node = jinja_provider_reserve(provider);
  if (node == NULL) return NULL;
  node->object = object;
  node->desc = desc;
  node->parent = parent;
  node->expression_kind = 0;
  return node;
}

static JINJA_CMETA_NODE *jinja_provider_bool_node(JINJA_CMETA_PROVIDER *provider, bool value,
                                                  JINJA_CMETA_NODE *parent) {
  JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
  if (node == NULL) return NULL;
  node->owned_bool = value;
  node->object = &node->owned_bool;
  node->desc = &cmeta_data_bool;
  node->parent = parent;
  node->expression_kind = JINJA_CMETA_EXPRESSION_BOOL;
  return node;
}

static JINJA_CMETA_NODE *jinja_provider_integer_node(JINJA_CMETA_PROVIDER *provider, int64_t value,
                                                     JINJA_CMETA_NODE *parent) {
  JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
  if (node == NULL) return NULL;
  node->owned_integer = value;
  node->object = &node->owned_integer;
  node->desc = &jinja_cmeta_integer_desc;
  node->parent = parent;
  node->expression_kind = JINJA_CMETA_EXPRESSION_INTEGER;
  return node;
}

static JINJA_CMETA_NODE *jinja_provider_float_node(JINJA_CMETA_PROVIDER *provider, double value,
                                                   JINJA_CMETA_NODE *parent) {
  JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
  if (node == NULL) return NULL;
  node->owned_float = value;
  node->object = &node->owned_float;
  node->desc = &cmeta_data_double;
  node->parent = parent;
  node->expression_kind = JINJA_CMETA_EXPRESSION_FLOAT;
  return node;
}

static JINJA_CMETA_NODE *jinja_provider_string_node(JINJA_CMETA_PROVIDER *provider, vstr value,
                                                    JINJA_CMETA_NODE *parent) {
  JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
  if (node == NULL) return NULL;
  node->owned_string = value;
  node->object = &node->owned_string;
  node->desc = &jinja_cmeta_vstr_desc;
  node->parent = parent;
  node->expression_kind = JINJA_CMETA_EXPRESSION_STRING;
  return node;
}

static JINJA_CMETA_NODE *jinja_provider_collection_node(JINJA_CMETA_PROVIDER *provider,
                                                        const JINJA_CMETA_VALUE *value,
                                                        JINJA_CMETA_NODE *parent) {
  JINJA_CMETA_NODE *node;
  size_t visible_count;
  JINJA_CMETA_STATUS status;
  if (provider == NULL || value == NULL || !jinja_value_is_container(value->kind)) {
    if (provider != NULL) jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return NULL;
  }
  visible_count = value->collection_item_count;
  if (value->kind == JINJA_CMETA_VALUE_DICT) {
    status = jinja_dict_unique_count(provider, value, 0u, &visible_count);
    if (status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, status);
      return NULL;
    }
  }
  node = jinja_provider_reserve(provider);
  if (node == NULL) return NULL;
  node->owned_sequence = (JINJA_CMETA_SEQUENCE_VIEW){NULL, visible_count, 0u, NULL};
  node->object = &node->owned_sequence;
  node->desc = &jinja_cmeta_sequence_desc;
  node->parent = parent;
  node->first_collection_item = value->first_collection_item;
  node->collection_item_count = value->collection_item_count;
  node->collection_values = value->collection_values;
  node->expression_kind = value->kind == JINJA_CMETA_VALUE_LIST    ? JINJA_CMETA_EXPRESSION_LIST
                          : value->kind == JINJA_CMETA_VALUE_TUPLE ? JINJA_CMETA_EXPRESSION_TUPLE
                                                                   : JINJA_CMETA_EXPRESSION_DICT;
  return node;
}


static JINJA_CMETA_NODE *jinja_provider_iteration_child(JINJA_CMETA_NODE *child, size_t index,
                                                        size_t length, JINJA_CMETA_NODE *sequence) {
  if (child == NULL || sequence == NULL) return NULL;
  if (index >= length) return NULL;
  child->loop_index = index;
  child->loop_length = length;
  child->loop_sequence = sequence;
  child->loop_alias = sequence->loop_alias;
  child->has_loop_info = 1;
  return child;
}

static int jinja_name_equal(const char *name, size_t size, const char *expected) {
  size_t expected_size;
  if ((size != 0u && name == NULL) || expected == NULL) return 0;
  expected_size = strlen(expected);
  return size == expected_size && memcmp(name, expected, size) == 0;
}

static const int64_t *jinja_range_property(const JINJA_CMETA_RANGE *range, const char *name,
                                           size_t size) {
  if (jinja_name_equal(name, size, "start")) return &range->start;
  if (jinja_name_equal(name, size, "stop")) return &range->stop;
  if (jinja_name_equal(name, size, "step")) return &range->step;
  return NULL;
}

static void *jinja_provider_value_node(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *value, JINJA_CMETA_NODE *context);

static int jinja_range_named_value(const JINJA_CMETA_RANGE *range, vstr name, JINJA_CMETA_VALUE *value) {
  const int64_t *property = jinja_range_property(range, name.data, name.len);
  if (property != NULL) {
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = *property};
    return 1;
  }
  JINJA_CMETA_EXPRESSION_KIND method;
  if (jinja_name_equal(name.data, name.len, "count")) method = JINJA_CMETA_EXPRESSION_RANGE_COUNT;
  else if (jinja_name_equal(name.data, name.len, "index")) method = JINJA_CMETA_EXPRESSION_RANGE_INDEX;
  else return 0;
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .range = *range, .callable_kind = method};
  return 1;
}

static JINJA_CMETA_NODE *jinja_nearest_loop_node(JINJA_CMETA_NODE *context) {
  JINJA_CMETA_NODE *candidate;
  for (candidate = context; candidate != NULL; candidate = candidate->parent)
    if (candidate->has_loop_info && !candidate->is_loop_object) return candidate;
  return NULL;
}

static JINJA_CMETA_NODE *jinja_find_loop_alias(JINJA_CMETA_NODE *context,
                                              const char *name, size_t size) {
  JINJA_CMETA_NODE *candidate;
  for (candidate = context; candidate != NULL; candidate = candidate->parent) {
    if (candidate->has_loop_info && !candidate->is_loop_object &&
        candidate->loop_alias.len == size && size != 0u &&
        memcmp(candidate->loop_alias.data, name, size) == 0)
      return candidate;
  }
  return NULL;
}

/* The physical cursor also counts missing values discarded by lookahead.
 * Retained nodes have stable addresses until the single render is destroyed. */
static JINJA_CMETA_NODE *jinja_loop_fetch(JINJA_CMETA_PROVIDER *provider,
                                        JINJA_CMETA_NODE *sequence) {
  if (sequence->loop_next_index > UINT_MAX || sequence->loop_next_index == SIZE_MAX) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
    return NULL;
  }
  JINJA_CMETA_NODE *child = jinja_iteration_child_at(sequence,
      (unsigned)sequence->loop_next_index, provider);
  if (child != NULL) ++sequence->loop_next_index;
  return child;
}

static JINJA_CMETA_NODE *jinja_loop_peek_node(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_NODE *sequence) {
  if (sequence->loop_peek == NULL) {
    JINJA_CMETA_NODE *child = jinja_loop_fetch(provider, sequence);
    /* Jinja's missing singleton is also its empty lookahead marker. */
    if (child != NULL && !child->is_missing) sequence->loop_peek = child;
  }
  return sequence->loop_peek;
}

static JINJA_CMETA_NODE *jinja_loop_advance(JINJA_CMETA_PROVIDER *provider,
                                          JINJA_CMETA_NODE *sequence) {
  size_t index = 0u;
  if (sequence->loop_current != NULL) {
    if (sequence->loop_current->loop_index >= UINT_MAX) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
      return NULL;
    }
    index = sequence->loop_current->loop_index + 1u;
  }
  JINJA_CMETA_NODE *child = sequence->loop_peek;
  if (child == NULL) child = jinja_loop_fetch(provider, sequence);
  if (child != NULL) {
    sequence->loop_peek = NULL;
    child->loop_index = index;
    sequence->loop_previous = sequence->loop_current;
    sequence->loop_current = child;
  }
  return child;
}

static JINJA_CMETA_NODE *jinja_loop_neighbor_node(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_NODE *loop, int previous) {
  JINJA_CMETA_NODE *neighbor;
  JINJA_CMETA_NODE *source;

  if (provider == NULL || loop == NULL || !loop->has_loop_info || loop->loop_sequence == NULL ||
      loop->loop_length == 0u || loop->loop_index >= loop->loop_length) {
    if (provider != NULL) jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return NULL;
  }
  if (previous) {
    if (loop->loop_index == 0u) return NULL;
    source = loop->loop_sequence->loop_previous;
  } else {
    source = jinja_loop_peek_node(provider, loop->loop_sequence);
  }
  if (source == NULL) return NULL;
  neighbor = jinja_provider_reserve(provider);
  if (neighbor == NULL) return NULL;
  *neighbor = *source;
  neighbor->parent = loop->parent;
  neighbor->loop_index = 0u;
  neighbor->loop_length = 0u;
  neighbor->loop_sequence = NULL;
  neighbor->has_loop_info = 0;
  return neighbor;
}

static JINJA_CMETA_STATUS jinja_loop_length(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_NODE *loop, size_t *length) {
  JINJA_CMETA_NODE *receiver = loop != NULL ? loop->loop_sequence : NULL;
  /* len(LoopContext) is the full source length, not its remaining count.
   * Follow sized receivers without consuming them; only unsized sources drain. */
  for (size_t depth = 0u; depth < provider->shared.node_capacity; ++depth) {
    if (loop == NULL || loop->loop_sequence == NULL) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_NODE *sequence = loop->loop_sequence;
    if (sequence->loop_length_known) {
      *length = sequence->loop_reported_length;
      receiver->loop_reported_length = *length;
      receiver->loop_length_known = 1;
      return JINJA_CMETA_OK;
    }
    int filtered = sequence->loop_state != NULL && sequence->loop_state->source != NULL;
    if (!filtered && sequence->iterator != NULL &&
        sequence->iterator->kind == JINJA_CMETA_VALUE_LOOP &&
        sequence->iterator->iterator_kind == JINJA_CMETA_ITERATOR_LOOP) {
      if (sequence->iterator->loop == NULL) return JINJA_CMETA_ERR_RENDER;
      loop = sequence->iterator->loop->loop_current;
      continue;
    }
    if (filtered || sequence->expression_kind == JINJA_CMETA_EXPRESSION_ITEMS) {
      JINJA_CMETA_STATUS status = jinja_iterator_cache_until(provider, sequence, SIZE_MAX);
      if (status != JINJA_CMETA_OK) return status;
      size_t produced = sequence->loop_current != NULL ? sequence->loop_current->loop_index + 1u : 0u;
      size_t peek = sequence->loop_peek != NULL ? 1u : 0u;
      if (produced > sequence->loop_next_index ||
          peek > sequence->loop_next_index - produced) return JINJA_CMETA_ERR_RENDER;
      size_t discarded = sequence->loop_next_index - produced - peek;
      if (discarded > sequence->iterator_cached_count) return JINJA_CMETA_ERR_RENDER;
      *length = sequence->iterator_cached_count - discarded;
    } else *length = loop->loop_length;
    sequence->loop_reported_length = *length;
    sequence->loop_length_known = 1;
    receiver->loop_reported_length = *length;
    receiver->loop_length_known = 1;
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_CAPACITY;
}

static int jinja_loop_property_node(JINJA_CMETA_PROVIDER *provider,
                                    const JINJA_CMETA_NODE *loop, const char *name, size_t size,
                                    JINJA_CMETA_NODE *result) {
  int boolean = 0;
  int is_boolean = 0;
  size_t integer = 0u;
  size_t length = loop != NULL ? loop->loop_length : 0u;

  if (loop != NULL && loop->loop_sequence != NULL) {
    int needs_length = jinja_name_equal(name, size, "length") ||
                       jinja_name_equal(name, size, "revindex") ||
                       jinja_name_equal(name, size, "revindex0");
    if (needs_length) {
      JINJA_CMETA_STATUS status = jinja_loop_length(provider, loop, &length);
      if (status != JINJA_CMETA_OK) {
        jinja_provider_fail(provider, status);
        return 0;
      }
    }
  }

  if (loop == NULL || result == NULL || !loop->has_loop_info || loop->loop_length == 0u ||
      loop->loop_index >= loop->loop_length)
    return 0;
  if (jinja_name_equal(name, size, "index0")) integer = loop->loop_index;
  else if (jinja_name_equal(name, size, "index")) integer = loop->loop_index + 1u;
  else if (jinja_name_equal(name, size, "revindex0"))
    integer = length - loop->loop_index - 1u;
  else if (jinja_name_equal(name, size, "revindex")) integer = length - loop->loop_index;
  else if (jinja_name_equal(name, size, "length")) integer = length;
  else if (jinja_name_equal(name, size, "depth0") || jinja_name_equal(name, size, "depth")) {
    integer = loop->loop_sequence != NULL && loop->loop_sequence->loop_state != NULL
        ? loop->loop_sequence->loop_state->recursive_depth : 0u;
    if (jinja_name_equal(name, size, "depth")) ++integer;
  }
  else if (jinja_name_equal(name, size, "first")) {
    boolean = loop->loop_index == 0u;
    is_boolean = 1;
  } else if (jinja_name_equal(name, size, "last")) {
    boolean = jinja_loop_peek_node(provider, loop->loop_sequence) == NULL;
    if (provider->shared.status != JINJA_CMETA_OK) return 0;
    is_boolean = 1;
  } else {
    return 0;
  }
  *result = (JINJA_CMETA_NODE){0};
  result->parent = (JINJA_CMETA_NODE *)loop;
  if (is_boolean) {
    result->owned_bool = boolean != 0;
    result->object = &result->owned_bool;
    result->desc = &cmeta_data_bool;
    result->expression_kind = JINJA_CMETA_EXPRESSION_BOOL;
  } else {
    if (integer > (size_t)INT64_MAX) return 0;
    result->owned_integer = (int64_t)integer;
    result->object = &result->owned_integer;
    result->desc = &jinja_cmeta_integer_desc;
    result->expression_kind = JINJA_CMETA_EXPRESSION_INTEGER;
  }
  return 1;
}



static JINJA_CMETA_STATUS jinja_loop_attribute(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *sequence, vstr name, JINJA_CMETA_VALUE *result) {
  if (sequence == NULL || sequence->loop_current == NULL) return JINJA_CMETA_ERR_RENDER;
  const JINJA_CMETA_NODE *loop = sequence->loop_current;
  *result = (JINJA_CMETA_VALUE){0};
  if (vstr_eq(name, vstr_from_cstr("cycle")) || vstr_eq(name, vstr_from_cstr("changed"))) {
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .loop = sequence,
        .callable_kind = vstr_eq(name, vstr_from_cstr("cycle"))
            ? JINJA_CMETA_EXPRESSION_LOOP_CYCLE : JINJA_CMETA_EXPRESSION_LOOP_CHANGED};
    return JINJA_CMETA_OK;
  }
  if (vstr_eq(name, vstr_from_cstr("previtem")) || vstr_eq(name, vstr_from_cstr("nextitem"))) {
    JINJA_CMETA_NODE *neighbor = jinja_loop_neighbor_node(provider, loop,
        vstr_eq(name, vstr_from_cstr("previtem")));
    if (neighbor != NULL) *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NODE, .node = *neighbor};
    return provider->shared.status;
  }
  JINJA_CMETA_NODE property;
  if (jinja_loop_property_node(provider, loop, name.data, name.len, &property)) {
    if (property.expression_kind == JINJA_CMETA_EXPRESSION_BOOL)
      *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_BOOL, .boolean = property.owned_bool};
    else
      *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = property.owned_integer};
  }
  return provider->shared.status;
}

static const cmeta_data_field_desc *jinja_struct_field(const cmeta_data_struct_shape *shape,
                                                       const char *name, size_t name_size) {
  size_t i;
  if (shape == NULL || (name_size != 0u && name == NULL)) return NULL;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    size_t field_size = strlen(field->name);
    if (field_size == name_size && memcmp(field->name, name, name_size) == 0) return field;
  }
  return NULL;
}

static int jinja_child_address(const JINJA_CMETA_NODE *parent, const cmeta_data_field_desc *field,
                               const void **out) {
  size_t parent_size;
  size_t child_size;
  if (parent->desc->storage_type == NULL || field->value == NULL ||
      field->value->storage_type == NULL)
    return 0;
  parent_size = parent->desc->storage_type->size;
  child_size = field->value->storage_type->size;
  if (field->offset > parent_size || child_size > parent_size - field->offset) return 0;
  *out = (const unsigned char *)parent->object + field->offset;
  return 1;
}



static JINJA_CMETA_STATUS jinja_read_signed(const JINJA_CMETA_NODE *node, int64_t *out) {
  const cmeta_data_integer_shape *shape = (const cmeta_data_integer_shape *)node->desc->shape;
  if (shape == NULL || node->desc->storage_type == NULL ||
      shape->bits != node->desc->storage_type->size * CHAR_BIT)
    return JINJA_CMETA_ERR_METADATA;
  switch (shape->bits) {
  case 8u: {
    int8_t value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  case 16u: {
    int16_t value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  case 32u: {
    int32_t value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  case 64u:
    memcpy(out, node->object, sizeof(*out));
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_METADATA;
}

static JINJA_CMETA_STATUS jinja_read_unsigned(const JINJA_CMETA_NODE *node, uint64_t *out) {
  const cmeta_data_integer_shape *shape = (const cmeta_data_integer_shape *)node->desc->shape;
  if (shape == NULL || node->desc->storage_type == NULL ||
      shape->bits != node->desc->storage_type->size * CHAR_BIT)
    return JINJA_CMETA_ERR_METADATA;
  switch (shape->bits) {
  case 8u: {
    uint8_t value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  case 16u: {
    uint16_t value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  case 32u: {
    uint32_t value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  case 64u:
    memcpy(out, node->object, sizeof(*out));
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_METADATA;
}

static JINJA_CMETA_STATUS jinja_resolve_path(JINJA_CMETA_PROVIDER *provider,
                                             JINJA_CMETA_NODE *context, vstr path,
                                             JINJA_CMETA_NODE *result, int *found) {
  JINJA_CMETA_NODE current = {0};
  size_t segment_start = 0u;
  int first = 1;

  if (provider == NULL || context == NULL || result == NULL || found == NULL ||
      !vstr_is_valid(path) || path.len == 0u)
    return JINJA_CMETA_ERR_RENDER;
  *found = 0;
  if (path.len == sizeof(JINJA_CMETA_CURRENT_LOOP_NAME) - 1u &&
      memcmp(path.data, JINJA_CMETA_CURRENT_LOOP_NAME,
             sizeof(JINJA_CMETA_CURRENT_LOOP_NAME) - 1u) == 0) {
    JINJA_CMETA_NODE *loop = jinja_nearest_loop_node(context);
    if (loop != NULL) {
      *result = *loop;
      *found = 1;
    }
    return JINJA_CMETA_OK;
  }
  if (path.len == 1u && path.data[0] == '.') {
    *result = *context;
    *found = 1;
    return JINJA_CMETA_OK;
  }
  if (path.len > 5u && memcmp(path.data, "loop.", 5u) == 0) {
    JINJA_CMETA_NODE *candidate;
    size_t property_end = 5u;
    for (candidate = context; candidate != NULL; candidate = candidate->parent) {
      if (!candidate->has_loop_info || candidate->is_loop_object) continue;
      while (property_end < path.len && path.data[property_end] != '.')
        ++property_end;
      if (jinja_name_equal(path.data + 5u, property_end - 5u, "previtem") ||
          jinja_name_equal(path.data + 5u, property_end - 5u, "nextitem")) {
        JINJA_CMETA_NODE *neighbor = jinja_loop_neighbor_node(
            provider, candidate, jinja_name_equal(path.data + 5u, property_end - 5u, "previtem"));
        if (neighbor == NULL) {
          if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
          return property_end == path.len ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
        }
        current = *neighbor;
      } else if (!jinja_loop_property_node(provider, candidate, path.data + 5u, property_end - 5u,
                                           &current)) {
        return provider->shared.status;
      }
      if (property_end == path.len) {
        *result = current;
        *found = 1;
        return JINJA_CMETA_OK;
      }
      segment_start = property_end + 1u;
      first = 0;
      break;
    }
  }

  while (segment_start < path.len) {
    const cmeta_data_field_desc *field = NULL;
    const void *child = NULL;
    size_t segment_end = segment_start;
    while (segment_end < path.len && path.data[segment_end] != '.')
      ++segment_end;
    if (segment_end == segment_start) return JINJA_CMETA_ERR_RENDER;

    if (first) {
      JINJA_CMETA_NODE *candidate;
      JINJA_CMETA_NODE *binding = jinja_find_loop_alias(
          context, path.data + segment_start, segment_end - segment_start);
      if (binding != NULL) {
        current = *binding;
        if (segment_end == path.len) {
          *result = current;
          *found = 1;
          return JINJA_CMETA_OK;
        }
        segment_start = segment_end + 1u;
        first = 0;
        continue;
      }
      for (candidate = context; candidate != NULL; candidate = candidate->parent) {
        const cmeta_data_struct_shape *shape;
        if (candidate == &provider->shared.nodes[0] && !provider->instance->root_visible) continue;
        if (candidate->expression_kind == JINJA_CMETA_EXPRESSION_CALL) continue;
        if (candidate->expression_kind == JINJA_CMETA_EXPRESSION_RANGE) {
          const int64_t *property = jinja_range_property(
              &candidate->range, path.data + segment_start, segment_end - segment_start);
          if (property != NULL) {
            current = (JINJA_CMETA_NODE){
                .object = property, .desc = &jinja_cmeta_integer_desc, .parent = candidate};
            break;
          }
        }
        if (!cmeta_data_desc_valid(candidate->desc) || candidate->object == NULL)
          return JINJA_CMETA_ERR_METADATA;
        if (candidate->desc->kind != CMETA_DATA_STRUCT) continue;
        shape = (const cmeta_data_struct_shape *)candidate->desc->shape;
        field = jinja_struct_field(shape, path.data + segment_start, segment_end - segment_start);
        if (field == NULL) continue;
        if (!cmeta_data_desc_valid(field->value) || !jinja_child_address(candidate, field, &child))
          return JINJA_CMETA_ERR_METADATA;
        current.object = child;
        current.desc = field->value;
        current.parent = candidate;
        break;
      }
      if (candidate == NULL)
        return segment_end == path.len ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
      first = 0;
    } else {
      const cmeta_data_struct_shape *shape;
      if (current.expression_kind == JINJA_CMETA_EXPRESSION_RANGE) {
        JINJA_CMETA_VALUE member;
        JINJA_CMETA_NODE *value;
        if (!jinja_range_named_value(&current.range,
            vstr_from_buf(path.data + segment_start, segment_end - segment_start), &member))
          return segment_end == path.len ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
        value = jinja_provider_value_node(provider, &member, context);
        if (value == NULL) return provider->shared.status;
        current = *value;
      } else if (current.expression_kind == JINJA_CMETA_EXPRESSION_CALL) {
        return segment_end == path.len ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
      } else {
        if (!cmeta_data_desc_valid(current.desc) || current.object == NULL)
          return JINJA_CMETA_ERR_METADATA;
        if (current.desc->kind != CMETA_DATA_STRUCT)
          return segment_end == path.len ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
        shape = (const cmeta_data_struct_shape *)current.desc->shape;
        field = jinja_struct_field(shape, path.data + segment_start, segment_end - segment_start);
        if (field == NULL)
          return segment_end == path.len ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
        if (!cmeta_data_desc_valid(field->value) || !jinja_child_address(&current, field, &child))
          return JINJA_CMETA_ERR_METADATA;
        current.object = child;
        current.desc = field->value;
      }
    }

    if (segment_end == path.len) {
      *result = current;
      *found = 1;
      return JINJA_CMETA_OK;
    }
    segment_start = segment_end + 1u;
    if (segment_start == path.len) return JINJA_CMETA_ERR_RENDER;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_scalar_from_node(JINJA_CMETA_PROVIDER *provider,
                                                 const JINJA_CMETA_NODE *node,
                                                 JINJA_CMETA_SCALAR *scalar) {
  if (provider == NULL || node == NULL || scalar == NULL || node->object == NULL ||
      !cmeta_data_desc_valid(node->desc))
    return JINJA_CMETA_ERR_METADATA;
  *scalar = (JINJA_CMETA_SCALAR){0};
  switch (node->desc->kind) {
  case CMETA_DATA_BOOL:
    scalar->kind = JINJA_CMETA_SCALAR_BOOL;
    memcpy(&scalar->boolean, node->object, sizeof(scalar->boolean));
    return JINJA_CMETA_OK;
  case CMETA_DATA_SINT:
    scalar->kind = JINJA_CMETA_SCALAR_SINT;
    return jinja_read_signed(node, &scalar->sint);
  case CMETA_DATA_UINT:
    scalar->kind = JINJA_CMETA_SCALAR_UINT;
    return jinja_read_unsigned(node, &scalar->uint);
  case CMETA_DATA_FLOAT:
    scalar->kind = JINJA_CMETA_SCALAR_FLOAT;
    return jinja_read_float(node, &scalar->floating);
  case CMETA_DATA_STRING: {
    cmeta_status status = cmeta_data_buffer_read(
        node->desc, node->object, provider->shared.max_string_bytes, &scalar->data, &scalar->size);
    if (status == CMETA_CAPACITY_EXCEEDED) return JINJA_CMETA_ERR_CAPACITY;
    if (status != CMETA_OK) return JINJA_CMETA_ERR_METADATA;
    scalar->kind = JINJA_CMETA_SCALAR_STRING;
    return JINJA_CMETA_OK;
  }
  default:
    return JINJA_CMETA_ERR_RENDER;
  }
}

static JINJA_CMETA_STATUS jinja_value_from_operand(JINJA_CMETA_PROVIDER *provider,
                                                    JINJA_CMETA_NODE *context,
                                                    const JINJA_CMETA_OPERAND *operand,
                                                    JINJA_CMETA_VALUE *value) {
  if (operand == NULL || value == NULL) return JINJA_CMETA_ERR_RENDER;
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  switch (operand->kind) {
  case JINJA_CMETA_OPERAND_BOOL:
    value->kind = JINJA_CMETA_VALUE_BOOL;
    value->boolean = operand->boolean != 0;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_OPERAND_INTEGER:
    value->kind = JINJA_CMETA_VALUE_INTEGER;
    value->integer = operand->integer;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_OPERAND_FLOAT:
    value->kind = JINJA_CMETA_VALUE_FLOAT;
    value->floating = operand->floating;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_OPERAND_STRING:
    if (!vstr_is_valid(operand->text)) return JINJA_CMETA_ERR_RENDER;
    value->kind = JINJA_CMETA_VALUE_STRING;
    value->string = operand->text;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_OPERAND_PATH:
    return jinja_resolve_value_path(provider, context, operand->text, value);
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_scalar_from_value(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *value,
                                                  JINJA_CMETA_SCALAR *scalar) {
  if (provider == NULL || value == NULL || scalar == NULL) return JINJA_CMETA_ERR_RENDER;
  *scalar = (JINJA_CMETA_SCALAR){0};
  switch (value->kind) {
  case JINJA_CMETA_VALUE_UNDEFINED:
    scalar->kind = JINJA_CMETA_SCALAR_UNDEFINED;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_NONE:
    scalar->kind = JINJA_CMETA_SCALAR_NONE;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_BOOL:
    scalar->kind = JINJA_CMETA_SCALAR_BOOL;
    scalar->boolean = value->boolean;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_INTEGER:
    scalar->kind = JINJA_CMETA_SCALAR_SINT;
    scalar->sint = value->integer;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_FLOAT:
    scalar->kind = JINJA_CMETA_SCALAR_FLOAT;
    scalar->floating = value->floating;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_STRING:
    if (!vstr_is_valid(value->string)) return JINJA_CMETA_ERR_RENDER;
    scalar->kind = JINJA_CMETA_SCALAR_STRING;
    scalar->data = (const unsigned char *)value->string.data;
    scalar->size = value->string.len;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_NODE:
    return jinja_scalar_from_node(provider, &value->node, scalar);
  case JINJA_CMETA_VALUE_LIST:
  case JINJA_CMETA_VALUE_TUPLE:
  case JINJA_CMETA_VALUE_DICT:
    return JINJA_CMETA_ERR_RENDER;
  case JINJA_CMETA_VALUE_RANGE:
  case JINJA_CMETA_VALUE_ITERATOR:
  case JINJA_CMETA_VALUE_NAMESPACE:
  case JINJA_CMETA_VALUE_CYCLER:
  case JINJA_CMETA_VALUE_JOINER:
  case JINJA_CMETA_VALUE_MODULE:
  case JINJA_CMETA_VALUE_TEMPLATE:
  case JINJA_CMETA_VALUE_CALLABLE:
  case JINJA_CMETA_VALUE_LOOP:
  case JINJA_CMETA_VALUE_MISSING:
    return JINJA_CMETA_ERR_RENDER;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static int jinja_scalar_is_numeric(JINJA_CMETA_SCALAR_KIND kind) {
  return kind == JINJA_CMETA_SCALAR_BOOL || kind == JINJA_CMETA_SCALAR_SINT ||
         kind == JINJA_CMETA_SCALAR_UINT || kind == JINJA_CMETA_SCALAR_FLOAT;
}

static int jinja_compare_unsigned(uint64_t left, uint64_t right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

static int jinja_compare_signed(int64_t left, int64_t right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

static int jinja_compare_float_to_integral(double left, const JINJA_CMETA_SCALAR *right,
                                           int *unordered) {
  if (isnan(left)) {
    *unordered = 1;
    return 0;
  }
  if (right->kind == JINJA_CMETA_SCALAR_SINT) {
    const double minimum = -9223372036854775808.0;
    const double upper_bound = 9223372036854775808.0;
    int64_t truncated;
    if (left < minimum) return -1;
    if (left >= upper_bound) return 1;
    truncated = (int64_t)left;
    if (truncated < right->sint) return -1;
    if (truncated > right->sint) return 1;
    return left<(double)truncated ? -1 : left>(double) truncated ? 1 : 0;
  } else {
    const uint64_t right_value = right->kind == JINJA_CMETA_SCALAR_BOOL
                                     ? (right->boolean ? UINT64_C(1) : UINT64_C(0))
                                     : right->uint;
    const double upper_bound = 18446744073709551616.0;
    uint64_t truncated;
    if (left < 0.0) return -1;
    if (left >= upper_bound) return 1;
    truncated = (uint64_t)left;
    if (truncated < right_value) return -1;
    if (truncated > right_value) return 1;
    return left<(double)truncated ? -1 : left>(double) truncated ? 1 : 0;
  }
}

static int jinja_compare_numeric_scalars(const JINJA_CMETA_SCALAR *left,
                                         const JINJA_CMETA_SCALAR *right, int *unordered) {
  const int left_signed = left->kind == JINJA_CMETA_SCALAR_SINT;
  const int right_signed = right->kind == JINJA_CMETA_SCALAR_SINT;
  const uint64_t left_unsigned = left->kind == JINJA_CMETA_SCALAR_BOOL
                                     ? (left->boolean ? UINT64_C(1) : UINT64_C(0))
                                     : left->uint;
  const uint64_t right_unsigned = right->kind == JINJA_CMETA_SCALAR_BOOL
                                      ? (right->boolean ? UINT64_C(1) : UINT64_C(0))
                                      : right->uint;

  *unordered = 0;
  if (left->kind == JINJA_CMETA_SCALAR_FLOAT && right->kind == JINJA_CMETA_SCALAR_FLOAT) {
    if (isnan(left->floating) || isnan(right->floating)) {
      *unordered = 1;
      return 0;
    }
    return left->floating < right->floating ? -1 : left->floating > right->floating ? 1 : 0;
  }
  if (left->kind == JINJA_CMETA_SCALAR_FLOAT)
    return jinja_compare_float_to_integral(left->floating, right, unordered);
  if (right->kind == JINJA_CMETA_SCALAR_FLOAT)
    return -jinja_compare_float_to_integral(right->floating, left, unordered);

  if (left_signed && right_signed) return jinja_compare_signed(left->sint, right->sint);
  if (!left_signed && !right_signed) return jinja_compare_unsigned(left_unsigned, right_unsigned);
  if (left_signed) {
    if (left->sint < 0) return -1;
    return jinja_compare_unsigned((uint64_t)left->sint, right_unsigned);
  }
  if (right->sint < 0) return 1;
  return jinja_compare_unsigned(left_unsigned, (uint64_t)right->sint);
}

static int jinja_compare_buffers(const JINJA_CMETA_SCALAR *left, const JINJA_CMETA_SCALAR *right) {
  size_t common_size = left->size < right->size ? left->size : right->size;
  int ordering = common_size == 0u ? 0 : memcmp(left->data, right->data, common_size);
  if (ordering != 0) return ordering < 0 ? -1 : 1;
  return left->size < right->size ? -1 : left->size > right->size ? 1 : 0;
}

static int jinja_comparison_result(int ordering, JINJA_CMETA_COMPARISON_KIND comparison,
                                   int *result) {
  if (result == NULL) return 0;
  switch (comparison) {
  case JINJA_CMETA_COMPARISON_EQUAL:
    *result = ordering == 0;
    return 1;
  case JINJA_CMETA_COMPARISON_NOT_EQUAL:
    *result = ordering != 0;
    return 1;
  case JINJA_CMETA_COMPARISON_LESS:
    *result = ordering < 0;
    return 1;
  case JINJA_CMETA_COMPARISON_LESS_EQUAL:
    *result = ordering <= 0;
    return 1;
  case JINJA_CMETA_COMPARISON_GREATER:
    *result = ordering > 0;
    return 1;
  case JINJA_CMETA_COMPARISON_GREATER_EQUAL:
    *result = ordering >= 0;
    return 1;
  case JINJA_CMETA_COMPARISON_IN:
  case JINJA_CMETA_COMPARISON_NOT_IN:
    return 0;
  }
  return 0;
}

static JINJA_CMETA_STATUS jinja_compare_scalars(const JINJA_CMETA_SCALAR *left,
                                                const JINJA_CMETA_SCALAR *right,
                                                JINJA_CMETA_COMPARISON_KIND comparison,
                                                int *result) {
  int ordering;
  int unordered = 0;
  const int equality =
      comparison == JINJA_CMETA_COMPARISON_EQUAL || comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;

  if (left == NULL || right == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (left->kind == JINJA_CMETA_SCALAR_UNDEFINED || right->kind == JINJA_CMETA_SCALAR_UNDEFINED ||
      left->kind == JINJA_CMETA_SCALAR_NONE || right->kind == JINJA_CMETA_SCALAR_NONE) {
    if (!equality) return JINJA_CMETA_ERR_RENDER;
    ordering = left->kind == right->kind ? 0 : 1;
  } else if (jinja_scalar_is_numeric(left->kind) && jinja_scalar_is_numeric(right->kind)) {
    ordering = jinja_compare_numeric_scalars(left, right, &unordered);
  } else if (left->kind == right->kind && left->kind == JINJA_CMETA_SCALAR_STRING) {
    ordering = jinja_compare_buffers(left, right);
  } else {
    if (!equality) return JINJA_CMETA_ERR_RENDER;
    ordering = 1;
  }
  if (unordered) {
    *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
    return JINJA_CMETA_OK;
  }
  return jinja_comparison_result(ordering, comparison, result) ? JINJA_CMETA_OK
                                                               : JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_number_from_value(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *value,
                                                  JINJA_CMETA_NUMBER *number) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || value == NULL || number == NULL) return JINJA_CMETA_ERR_RENDER;
  *number = (JINJA_CMETA_NUMBER){0};
  status = jinja_scalar_from_value(provider, value, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  switch (scalar.kind) {
  case JINJA_CMETA_SCALAR_BOOL:
    number->kind = JINJA_CMETA_NUMBER_INTEGER;
    number->integer = scalar.boolean ? INT64_C(1) : INT64_C(0);
    return JINJA_CMETA_OK;
  case JINJA_CMETA_SCALAR_SINT:
    number->kind = JINJA_CMETA_NUMBER_INTEGER;
    number->integer = scalar.sint;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_SCALAR_UINT:
    if (scalar.uint > (uint64_t)INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
    number->kind = JINJA_CMETA_NUMBER_INTEGER;
    number->integer = (int64_t)scalar.uint;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_SCALAR_FLOAT:
    number->kind = JINJA_CMETA_NUMBER_FLOAT;
    number->floating = scalar.floating;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_SCALAR_UNDEFINED:
  case JINJA_CMETA_SCALAR_NONE:
  case JINJA_CMETA_SCALAR_STRING:
    return JINJA_CMETA_ERR_RENDER;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_checked_add(int64_t left, int64_t right, int64_t *result) {
  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))
    return JINJA_CMETA_ERR_CAPACITY;
  *result = left + right;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_checked_subtract(int64_t left, int64_t right, int64_t *result) {
  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  if ((right > 0 && left < INT64_MIN + right) || (right < 0 && left > INT64_MAX + right))
    return JINJA_CMETA_ERR_CAPACITY;
  *result = left - right;
  return JINJA_CMETA_OK;
}

static uint64_t jinja_integer_magnitude(int64_t value) {
  return value < 0 ? (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1) : (uint64_t)value;
}

/* Requires right != 0. Integer quotient/remainder retain the bits lost by
 * converting either operand first. Round once, ties to even, then scale exactly.
 * O(64 + DBL_MANT_DIG) time, O(1) space; at most 115 remainder-bit steps.
 * Magnitudes are <= 2^63, so doubling a remainder < denominator fits uint64.
 * Nonzero int64 ratios are normal binary64 values: no underflow/overflow path. */
static double jinja_integer_true_divide(int64_t left, int64_t right) {
  const uint64_t numerator = jinja_integer_magnitude(left);
  const uint64_t denominator = jinja_integer_magnitude(right);
  const uint64_t exact_limit = UINT64_C(1) << DBL_MANT_DIG;
  uint64_t significand;
  uint64_t remainder;
  int bits = 0;
  int exponent;
  int round_up;

  if (numerator == 0u || (numerator <= exact_limit && denominator <= exact_limit))
    return (double)left / (double)right;
  significand = numerator / denominator;
  remainder = numerator % denominator;
  for (uint64_t remaining = significand; remaining != 0u; remaining >>= 1u) ++bits;
  exponent = bits - 1;
  if (bits == 0) {
    exponent = 0;
    do {
      remainder <<= 1u;
      --exponent;
    } while (remainder < denominator);
    significand = 1u;
    remainder -= denominator;
    bits = 1;
  }
  if (bits > DBL_MANT_DIG) {
    const int discarded = bits - DBL_MANT_DIG;
    const uint64_t halfway = UINT64_C(1) << (discarded - 1);
    const uint64_t low = significand & ((UINT64_C(1) << discarded) - UINT64_C(1));
    significand >>= discarded;
    round_up = low > halfway ||
        (low == halfway && (remainder != 0u || (significand & UINT64_C(1)) != 0u));
  } else {
    for (; bits < DBL_MANT_DIG; ++bits) {
      significand <<= 1u;
      remainder <<= 1u;
      if (remainder >= denominator) {
        remainder -= denominator;
        significand |= UINT64_C(1);
      }
    }
    round_up = remainder > denominator - remainder ||
        (remainder == denominator - remainder && (significand & UINT64_C(1)) != 0u);
  }
  if (round_up) ++significand;
  const double magnitude = ldexp((double)significand, exponent - (DBL_MANT_DIG - 1));
  return (left < 0) != (right < 0) ? -magnitude : magnitude;
}

static JINJA_CMETA_STATUS jinja_checked_multiply(int64_t left, int64_t right, int64_t *result) {
  const int negative = (left < 0) != (right < 0);
  const uint64_t left_magnitude = jinja_integer_magnitude(left);
  const uint64_t right_magnitude = jinja_integer_magnitude(right);
  const uint64_t limit = negative ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
  uint64_t product;

  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (left_magnitude != 0u && right_magnitude > limit / left_magnitude)
    return JINJA_CMETA_ERR_CAPACITY;
  product = left_magnitude * right_magnitude;
  if (!negative) {
    *result = (int64_t)product;
  } else if (product == (uint64_t)INT64_MAX + UINT64_C(1)) {
    *result = INT64_MIN;
  } else {
    *result = -(int64_t)product;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_floor_divide_or_modulo(int64_t left, int64_t right, int modulo,
                                                       int64_t *result) {
  int64_t quotient;
  int64_t remainder;

  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (right == 0) return JINJA_CMETA_ERR_RENDER;
  if (left == INT64_MIN && right == -INT64_C(1)) {
    if (modulo) {
      *result = INT64_C(0);
      return JINJA_CMETA_OK;
    }
    return JINJA_CMETA_ERR_CAPACITY;
  }
  quotient = left / right;
  remainder = left % right;
  if (remainder != 0 && (left < 0) != (right < 0)) {
    --quotient;
    remainder += right;
  }
  *result = modulo ? remainder : quotient;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_checked_power(int64_t base, int64_t exponent, int64_t *result) {
  int64_t accumulator = INT64_C(1);
  JINJA_CMETA_STATUS status;

  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (exponent < 0) return JINJA_CMETA_ERR_RENDER;
  while (exponent != 0) {
    if ((exponent & INT64_C(1)) != 0) {
      status = jinja_checked_multiply(accumulator, base, &accumulator);
      if (status != JINJA_CMETA_OK) return status;
    }
    exponent /= INT64_C(2);
    if (exponent != 0) {
      status = jinja_checked_multiply(base, base, &base);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  *result = accumulator;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_evaluate_arithmetic(JINJA_CMETA_ARITHMETIC_KIND arithmetic,
                                                    int64_t left, int64_t right, int64_t *result) {
  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  switch (arithmetic) {
  case JINJA_CMETA_ARITHMETIC_POSITIVE:
    *result = left;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_NEGATE:
    if (left == INT64_MIN) return JINJA_CMETA_ERR_CAPACITY;
    *result = -left;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_ADD:
    return jinja_checked_add(left, right, result);
  case JINJA_CMETA_ARITHMETIC_SUBTRACT:
    return jinja_checked_subtract(left, right, result);
  case JINJA_CMETA_ARITHMETIC_MULTIPLY:
    return jinja_checked_multiply(left, right, result);
  case JINJA_CMETA_ARITHMETIC_TRUE_DIVIDE:
    return JINJA_CMETA_ERR_RENDER;
  case JINJA_CMETA_ARITHMETIC_FLOOR_DIVIDE:
    return jinja_floor_divide_or_modulo(left, right, 0, result);
  case JINJA_CMETA_ARITHMETIC_MODULO:
    return jinja_floor_divide_or_modulo(left, right, 1, result);
  case JINJA_CMETA_ARITHMETIC_POWER:
    return jinja_checked_power(left, right, result);
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_float_divmod(double left, double right, double *floored,
                                             double *modulo) {
  double remainder;
  double division;
  double floor_value;

  if (floored == NULL || modulo == NULL || right == 0.0) return JINJA_CMETA_ERR_RENDER;
  remainder = fmod(left, right);
  division = (left - remainder) / right;
  if (remainder != 0.0) {
    if ((right < 0.0) != (remainder < 0.0)) {
      remainder += right;
      division -= 1.0;
    }
  } else {
    remainder = copysign(0.0, right);
  }
  floor_value = floor(division);
  if (division - floor_value > 0.5) floor_value += 1.0;
  if (floor_value == 0.0) floor_value = copysign(0.0, left / right);
  *floored = floor_value;
  *modulo = remainder;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_evaluate_float_arithmetic(JINJA_CMETA_ARITHMETIC_KIND arithmetic,
                                                          double left, double right,
                                                          double *result) {
  double floored;
  double modulo;

  if (result == NULL) return JINJA_CMETA_ERR_RENDER;
  switch (arithmetic) {
  case JINJA_CMETA_ARITHMETIC_POSITIVE:
    *result = left;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_NEGATE:
    *result = -left;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_ADD:
    *result = left + right;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_SUBTRACT:
    *result = left - right;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_MULTIPLY:
    *result = left * right;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_TRUE_DIVIDE:
    if (right == 0.0) return JINJA_CMETA_ERR_RENDER;
    *result = left / right;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_FLOOR_DIVIDE:
    if (jinja_float_divmod(left, right, &floored, &modulo) != JINJA_CMETA_OK)
      return JINJA_CMETA_ERR_RENDER;
    *result = floored;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_MODULO:
    if (jinja_float_divmod(left, right, &floored, &modulo) != JINJA_CMETA_OK)
      return JINJA_CMETA_ERR_RENDER;
    *result = modulo;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_ARITHMETIC_POWER:
    if (left == 0.0 && right < 0.0) return JINJA_CMETA_ERR_RENDER;
    if (left < 0.0 && isfinite(right) && trunc(right) != right) return JINJA_CMETA_ERR_RENDER;
    errno = 0;
    *result = pow(left, right);
    if ((errno == EDOM && isnan(*result)) || (isfinite(left) && isfinite(right) && isinf(*result)))
      return JINJA_CMETA_ERR_CAPACITY;
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_evaluate_number_arithmetic(JINJA_CMETA_ARITHMETIC_KIND arithmetic,
                                                           const JINJA_CMETA_NUMBER *left,
                                                           const JINJA_CMETA_NUMBER *right,
                                                           JINJA_CMETA_NUMBER *result) {
  int unary;
  int integral_result;

  if (left == NULL || right == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  unary =
      arithmetic == JINJA_CMETA_ARITHMETIC_POSITIVE || arithmetic == JINJA_CMETA_ARITHMETIC_NEGATE;
  integral_result = left->kind == JINJA_CMETA_NUMBER_INTEGER &&
                    (unary || right->kind == JINJA_CMETA_NUMBER_INTEGER) &&
                    arithmetic != JINJA_CMETA_ARITHMETIC_TRUE_DIVIDE &&
                    !(arithmetic == JINJA_CMETA_ARITHMETIC_POWER && right->integer < INT64_C(0));
  *result = (JINJA_CMETA_NUMBER){0};
  if (integral_result) {
    result->kind = JINJA_CMETA_NUMBER_INTEGER;
    return jinja_evaluate_arithmetic(arithmetic, left->integer, right->integer, &result->integer);
  }
  result->kind = JINJA_CMETA_NUMBER_FLOAT;
  if (arithmetic == JINJA_CMETA_ARITHMETIC_TRUE_DIVIDE &&
      left->kind == JINJA_CMETA_NUMBER_INTEGER && right->kind == JINJA_CMETA_NUMBER_INTEGER) {
    if (right->integer == 0) return JINJA_CMETA_ERR_RENDER;
    result->floating = jinja_integer_true_divide(left->integer, right->integer);
    return JINJA_CMETA_OK;
  }
  return jinja_evaluate_float_arithmetic(
      arithmetic, left->kind == JINJA_CMETA_NUMBER_FLOAT ? left->floating : (double)left->integer,
      right->kind == JINJA_CMETA_NUMBER_FLOAT ? right->floating : (double)right->integer,
      &result->floating);
}

/* One render owns the cumulative work budget; expression/call boundaries must
 * not reset it. Successful entries are paired with an exit even on failure. */
static JINJA_CMETA_STATUS jinja_value_traversal_enter(JINJA_CMETA_PROVIDER *provider) {
  if (provider == NULL) return JINJA_CMETA_ERR_RENDER;
  if (provider->shared.value_depth >= provider->shared.max_value_depth) {
    provider->shared.value_limit_error = "value traversal depth limit exceeded";
    return JINJA_CMETA_ERR_CAPACITY;
  }
  if (provider->shared.value_visits >= provider->shared.max_value_visits) {
    provider->shared.value_limit_error = "value comparison/key visit budget exceeded";
    return JINJA_CMETA_ERR_CAPACITY;
  }
  ++provider->shared.value_depth;
  ++provider->shared.value_visits;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_compare_values(JINJA_CMETA_PROVIDER *provider,
                                               const JINJA_CMETA_VALUE *left_value,
                                               const JINJA_CMETA_VALUE *right_value,
                                               JINJA_CMETA_COMPARISON_KIND comparison, size_t depth,
                                               int *result);
static JINJA_CMETA_STATUS jinja_same_value(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *left, const JINJA_CMETA_VALUE *right, int *result);

/* Container probes use identity-or-equality; standalone NaN == NaN remains false. */
static JINJA_CMETA_STATUS jinja_container_equal(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *left, const JINJA_CMETA_VALUE *right, size_t depth, int *result) {
  /* Native containers may retain unread borrowed fields. Traverse even aliases
   * so identity cannot suppress the existing metadata and byte-limit checks. */
  if (jinja_value_is_container(left->kind) || jinja_value_is_container(right->kind))
    return jinja_compare_values(provider, left, right, JINJA_CMETA_COMPARISON_EQUAL, depth, result);
  int same = 0;
  JINJA_CMETA_STATUS status = jinja_same_value(provider, left, right, &same);
  if (status != JINJA_CMETA_OK) return status;
  if (!same)
    return jinja_compare_values(provider, left, right, JINJA_CMETA_COMPARISON_EQUAL, depth, result);

  status = jinja_value_traversal_enter(provider);
  if (status != JINJA_CMETA_OK) return status;
  /* A borrowed scalar's address is not proof that its metadata or byte view is valid. */
  JINJA_CMETA_SCALAR ignored;
  if (left->kind == JINJA_CMETA_VALUE_NODE)
    status = jinja_scalar_from_value(provider, left, &ignored);
  if (status == JINJA_CMETA_OK && right->kind == JINJA_CMETA_VALUE_NODE)
    status = jinja_scalar_from_value(provider, right, &ignored);
  if (status == JINJA_CMETA_OK) *result = 1;
  --provider->shared.value_depth;
  return status;
}

static JINJA_CMETA_STATUS jinja_collection_item_value(JINJA_CMETA_PROVIDER *provider,
                                                      const JINJA_CMETA_VALUE *collection,
                                                      size_t position, size_t depth,
                                                      JINJA_CMETA_VALUE *value);

static JINJA_CMETA_STATUS jinja_dict_entry_value(JINJA_CMETA_PROVIDER *provider,
                                                 const JINJA_CMETA_VALUE *dict, size_t position,
                                                 size_t depth, JINJA_CMETA_VALUE *key,
                                                 JINJA_CMETA_VALUE *value) {
  (void)depth;
  if (provider == NULL || dict == NULL || key == NULL || value == NULL ||
      dict->kind != JINJA_CMETA_VALUE_DICT || position >= dict->collection_item_count ||
      dict->collection_values == NULL || position > SIZE_MAX / 2u)
    return JINJA_CMETA_ERR_RENDER;
  *key = dict->collection_values[position * 2u];
  *value = dict->collection_values[position * 2u + 1u];
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_dict_key_hashable_impl(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *key, int *hashable);

static JINJA_CMETA_STATUS jinja_dict_key_hashable(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *key, int *hashable) {
  if (key == NULL || hashable == NULL) return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STATUS status = jinja_value_traversal_enter(provider);
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_dict_key_hashable_impl(provider, key, hashable);
  --provider->shared.value_depth;
  return status;
}

static JINJA_CMETA_STATUS jinja_dict_key_hashable_impl(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *key, int *hashable) {
  size_t i;

  if (key->kind == JINJA_CMETA_VALUE_CALLABLE || key->kind == JINJA_CMETA_VALUE_TEMPLATE ||
      key->kind == JINJA_CMETA_VALUE_MODULE ||
      key->kind == JINJA_CMETA_VALUE_CYCLER || key->kind == JINJA_CMETA_VALUE_JOINER) {
    *hashable = 1;
    return JINJA_CMETA_OK;
  }
  if (key->kind == JINJA_CMETA_VALUE_ITERATOR) {
    if (key->iterator == NULL) return JINJA_CMETA_ERR_RENDER;
    *hashable = 1;
    return JINJA_CMETA_OK;
  }
  if (key->kind == JINJA_CMETA_VALUE_NAMESPACE) {
    *hashable = key->namespace_dict != NULL;
    return JINJA_CMETA_OK;
  }
  if (key->kind == JINJA_CMETA_VALUE_LIST || key->kind == JINJA_CMETA_VALUE_DICT) {
    *hashable = 0;
    return JINJA_CMETA_OK;
  }
  if (key->kind == JINJA_CMETA_VALUE_TUPLE) {
    for (i = 0u; i < key->collection_item_count; ++i) {
      JINJA_CMETA_VALUE item;
      JINJA_CMETA_STATUS status = jinja_collection_item_value(provider, key, i, 0u, &item);
      if (status != JINJA_CMETA_OK) return status;
      status = jinja_dict_key_hashable(provider, &item, hashable);
      if (status != JINJA_CMETA_OK || !*hashable) return status;
    }
    *hashable = 1;
    return JINJA_CMETA_OK;
  }
  if (key->kind == JINJA_CMETA_VALUE_NODE) {
    const cmeta_data_desc *desc = key->node.desc;
    if (key->node.object == NULL || !cmeta_data_desc_valid(desc)) return JINJA_CMETA_ERR_METADATA;
    *hashable = desc->kind == CMETA_DATA_BOOL || desc->kind == CMETA_DATA_SINT ||
                desc->kind == CMETA_DATA_UINT || desc->kind == CMETA_DATA_FLOAT ||
                desc->kind == CMETA_DATA_STRING || desc->kind == CMETA_DATA_BYTES;
    return JINJA_CMETA_OK;
  }
  *hashable = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_dict_key_equal(JINJA_CMETA_PROVIDER *provider,
                                               const JINJA_CMETA_VALUE *left,
                                               const JINJA_CMETA_VALUE *right, size_t depth,
                                               int *equal) {
  int hashable;
  JINJA_CMETA_STATUS status = jinja_dict_key_hashable(provider, left, &hashable);
  if (status != JINJA_CMETA_OK) return status;
  if (!hashable) return JINJA_CMETA_ERR_RENDER;
  status = jinja_dict_key_hashable(provider, right, &hashable);
  if (status != JINJA_CMETA_OK) return status;
  if (!hashable) return JINJA_CMETA_ERR_RENDER;
  return jinja_container_equal(provider, left, right, depth + 1u, equal);
}

static JINJA_CMETA_STATUS jinja_dict_entry_is_first(JINJA_CMETA_PROVIDER *provider,
                                                    const JINJA_CMETA_VALUE *dict, size_t position,
                                                    size_t depth, int *first) {
  JINJA_CMETA_VALUE key;
  JINJA_CMETA_VALUE ignored;
  size_t i;
  JINJA_CMETA_STATUS status =
      jinja_dict_entry_value(provider, dict, position, depth, &key, &ignored);
  if (status != JINJA_CMETA_OK) return status;
  for (i = 0u; i < position; ++i) {
    JINJA_CMETA_VALUE candidate;
    int equal = 0;
    status = jinja_dict_entry_value(provider, dict, i, depth, &candidate, &ignored);
    if (status == JINJA_CMETA_OK)
      status = jinja_dict_key_equal(provider, &key, &candidate, depth + 1u, &equal);
    if (status != JINJA_CMETA_OK) return status;
    if (equal) {
      *first = 0;
      return JINJA_CMETA_OK;
    }
  }
  {
    int hashable;
    status = jinja_dict_key_hashable(provider, &key, &hashable);
    if (status != JINJA_CMETA_OK) return status;
    if (!hashable) return JINJA_CMETA_ERR_RENDER;
  }
  *first = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_dict_lookup(JINJA_CMETA_PROVIDER *provider,
                                            const JINJA_CMETA_VALUE *dict,
                                            const JINJA_CMETA_VALUE *key, size_t depth,
                                            JINJA_CMETA_VALUE *value, int *found) {
  size_t position;
  int hashable;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || dict == NULL || key == NULL || value == NULL || found == NULL ||
      dict->kind != JINJA_CMETA_VALUE_DICT)
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_dict_key_hashable(provider, key, &hashable);
  if (status != JINJA_CMETA_OK) return status;
  if (!hashable) return JINJA_CMETA_ERR_RENDER;
  *found = 0;
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  for (position = dict->collection_item_count; position != 0u; --position) {
    JINJA_CMETA_VALUE candidate;
    JINJA_CMETA_VALUE candidate_value;
    int equal = 0;
    status =
        jinja_dict_entry_value(provider, dict, position - 1u, depth, &candidate, &candidate_value);
    if (status == JINJA_CMETA_OK)
      status = jinja_dict_key_equal(provider, key, &candidate, depth + 1u, &equal);
    if (status != JINJA_CMETA_OK) return status;
    if (equal) {
      *value = candidate_value;
      *found = 1;
      return JINJA_CMETA_OK;
    }
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_dict_unique_count(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *dict, size_t depth,
                                                  size_t *count) {
  size_t position;
  size_t result = 0u;

  if (provider == NULL || dict == NULL || count == NULL || dict->kind != JINJA_CMETA_VALUE_DICT)
    return JINJA_CMETA_ERR_RENDER;
  for (position = 0u; position < dict->collection_item_count; ++position) {
    int first;
    JINJA_CMETA_STATUS status = jinja_dict_entry_is_first(provider, dict, position, depth, &first);
    if (status != JINJA_CMETA_OK) return status;
    if (first) ++result;
  }
  *count = result;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_dict_key_by_unique_index(JINJA_CMETA_PROVIDER *provider,
                                                         const JINJA_CMETA_VALUE *dict,
                                                         size_t unique_index, size_t depth,
                                                         JINJA_CMETA_VALUE *key) {
  size_t position;
  size_t current = 0u;

  if (provider == NULL || dict == NULL || key == NULL || dict->kind != JINJA_CMETA_VALUE_DICT)
    return JINJA_CMETA_ERR_RENDER;
  for (position = 0u; position < dict->collection_item_count; ++position) {
    JINJA_CMETA_VALUE ignored;
    int first;
    JINJA_CMETA_STATUS status = jinja_dict_entry_is_first(provider, dict, position, depth, &first);
    if (status != JINJA_CMETA_OK) return status;
    if (!first) continue;
    if (current == unique_index)
      return jinja_dict_entry_value(provider, dict, position, depth, key, &ignored);
    ++current;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_collection_item_value(JINJA_CMETA_PROVIDER *provider,
                                                      const JINJA_CMETA_VALUE *collection,
                                                      size_t position, size_t depth,
                                                      JINJA_CMETA_VALUE *value) {
  (void)depth;
  if (provider == NULL || collection == NULL || value == NULL ||
      !jinja_value_is_collection(collection->kind) ||
      position >= collection->collection_item_count || collection->collection_values == NULL)
    return JINJA_CMETA_ERR_RENDER;
  *value = collection->collection_values[position];
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_collection_compare(JINJA_CMETA_PROVIDER *provider,
                                                   const JINJA_CMETA_VALUE *left,
                                                   const JINJA_CMETA_VALUE *right,
                                                   JINJA_CMETA_COMPARISON_KIND comparison,
                                                   size_t depth, int *result) {
  size_t common;
  size_t i;

  if (provider == NULL || left == NULL || right == NULL || result == NULL ||
      !jinja_value_is_collection(left->kind) || !jinja_value_is_collection(right->kind))
    return JINJA_CMETA_ERR_RENDER;
  if ((comparison == JINJA_CMETA_COMPARISON_EQUAL ||
       comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL) &&
      left->kind != right->kind) {
    *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
    return JINJA_CMETA_OK;
  }
  if (left->kind != right->kind) return JINJA_CMETA_ERR_RENDER;
  if ((comparison == JINJA_CMETA_COMPARISON_EQUAL ||
       comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL) &&
      left->collection_item_count != right->collection_item_count) {
    *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
    return JINJA_CMETA_OK;
  }

  common = left->collection_item_count < right->collection_item_count
               ? left->collection_item_count
               : right->collection_item_count;
  for (i = 0u; i < common; ++i) {
    JINJA_CMETA_VALUE left_item;
    JINJA_CMETA_VALUE right_item;
    JINJA_CMETA_STATUS status;
    int equal = 0;

    status = jinja_collection_item_value(provider, left, i, depth, &left_item);
    if (status == JINJA_CMETA_OK)
      status = jinja_collection_item_value(provider, right, i, depth, &right_item);
    if (status == JINJA_CMETA_OK)
      status = jinja_container_equal(provider, &left_item, &right_item, depth + 1u, &equal);
    if (status != JINJA_CMETA_OK) return status;
    if (!equal) {
      if (comparison == JINJA_CMETA_COMPARISON_EQUAL ||
          comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL) {
        *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
        return JINJA_CMETA_OK;
      }
      return jinja_compare_values(
          provider, &left_item, &right_item,
          comparison == JINJA_CMETA_COMPARISON_LESS_EQUAL      ? JINJA_CMETA_COMPARISON_LESS
          : comparison == JINJA_CMETA_COMPARISON_GREATER_EQUAL ? JINJA_CMETA_COMPARISON_GREATER
                                                               : comparison,
          depth + 1u, result);
    }
  }

  switch (comparison) {
  case JINJA_CMETA_COMPARISON_EQUAL:
  case JINJA_CMETA_COMPARISON_LESS_EQUAL:
  case JINJA_CMETA_COMPARISON_GREATER_EQUAL:
    *result = left->collection_item_count == right->collection_item_count;
    if (comparison == JINJA_CMETA_COMPARISON_LESS_EQUAL)
      *result = left->collection_item_count <= right->collection_item_count;
    else if (comparison == JINJA_CMETA_COMPARISON_GREATER_EQUAL)
      *result = left->collection_item_count >= right->collection_item_count;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_COMPARISON_NOT_EQUAL:
    *result = left->collection_item_count != right->collection_item_count;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_COMPARISON_LESS:
    *result = left->collection_item_count < right->collection_item_count;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_COMPARISON_GREATER:
    *result = left->collection_item_count > right->collection_item_count;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_COMPARISON_IN:
  case JINJA_CMETA_COMPARISON_NOT_IN:
    return JINJA_CMETA_ERR_RENDER;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_dict_compare(JINJA_CMETA_PROVIDER *provider,
                                             const JINJA_CMETA_VALUE *left,
                                             const JINJA_CMETA_VALUE *right,
                                             JINJA_CMETA_COMPARISON_KIND comparison, size_t depth,
                                             int *result) {
  size_t left_count = 0u;
  size_t right_count = 0u;
  size_t position;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || left == NULL || right == NULL || result == NULL ||
      left->kind != JINJA_CMETA_VALUE_DICT || right->kind != JINJA_CMETA_VALUE_DICT)
    return JINJA_CMETA_ERR_RENDER;
  if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_dict_unique_count(provider, left, depth, &left_count);
  if (status == JINJA_CMETA_OK)
    status = jinja_dict_unique_count(provider, right, depth, &right_count);
  if (status != JINJA_CMETA_OK) return status;
  if (left_count != right_count) {
    *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
    return JINJA_CMETA_OK;
  }
  for (position = 0u; position < left->collection_item_count; ++position) {
    JINJA_CMETA_VALUE key;
    JINJA_CMETA_VALUE left_value;
    JINJA_CMETA_VALUE right_value;
    int first;
    int found = 0;
    int equal = 0;

    status = jinja_dict_entry_is_first(provider, left, position, depth, &first);
    if (status != JINJA_CMETA_OK) return status;
    if (!first) continue;
    status = jinja_dict_entry_value(provider, left, position, depth, &key, &left_value);
    if (status == JINJA_CMETA_OK)
      status = jinja_dict_lookup(provider, left, &key, depth + 1u, &left_value, &found);
    if (status == JINJA_CMETA_OK && !found) status = JINJA_CMETA_ERR_RENDER;
    if (status == JINJA_CMETA_OK)
      status = jinja_dict_lookup(provider, right, &key, depth + 1u, &right_value, &found);
    if (status != JINJA_CMETA_OK) return status;
    if (!found) {
      *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
      return JINJA_CMETA_OK;
    }
    status = jinja_container_equal(provider, &left_value, &right_value, depth + 1u, &equal);
    if (status != JINJA_CMETA_OK) return status;
    if (!equal) {
      *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
      return JINJA_CMETA_OK;
    }
  }
  *result = comparison == JINJA_CMETA_COMPARISON_EQUAL;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_range_find(JINJA_CMETA_PROVIDER *provider,
                                           const JINJA_CMETA_RANGE *range,
                                           const JINJA_CMETA_VALUE *needle, int *found,
                                           uint64_t *position) {
  JINJA_CMETA_SCALAR left;
  JINJA_CMETA_STATUS status;
  int64_t number;
  uint64_t distance;
  uint64_t stride;
  if (provider == NULL || range == NULL || needle == NULL || found == NULL || position == NULL ||
      range->step == 0)
    return JINJA_CMETA_ERR_RENDER;
  *found = 0;
  *position = 0u;
  if (range->count == 0u || jinja_value_is_container(needle->kind) ||
      needle->kind == JINJA_CMETA_VALUE_RANGE || needle->kind == JINJA_CMETA_VALUE_ITERATOR)
    return JINJA_CMETA_OK;
  if (needle->kind == JINJA_CMETA_VALUE_NODE) {
    const cmeta_data_desc *desc = needle->node.desc;
    if (!cmeta_data_desc_valid(desc) || needle->node.object == NULL)
      return JINJA_CMETA_ERR_METADATA;
    if (desc->kind != CMETA_DATA_BOOL && desc->kind != CMETA_DATA_SINT &&
        desc->kind != CMETA_DATA_UINT && desc->kind != CMETA_DATA_FLOAT &&
        desc->kind != CMETA_DATA_STRING && desc->kind != CMETA_DATA_BYTES)
      return JINJA_CMETA_OK;
  }
  status = jinja_scalar_from_value(provider, needle, &left);
  if (status != JINJA_CMETA_OK) return status;
  if (left.kind == JINJA_CMETA_SCALAR_BOOL) number = left.boolean ? 1 : 0;
  else if (left.kind == JINJA_CMETA_SCALAR_SINT) number = left.sint;
  else if (left.kind == JINJA_CMETA_SCALAR_UINT) {
    if (left.uint > INT64_MAX) return JINJA_CMETA_OK;
    number = (int64_t)left.uint;
  } else if (left.kind == JINJA_CMETA_SCALAR_FLOAT) {
    if (!isfinite(left.floating) || left.floating < (double)INT64_MIN ||
        left.floating >= -(double)INT64_MIN || trunc(left.floating) != left.floating)
      return JINJA_CMETA_OK;
    number = (int64_t)left.floating;
  } else return JINJA_CMETA_OK;
  if (range->step > 0) {
    if (number < range->start || number >= range->stop) return JINJA_CMETA_OK;
    distance = (uint64_t)number - (uint64_t)range->start;
    stride = (uint64_t)range->step;
  } else {
    if (number > range->start || number <= range->stop) return JINJA_CMETA_OK;
    distance = (uint64_t)range->start - (uint64_t)number;
    stride = UINT64_C(0) - (uint64_t)range->step;
  }
  *found = distance % stride == 0u;
  if (*found) *position = distance / stride;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_membership_result(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *left_value,
                                                  const JINJA_CMETA_VALUE *right_value,
                                                  size_t depth, int *result) {
  JINJA_CMETA_SCALAR left;
  JINJA_CMETA_SCALAR right;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || left_value == NULL || right_value == NULL || result == NULL)
    return JINJA_CMETA_ERR_RENDER;
  if (right_value->kind == JINJA_CMETA_VALUE_UNDEFINED) {
    *result = 0;
    return JINJA_CMETA_OK;
  }
  if (right_value->kind == JINJA_CMETA_VALUE_ITERATOR ||
      right_value->kind == JINJA_CMETA_VALUE_LOOP) {
    JINJA_CMETA_VALUE receiver = *right_value;
    /* Search consumes through the first match, or through exhaustion. Tuple
       creation uses the same bounded workspace and cursor commit as first. */
    for (;;) {
      JINJA_CMETA_VALUE candidate;
      int found;
      status = jinja_iterator_next(provider,
          receiver.kind == JINJA_CMETA_VALUE_LOOP ? &receiver : receiver.iterator,
          &candidate, &found);
      if (status != JINJA_CMETA_OK) return status;
      if (!found) {
        *result = 0;
        return JINJA_CMETA_OK;
      }
      status = jinja_container_equal(provider, left_value, &candidate, depth + 1u, result);
      if (status != JINJA_CMETA_OK || *result) return status;
    }
  }
  if (right_value->kind == JINJA_CMETA_VALUE_RANGE) {
    uint64_t position;
    return jinja_range_find(provider, &right_value->range, left_value, result, &position);
  }

  if (right_value->kind == JINJA_CMETA_VALUE_DICT) {
    JINJA_CMETA_VALUE ignored;
    int found;
    status = jinja_dict_lookup(provider, right_value, left_value, depth, &ignored, &found);
    if (status != JINJA_CMETA_OK) return status;
    *result = found;
    return JINJA_CMETA_OK;
  }
  if (jinja_value_is_collection(right_value->kind)) {
    size_t i;
    for (i = 0u; i < right_value->collection_item_count; ++i) {
      JINJA_CMETA_VALUE candidate;
      int equal = 0;
      status = jinja_collection_item_value(provider, right_value, i, depth, &candidate);
      if (status == JINJA_CMETA_OK)
        status = jinja_container_equal(provider, left_value, &candidate, depth + 1u, &equal);
      if (status != JINJA_CMETA_OK) return status;
      if (equal) {
        *result = 1;
        return JINJA_CMETA_OK;
      }
    }
    *result = 0;
    return JINJA_CMETA_OK;
  }

  status = jinja_scalar_from_value(provider, right_value, &right);
  if (status == JINJA_CMETA_OK && right.kind == JINJA_CMETA_SCALAR_STRING) {
    status = jinja_scalar_from_value(provider, left_value, &left);
    if (status != JINJA_CMETA_OK || left.kind != JINJA_CMETA_SCALAR_STRING)
      return JINJA_CMETA_ERR_RENDER;
    *result = vstr_contains(vstr_from_buf((const char *)right.data, right.size),
                            vstr_from_buf((const char *)left.data, left.size));
    return JINJA_CMETA_OK;
  }

  if (right_value->kind == JINJA_CMETA_VALUE_NODE &&
      jinja_is_sequence_desc(right_value->node.desc)) {
    const JINJA_CMETA_SEQUENCE_VIEW *view =
        (const JINJA_CMETA_SEQUENCE_VIEW *)right_value->node.object;
    size_t i;

    if (view == NULL) return JINJA_CMETA_ERR_METADATA;
    if (view->count == 0u) {
      *result = 0;
      return JINJA_CMETA_OK;
    }
    if (view->data == NULL || view->stride == 0u || !cmeta_data_desc_valid(view->element) ||
        view->element->storage_type == NULL || view->element->storage_type->size > view->stride ||
        view->count - 1u > SIZE_MAX / view->stride)
      return JINJA_CMETA_ERR_METADATA;
    if (view->count > provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
    for (i = 0u; i < view->count; ++i) {
      JINJA_CMETA_VALUE candidate = {
          .kind = JINJA_CMETA_VALUE_NODE,
          .node = {.object = (const unsigned char *)view->data + i * view->stride,
                   .desc = view->element}};
      int equal = 0;
      status = jinja_container_equal(provider, left_value, &candidate, depth + 1u, &equal);
      if (status != JINJA_CMETA_OK) return status;
      if (equal) {
        *result = 1;
        return JINJA_CMETA_OK;
      }
    }
    *result = 0;
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_compare_values_impl(JINJA_CMETA_PROVIDER *provider,
                                               const JINJA_CMETA_VALUE *left_value,
                                               const JINJA_CMETA_VALUE *right_value,
                                               JINJA_CMETA_COMPARISON_KIND comparison, size_t depth,
                                               int *result) {
  JINJA_CMETA_STATUS status;

  if (comparison == JINJA_CMETA_COMPARISON_IN || comparison == JINJA_CMETA_COMPARISON_NOT_IN) {
    status = jinja_membership_result(provider, left_value, right_value, depth, result);
    if (status == JINJA_CMETA_OK && comparison == JINJA_CMETA_COMPARISON_NOT_IN) *result = !*result;
    return status;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_MISSING || right_value->kind == JINJA_CMETA_VALUE_MISSING) {
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    int equal = left_value->kind == JINJA_CMETA_VALUE_MISSING && right_value->kind == JINJA_CMETA_VALUE_MISSING;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_LOOP || right_value->kind == JINJA_CMETA_VALUE_LOOP) {
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    int equal = left_value->kind == JINJA_CMETA_VALUE_LOOP && right_value->kind == JINJA_CMETA_VALUE_LOOP &&
        left_value->loop == right_value->loop;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_CYCLER || left_value->kind == JINJA_CMETA_VALUE_JOINER ||
      right_value->kind == JINJA_CMETA_VALUE_CYCLER || right_value->kind == JINJA_CMETA_VALUE_JOINER) {
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    int equal = left_value->kind == right_value->kind && left_value->helper == right_value->helper;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_CALLABLE || right_value->kind == JINJA_CMETA_VALUE_CALLABLE) {
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    int equal = left_value->kind == JINJA_CMETA_VALUE_CALLABLE &&
                right_value->kind == JINJA_CMETA_VALUE_CALLABLE &&
                left_value->callable_kind == right_value->callable_kind &&
                left_value->helper == right_value->helper &&
                left_value->host_callable == right_value->host_callable;
    if (equal && (left_value->callable_kind == JINJA_CMETA_EXPRESSION_RANGE_COUNT ||
                  left_value->callable_kind == JINJA_CMETA_EXPRESSION_RANGE_INDEX))
      equal = left_value->range.identity == right_value->range.identity;
    if (equal && (left_value->callable_kind == JINJA_CMETA_EXPRESSION_MACRO ||
                  left_value->callable_kind == JINJA_CMETA_EXPRESSION_BLOCK))
      equal = left_value->closure == right_value->closure;
    if (equal && (left_value->callable_kind == JINJA_CMETA_EXPRESSION_LOOP_CYCLE ||
                  left_value->callable_kind == JINJA_CMETA_EXPRESSION_LOOP_CHANGED))
      equal = left_value->loop == right_value->loop;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_NAMESPACE ||
      right_value->kind == JINJA_CMETA_VALUE_NAMESPACE) {
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    int equal = left_value->kind == JINJA_CMETA_VALUE_NAMESPACE &&
                right_value->kind == JINJA_CMETA_VALUE_NAMESPACE &&
                left_value->namespace_dict == right_value->namespace_dict;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_TEMPLATE || right_value->kind == JINJA_CMETA_VALUE_TEMPLATE ||
      left_value->kind == JINJA_CMETA_VALUE_MODULE || right_value->kind == JINJA_CMETA_VALUE_MODULE) {
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL && comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    int equal = left_value->kind == right_value->kind &&
        left_value->identity.serial == right_value->identity.serial;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_ITERATOR ||
      right_value->kind == JINJA_CMETA_VALUE_ITERATOR) {
    int equal;
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL &&
        comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    if ((left_value->kind == JINJA_CMETA_VALUE_ITERATOR && left_value->iterator == NULL) ||
        (right_value->kind == JINJA_CMETA_VALUE_ITERATOR && right_value->iterator == NULL))
      return JINJA_CMETA_ERR_RENDER;
    equal = left_value->kind == JINJA_CMETA_VALUE_ITERATOR &&
            right_value->kind == JINJA_CMETA_VALUE_ITERATOR &&
            left_value->iterator == right_value->iterator;
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_RANGE || right_value->kind == JINJA_CMETA_VALUE_RANGE) {
    int equal = 0;
    if (comparison != JINJA_CMETA_COMPARISON_EQUAL &&
        comparison != JINJA_CMETA_COMPARISON_NOT_EQUAL)
      return JINJA_CMETA_ERR_RENDER;
    if (left_value->kind == JINJA_CMETA_VALUE_RANGE &&
        right_value->kind == JINJA_CMETA_VALUE_RANGE) {
      const JINJA_CMETA_RANGE *a = &left_value->range;
      const JINJA_CMETA_RANGE *b = &right_value->range;
      equal = a->count == b->count &&
              (a->count == 0u || (a->start == b->start && (a->count == 1u || a->step == b->step)));
    }
    *result = comparison == JINJA_CMETA_COMPARISON_EQUAL ? equal : !equal;
    return JINJA_CMETA_OK;
  }
  if (left_value->kind == JINJA_CMETA_VALUE_DICT || right_value->kind == JINJA_CMETA_VALUE_DICT) {
    if (left_value->kind == JINJA_CMETA_VALUE_DICT && right_value->kind == JINJA_CMETA_VALUE_DICT)
      return jinja_dict_compare(provider, left_value, right_value, comparison, depth, result);
    if (comparison == JINJA_CMETA_COMPARISON_EQUAL ||
        comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL) {
      *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
      return JINJA_CMETA_OK;
    }
    return JINJA_CMETA_ERR_RENDER;
  }
  if (jinja_value_is_collection(left_value->kind) || jinja_value_is_collection(right_value->kind)) {
    if (jinja_value_is_collection(left_value->kind) && jinja_value_is_collection(right_value->kind))
      return jinja_collection_compare(provider, left_value, right_value, comparison, depth, result);
    if (comparison == JINJA_CMETA_COMPARISON_EQUAL ||
        comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL) {
      *result = comparison == JINJA_CMETA_COMPARISON_NOT_EQUAL;
      return JINJA_CMETA_OK;
    }
    return JINJA_CMETA_ERR_RENDER;
  }
  {
    JINJA_CMETA_SCALAR left;
    JINJA_CMETA_SCALAR right;
    status = jinja_scalar_from_value(provider, left_value, &left);
    if (status == JINJA_CMETA_OK) status = jinja_scalar_from_value(provider, right_value, &right);
    if (status == JINJA_CMETA_OK) status = jinja_compare_scalars(&left, &right, comparison, result);
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_compare_values(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *left_value, const JINJA_CMETA_VALUE *right_value,
    JINJA_CMETA_COMPARISON_KIND comparison, size_t depth, int *result) {
  if (left_value == NULL || right_value == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (provider->strict_undefined &&
      (left_value->kind == JINJA_CMETA_VALUE_UNDEFINED ||
       right_value->kind == JINJA_CMETA_VALUE_UNDEFINED))
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STATUS status = jinja_value_traversal_enter(provider);
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_compare_values_impl(provider, left_value, right_value, comparison, depth, result);
  --provider->shared.value_depth;
  return status;
}

static JINJA_CMETA_STATUS jinja_value_truthy(JINJA_CMETA_PROVIDER *provider,
                                             const JINJA_CMETA_VALUE *value, int *result) {
  if (provider == NULL || value == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (provider->strict_undefined && value->kind == JINJA_CMETA_VALUE_UNDEFINED)
    return JINJA_CMETA_ERR_RENDER;
  switch (value->kind) {
  case JINJA_CMETA_VALUE_LOOP: {
    JINJA_CMETA_VALUE length;
    JINJA_CMETA_STATUS status = jinja_loop_attribute(provider, value->loop, vstr_from_cstr("length"), &length);
    if (status == JINJA_CMETA_OK) *result = length.integer != 0;
    return status;
  }
  case JINJA_CMETA_VALUE_CALLABLE:
  case JINJA_CMETA_VALUE_ITERATOR:
  case JINJA_CMETA_VALUE_NAMESPACE:
  case JINJA_CMETA_VALUE_CYCLER:
  case JINJA_CMETA_VALUE_JOINER:
  case JINJA_CMETA_VALUE_MODULE:
  case JINJA_CMETA_VALUE_TEMPLATE:
    *result = 1;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_MISSING:
    *result = 1;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_RANGE:
    *result = value->range.count != 0u;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_UNDEFINED:
  case JINJA_CMETA_VALUE_NONE:
    *result = 0;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_BOOL:
    *result = value->boolean != false;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_INTEGER:
    *result = value->integer != 0;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_FLOAT:
    *result = value->floating != 0.0;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_STRING:
    *result = value->string.len != 0u;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_NODE:
    *result = jinja_truthy(provider, &value->node);
    return provider->shared.status;
  case JINJA_CMETA_VALUE_LIST:
  case JINJA_CMETA_VALUE_TUPLE:
  case JINJA_CMETA_VALUE_DICT:
    *result = value->collection_item_count != 0u;
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_value_divisibleby(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *value, const JINJA_CMETA_VALUE *divisor, int *result) {
  enum { DIVISIBILITY_OPERANDS = 2 };
  const JINJA_CMETA_VALUE *operands[DIVISIBILITY_OPERANDS] = {value, divisor};
  uint64_t magnitudes[DIVISIBILITY_OPERANDS] = {0};
  double numbers[DIVISIBILITY_OPERANDS] = {0};
  int floating = 0;
  for (size_t i = 0u; i < DIVISIBILITY_OPERANDS; ++i) {
    JINJA_CMETA_SCALAR scalar;
    JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, operands[i], &scalar);
    if (status != JINJA_CMETA_OK) return status;
    switch (scalar.kind) {
    case JINJA_CMETA_SCALAR_BOOL:
      magnitudes[i] = scalar.boolean != 0;
      numbers[i] = (double)magnitudes[i];
      break;
    case JINJA_CMETA_SCALAR_SINT:
      magnitudes[i] = jinja_integer_magnitude(scalar.sint);
      numbers[i] = (double)scalar.sint;
      break;
    case JINJA_CMETA_SCALAR_UINT:
      magnitudes[i] = scalar.uint;
      numbers[i] = (double)scalar.uint;
      break;
    case JINJA_CMETA_SCALAR_FLOAT:
      floating = 1;
      numbers[i] = scalar.floating;
      break;
    default: return JINJA_CMETA_ERR_RENDER;
    }
  }
  if (floating) {
    if (numbers[1] == 0.0) return JINJA_CMETA_ERR_RENDER;
    *result = fmod(numbers[0], numbers[1]) == 0.0;
  } else {
    if (magnitudes[1] == 0u) return JINJA_CMETA_ERR_RENDER;
    /* Divisibility ignores signs; unsigned magnitudes avoid INT64_MIN / -1 overflow. */
    *result = magnitudes[0] % magnitudes[1] == 0u;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_value_test(JINJA_CMETA_PROVIDER *provider,
                                           const JINJA_CMETA_VALUE *value,
                                           JINJA_CMETA_TEST_KIND test, int *result) {
  const cmeta_data_desc *desc = NULL;
  cmeta_data_kind kind = CMETA_DATA_CUSTOM;

  if (value == NULL || result == NULL || test < JINJA_CMETA_TEST_DEFINED ||
      test > JINJA_CMETA_TEST_EVEN)
    return JINJA_CMETA_ERR_RENDER;
  if (value->kind == JINJA_CMETA_VALUE_NODE) {
    desc = value->node.desc;
    if (value->node.object == NULL || !cmeta_data_desc_valid(desc)) return JINJA_CMETA_ERR_METADATA;
    kind = desc->kind;
  }

  switch (test) {
  case JINJA_CMETA_TEST_FILTER:
  case JINJA_CMETA_TEST_TEST: {
    int hashable;
    JINJA_CMETA_STATUS status = jinja_dict_key_hashable(provider, value, &hashable);
    if (status != JINJA_CMETA_OK) return status;
    if (!hashable) return JINJA_CMETA_ERR_RENDER;
    *result = 0;
    if (!jinja_value_is_string(value)) return JINJA_CMETA_OK;
    JINJA_CMETA_SCALAR scalar;
    status = jinja_scalar_from_value(provider, value, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    if (scalar.size > provider->shared.max_string_bytes) return JINJA_CMETA_ERR_CAPACITY;
    vstr name = vstr_from_buf((const char *)scalar.data, scalar.size);
    if (vstr_utf8_invalid_offset(name) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
    JINJA_CMETA_EXPRESSION_KIND filter_kind;
    JINJA_CMETA_TEST_KIND test_kind;
    *result = test == JINJA_CMETA_TEST_FILTER ? jinja_builtin_filter_kind(name, &filter_kind)
                                             : jinja_builtin_test_kind(name, &test_kind);
    if (!*result) {
      *result = test == JINJA_CMETA_TEST_FILTER
          ? jinja_cmeta_env_find_filter(provider->instance->templ->env, name) != NULL
          : jinja_cmeta_env_find_test(provider->instance->templ->env, name) != NULL;
    }
    return JINJA_CMETA_OK;
  }
  case JINJA_CMETA_TEST_DIVISIBLEBY:
  case JINJA_CMETA_TEST_SAMEAS:
  case JINJA_CMETA_TEST_IN:
  case JINJA_CMETA_TEST_EQUAL:
  case JINJA_CMETA_TEST_NOT_EQUAL:
  case JINJA_CMETA_TEST_LESS:
  case JINJA_CMETA_TEST_LESS_EQUAL:
  case JINJA_CMETA_TEST_GREATER:
  case JINJA_CMETA_TEST_GREATER_EQUAL:
    return JINJA_CMETA_ERR_RENDER;
  case JINJA_CMETA_TEST_ODD:
  case JINJA_CMETA_TEST_EVEN: {
    enum { PARITY_DIVISOR = 2 };
    JINJA_CMETA_SCALAR scalar;
    JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, value, &scalar);
    int odd;
    if (status != JINJA_CMETA_OK) return status;
    switch (scalar.kind) {
    case JINJA_CMETA_SCALAR_BOOL: odd = scalar.boolean; break;
    case JINJA_CMETA_SCALAR_SINT: odd = scalar.sint % PARITY_DIVISOR != 0; break;
    case JINJA_CMETA_SCALAR_UINT: odd = scalar.uint % PARITY_DIVISOR != 0u; break;
    case JINJA_CMETA_SCALAR_FLOAT: {
      double remainder = isfinite(scalar.floating) ? fabs(fmod(scalar.floating, PARITY_DIVISOR)) : NAN;
      *result = remainder == (test == JINJA_CMETA_TEST_ODD ? 1.0 : 0.0);
      return JINJA_CMETA_OK;
    }
    default: return JINJA_CMETA_ERR_RENDER;
    }
    *result = test == JINJA_CMETA_TEST_ODD ? odd : !odd;
    return JINJA_CMETA_OK;
  }
  case JINJA_CMETA_TEST_ESCAPED:
    *result = jinja_value_is_safe(value);
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_DEFINED:
    *result = value->kind != JINJA_CMETA_VALUE_UNDEFINED;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_UNDEFINED:
    *result = value->kind == JINJA_CMETA_VALUE_UNDEFINED;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_NONE:
    *result = value->kind == JINJA_CMETA_VALUE_NONE;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_BOOLEAN:
    *result = value->kind == JINJA_CMETA_VALUE_BOOL ||
              (value->kind == JINJA_CMETA_VALUE_NODE && kind == CMETA_DATA_BOOL);
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_TRUE:
  case JINJA_CMETA_TEST_FALSE: {
    bool boolean = false;
    int is_boolean = 0;
    if (value->kind == JINJA_CMETA_VALUE_BOOL) {
      boolean = value->boolean;
      is_boolean = 1;
    } else if (value->kind == JINJA_CMETA_VALUE_NODE && kind == CMETA_DATA_BOOL) {
      memcpy(&boolean, value->node.object, sizeof(boolean));
      is_boolean = 1;
    }
    *result = is_boolean && (test == JINJA_CMETA_TEST_TRUE ? boolean != false : boolean == false);
    return JINJA_CMETA_OK;
  }
  case JINJA_CMETA_TEST_INTEGER:
    *result = value->kind == JINJA_CMETA_VALUE_INTEGER ||
              (value->kind == JINJA_CMETA_VALUE_NODE &&
               (kind == CMETA_DATA_SINT || kind == CMETA_DATA_UINT));
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_FLOAT:
    *result = value->kind == JINJA_CMETA_VALUE_FLOAT ||
              (value->kind == JINJA_CMETA_VALUE_NODE && kind == CMETA_DATA_FLOAT);
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_NUMBER:
    *result = value->kind == JINJA_CMETA_VALUE_BOOL || value->kind == JINJA_CMETA_VALUE_INTEGER ||
              value->kind == JINJA_CMETA_VALUE_FLOAT ||
              (value->kind == JINJA_CMETA_VALUE_NODE &&
               (kind == CMETA_DATA_BOOL || kind == CMETA_DATA_SINT || kind == CMETA_DATA_UINT ||
                kind == CMETA_DATA_FLOAT));
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_STRING:
    *result = value->kind == JINJA_CMETA_VALUE_STRING ||
              (value->kind == JINJA_CMETA_VALUE_NODE && kind == CMETA_DATA_STRING);
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_MAPPING:
    *result = value->kind == JINJA_CMETA_VALUE_DICT ||
              (value->kind == JINJA_CMETA_VALUE_NODE &&
               (kind == CMETA_DATA_STRUCT || kind == CMETA_DATA_MAP));
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_SEQUENCE:
    *result =
        value->kind == JINJA_CMETA_VALUE_RANGE || value->kind == JINJA_CMETA_VALUE_UNDEFINED ||
        value->kind == JINJA_CMETA_VALUE_STRING || jinja_value_is_container(value->kind) ||
        (value->kind == JINJA_CMETA_VALUE_NODE &&
         (kind == CMETA_DATA_STRING || kind == CMETA_DATA_BYTES || kind == CMETA_DATA_STRUCT ||
          kind == CMETA_DATA_SEQUENCE || kind == CMETA_DATA_MAP || jinja_is_sequence_desc(desc)));
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_ITERABLE:
    *result = value->kind == JINJA_CMETA_VALUE_LOOP ||
              value->kind == JINJA_CMETA_VALUE_ITERATOR || value->kind == JINJA_CMETA_VALUE_RANGE ||
              value->kind == JINJA_CMETA_VALUE_UNDEFINED ||
              value->kind == JINJA_CMETA_VALUE_STRING || jinja_value_is_container(value->kind) ||
              (value->kind == JINJA_CMETA_VALUE_NODE &&
               (kind == CMETA_DATA_STRING || kind == CMETA_DATA_BYTES ||
                kind == CMETA_DATA_STRUCT || kind == CMETA_DATA_SEQUENCE ||
                kind == CMETA_DATA_SET || kind == CMETA_DATA_MAP || jinja_is_sequence_desc(desc)));
    return JINJA_CMETA_OK;
  case JINJA_CMETA_TEST_CALLABLE:
    *result = value->kind == JINJA_CMETA_VALUE_LOOP || value->kind == JINJA_CMETA_VALUE_JOINER ||
              value->kind == JINJA_CMETA_VALUE_CALLABLE || value->kind == JINJA_CMETA_VALUE_UNDEFINED ||
              (value->kind == JINJA_CMETA_VALUE_NODE && value->node.is_loop_object);
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static void jinja_normalize_call_argument(JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_EXPRESSION_KIND kind;

  if (value == NULL || value->kind != JINJA_CMETA_VALUE_NODE) return;
  value->identity = value->node.value_identity;
  if (value->node.is_none) {
    value->kind = JINJA_CMETA_VALUE_NONE;
    return;
  }
  if (value->node.is_undefined) {
    value->kind = JINJA_CMETA_VALUE_UNDEFINED;
    return;
  }
  if (value->node.is_missing) {
    value->kind = JINJA_CMETA_VALUE_MISSING;
    return;
  }
  if (value->node.is_loop_object) {
    value->kind = JINJA_CMETA_VALUE_LOOP;
    value->loop = value->node.loop_receiver;
    return;
  }
  kind = value->node.expression_kind;
  if (kind == JINJA_CMETA_EXPRESSION_SELF || kind == JINJA_CMETA_EXPRESSION_MODULE) {
    value->kind = kind == JINJA_CMETA_EXPRESSION_MODULE
        ? JINJA_CMETA_VALUE_MODULE : JINJA_CMETA_VALUE_TEMPLATE;
    value->template_context = value->node.template_context;
    value->instance = value->node.instance;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_CALL) {
    value->kind = JINJA_CMETA_VALUE_CALLABLE;
    value->callable_kind = value->node.callable_kind;
    value->range = value->node.range;
    value->closure = value->node.closure;
    value->helper = value->node.helper;
    value->host_callable = value->node.host_callable;
    value->loop = value->node.loop_receiver;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_NAMESPACE) {
    value->kind = JINJA_CMETA_VALUE_NAMESPACE;
    value->namespace_dict = value->node.namespace_dict;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_CYCLER || kind == JINJA_CMETA_EXPRESSION_JOINER) {
    value->kind = kind == JINJA_CMETA_EXPRESSION_CYCLER ? JINJA_CMETA_VALUE_CYCLER : JINJA_CMETA_VALUE_JOINER;
    value->helper = value->node.helper;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_ITEMS) {
    value->kind = JINJA_CMETA_VALUE_ITERATOR;
    value->iterator = value->node.iterator;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_RANGE) {
    value->kind = JINJA_CMETA_VALUE_RANGE;
    value->range = value->node.range;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_BOOL) {
    value->kind = JINJA_CMETA_VALUE_BOOL;
    value->boolean = value->node.owned_bool;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_INTEGER) {
    value->kind = JINJA_CMETA_VALUE_INTEGER;
    value->integer = value->node.owned_integer;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_FLOAT) {
    value->kind = JINJA_CMETA_VALUE_FLOAT;
    value->floating = value->node.owned_float;
    return;
  }
  if (kind == JINJA_CMETA_EXPRESSION_STRING) {
    value->kind = JINJA_CMETA_VALUE_STRING;
    value->string = value->node.owned_string;
    value->string_safe = value->node.string_safe;
    return;
  }
  if (!jinja_expression_is_container(kind)) return;
  value->kind = kind == JINJA_CMETA_EXPRESSION_LIST    ? JINJA_CMETA_VALUE_LIST
                : kind == JINJA_CMETA_EXPRESSION_TUPLE ? JINJA_CMETA_VALUE_TUPLE
                                                       : JINJA_CMETA_VALUE_DICT;
  value->first_collection_item = value->node.first_collection_item;
  value->collection_item_count = value->node.collection_item_count;
  value->collection_values = value->node.collection_values;
}

static JINJA_CMETA_STATUS jinja_ensure_collection_storage(JINJA_CMETA_PROVIDER *provider,
                                                         size_t count) {
  return jinja_cmeta_values_prepare(&provider->shared.values,
      provider->shared.collection_value_count, count);
}

/* Publish a new immutable snapshot without changing the input snapshot.
 * O(fields) copying per write; old snapshots and borrowed payloads live until
 * render cleanup. The existing collection workspace bounds cumulative writes. */
static JINJA_CMETA_STATUS jinja_dict_snapshot_write(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *dict, const JINJA_CMETA_VALUE *key, const JINJA_CMETA_VALUE *value) {
  size_t position = dict->collection_item_count;
  size_t count = dict->collection_item_count;
  size_t offset = provider->shared.collection_value_count;
  int hashable;
  JINJA_CMETA_STATUS status = jinja_dict_key_hashable(provider, key, &hashable);
  if (status != JINJA_CMETA_OK) return status;
  if (!hashable) return JINJA_CMETA_ERR_RENDER;
  for (size_t i = 0u; i < count; ++i) {
    int equal;
    status = jinja_dict_key_equal(provider, key, &dict->collection_values[i * 2u], 0u, &equal);
    if (status != JINJA_CMETA_OK) return status;
    if (equal) { position = i; break; }
  }
  if (position == count) {
    if (count >= provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
    ++count;
  }
  if (offset > provider->shared.values.limit ||
      count > (provider->shared.values.limit - offset) / 2u)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, count * 2u);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_VALUE *fields = jinja_cmeta_values_at(&provider->shared.values, offset);
  if (dict->collection_item_count != 0u)
    memcpy(fields, dict->collection_values, dict->collection_item_count * 2u * sizeof(*fields));
  if (position == dict->collection_item_count) {
    fields[position * 2u] = *key;
    status = jinja_value_identify(provider, &fields[position * 2u]);
    if (status != JINJA_CMETA_OK) return status;
  }
  fields[position * 2u + 1u] = *value;
  status = jinja_value_identify(provider, &fields[position * 2u + 1u]);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.collection_value_count += count * 2u;
  dict->collection_values = fields;
  dict->collection_item_count = count;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_evaluate_collection(JINJA_CMETA_PROVIDER *provider,
                                                    JINJA_CMETA_NODE *context,
                                                    const JINJA_CMETA_EXPRESSION_NODE *expression,
                                                    size_t depth, JINJA_CMETA_VALUE *value) {
  const int dict = expression->kind == JINJA_CMETA_EXPRESSION_DICT;
  const size_t width = dict ? 2u : 1u;
  const size_t table_count =
      dict ? provider->instance->templ->dict_entry_count : provider->instance->templ->collection_item_count;
  size_t count;
  size_t offset;
  size_t i;
  if (expression->first_collection_item > table_count ||
      expression->collection_item_count > table_count - expression->first_collection_item ||
      expression->collection_item_count > SIZE_MAX / width)
    return JINJA_CMETA_ERR_RENDER;
  count = expression->collection_item_count * width;
  if (provider->shared.collection_value_count > provider->shared.values.limit ||
      count > provider->shared.values.limit - provider->shared.collection_value_count)
    return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_STATUS storage_status = jinja_ensure_collection_storage(provider, count);
  if (storage_status != JINJA_CMETA_OK) return storage_status;
  offset = provider->shared.collection_value_count;
  /* Reserve before recursion; nested collection snapshots never move or overwrite parents. */
  provider->shared.collection_value_count += count;
  for (i = 0u; i < count; ++i) {
    size_t expression_index;
    JINJA_CMETA_VALUE *item = jinja_cmeta_values_at(&provider->shared.values, offset + i);
    JINJA_CMETA_STATUS status;
    if (dict) {
      const JINJA_CMETA_DICT_ENTRY *entry;
      if (provider->instance->templ->dict_entries == NULL) return JINJA_CMETA_ERR_RENDER;
      entry = &provider->instance->templ->dict_entries[expression->first_collection_item + i / width];
      expression_index = i % width == 0u ? entry->key_node : entry->value_node;
    } else {
      if (provider->instance->templ->collection_items == NULL) return JINJA_CMETA_ERR_RENDER;
      expression_index = provider->instance->templ->collection_items[expression->first_collection_item + i].value_node;
    }
    status = jinja_expression_value(provider, context, expression_index, depth + 1u, item);
    if (status != JINJA_CMETA_OK) return status;
    jinja_normalize_call_argument(item);
  }
  value->kind = dict                                              ? JINJA_CMETA_VALUE_DICT
                : expression->kind == JINJA_CMETA_EXPRESSION_LIST ? JINJA_CMETA_VALUE_LIST
                                                                  : JINJA_CMETA_VALUE_TUPLE;
  value->first_collection_item = expression->first_collection_item;
  value->collection_item_count = expression->collection_item_count;
  value->collection_values = count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL;
  if (dict) {
    size_t ignored;
    return jinja_dict_unique_count(provider, value, depth, &ignored);
  }
  return JINJA_CMETA_OK;
}

typedef struct JINJA_CMETA_CALL_INPUT {
  JINJA_CMETA_VALUE *values;
  vstr keywords[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  size_t count;
  size_t positional;
  int expanded;
  int merge_expanded_keywords;
} JINJA_CMETA_CALL_INPUT;

typedef struct JINJA_CMETA_TRANSFORM {
  struct JINJA_CMETA_TRANSFORM *next;
  JINJA_CMETA_ITERATION source;
  JINJA_CMETA_CALL_INPUT call;
  JINJA_CMETA_VALUE parts;
  JINJA_CMETA_VALUE *unique_seen;
  const JINJA_CMETA_VALUE *name, *default_value;
  JINJA_CMETA_NODE *context;
  JINJA_CMETA_EXPRESSION_KIND kind;
  size_t unique_count, unique_capacity;
  int case_sensitive;
  int prepared, done;
} JINJA_CMETA_TRANSFORM;

static JINJA_CMETA_STATUS jinja_invoke_function(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CLOSURE *closure, const JINJA_CMETA_CALL_INPUT *input,
    JINJA_CMETA_VALUE *value);

static JINJA_CMETA_STATUS jinja_invoke_loop(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *sequence, const JINJA_CMETA_CALL_INPUT *call, JINJA_CMETA_VALUE *value);

static JINJA_CMETA_STATUS jinja_collect_call_input(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t index, size_t depth, JINJA_CMETA_VALUE *arguments, JINJA_CMETA_CALL_INPUT *input);

static JINJA_CMETA_STATUS jinja_evaluate_recursive_loop(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t expression_index, size_t depth, JINJA_CMETA_VALUE *value, JINJA_CMETA_VALUE *arguments);

static JINJA_CMETA_STATUS jinja_call_loop_method(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *sequence, JINJA_CMETA_EXPRESSION_KIND kind,
    const JINJA_CMETA_CALL_INPUT *input, size_t depth, JINJA_CMETA_VALUE *value) {
  if (sequence == NULL || sequence->loop_current == NULL || input->positional != input->count)
    return JINJA_CMETA_ERR_RENDER;
  const JINJA_CMETA_NODE *loop = sequence->loop_current;
  const JINJA_CMETA_VALUE *arguments = input->values;
  size_t count = input->count;
  size_t i;
  JINJA_CMETA_STATUS status;
  if (kind == JINJA_CMETA_EXPRESSION_LOOP_CYCLE) {
    if (count == 0u) return JINJA_CMETA_ERR_RENDER;
    *value = arguments[loop->loop_index % count];
    return JINJA_CMETA_OK;
  }

  if (kind == JINJA_CMETA_EXPRESSION_LOOP_CHANGED) {
    int changed = !sequence->changed_initialized ||
                  sequence->changed_value_count != count;

    if (!changed) {
      if (sequence->changed_value_offset > provider->shared.changed_value_count ||
          sequence->changed_value_count >
              provider->shared.changed_value_count - sequence->changed_value_offset)
        return JINJA_CMETA_ERR_RENDER;
      for (i = 0u; i < count; ++i) {
        int equal = 0;
        status = jinja_container_equal(
            provider, &provider->shared.changed_values[sequence->changed_value_offset + i], &arguments[i],
            depth + 1u, &equal);
        if (status != JINJA_CMETA_OK) return status;
        if (!equal) {
          changed = 1;
          break;
        }
      }
    }
    if (changed) {
      if (!sequence->changed_initialized ||
          sequence->changed_value_count != count) {
        if (provider->shared.changed_value_count > provider->shared.changed_value_capacity ||
            count >
                provider->shared.changed_value_capacity - provider->shared.changed_value_count)
          return JINJA_CMETA_ERR_CAPACITY;
        sequence->changed_value_offset = provider->shared.changed_value_count;
        provider->shared.changed_value_count += count;
        sequence->changed_value_count = count;
      }
      if (count != 0u)
        memcpy(provider->shared.changed_values + sequence->changed_value_offset, arguments,
               count * sizeof(arguments[0]));
      sequence->changed_initialized = 1;
    }
    value->kind = JINJA_CMETA_VALUE_BOOL;
    value->boolean = changed != 0;
    return JINJA_CMETA_OK;
  }

  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_evaluate_loop_call(JINJA_CMETA_PROVIDER *provider,
                                                   JINJA_CMETA_NODE *context,
                                                   const JINJA_CMETA_EXPRESSION_NODE *expression,
                                                   size_t expression_index, size_t depth,
                                                   JINJA_CMETA_VALUE *value,
                                                   JINJA_CMETA_VALUE *arguments) {
  JINJA_CMETA_NODE *loop;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || context == NULL || expression == NULL || value == NULL ||
      provider->instance->templ == NULL ||
      expression->collection_item_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      expression->first_collection_item > provider->instance->templ->collection_item_count ||
      expression->collection_item_count >
          provider->instance->templ->collection_item_count - expression->first_collection_item ||
      (expression->collection_item_count != 0u && provider->instance->templ->collection_items == NULL))
    return JINJA_CMETA_ERR_RENDER;

  const JINJA_CMETA_VALUE *bound_loop = jinja_binding_find(provider, vstr_from_cstr("loop"));
  if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
  loop = bound_loop != NULL && bound_loop->kind == JINJA_CMETA_VALUE_LOOP && bound_loop->loop != NULL
      ? bound_loop->loop->loop_current : jinja_nearest_loop_node(context);
  if (loop == NULL || loop->loop_sequence == NULL || loop->loop_length == 0u ||
      loop->loop_index >= loop->loop_length)
    return JINJA_CMETA_ERR_RENDER;

  JINJA_CMETA_CALL_INPUT input = {0};
  status = jinja_collect_call_input(provider, context, expression, expression_index, depth, arguments, &input);
  if (status != JINJA_CMETA_OK) return status;
  if (bound_loop != NULL && bound_loop->kind != JINJA_CMETA_VALUE_LOOP) return JINJA_CMETA_ERR_RENDER;
  return jinja_call_loop_method(provider, loop->loop_sequence, expression->kind, &input, depth, value);
}

static JINJA_CMETA_STATUS jinja_lookup_integer_key(JINJA_CMETA_PROVIDER *provider,
                                                   const JINJA_CMETA_VALUE *key, int64_t *index,
                                                   int *valid) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || key == NULL || index == NULL || valid == NULL)
    return JINJA_CMETA_ERR_RENDER;
  *valid = 0;
  switch (key->kind) {
  case JINJA_CMETA_VALUE_BOOL:
    *index = key->boolean ? INT64_C(1) : INT64_C(0);
    *valid = 1;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_INTEGER:
    *index = key->integer;
    *valid = 1;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_NODE:
    if (key->node.object == NULL || !cmeta_data_desc_valid(key->node.desc))
      return JINJA_CMETA_ERR_METADATA;
    if (key->node.desc->kind != CMETA_DATA_BOOL && key->node.desc->kind != CMETA_DATA_SINT &&
        key->node.desc->kind != CMETA_DATA_UINT)
      return JINJA_CMETA_OK;
    status = jinja_scalar_from_node(provider, &key->node, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    if (scalar.kind == JINJA_CMETA_SCALAR_BOOL) {
      *index = scalar.boolean ? INT64_C(1) : INT64_C(0);
    } else if (scalar.kind == JINJA_CMETA_SCALAR_SINT) {
      *index = scalar.sint;
    } else if (scalar.kind == JINJA_CMETA_SCALAR_UINT) {
      if (scalar.uint > (uint64_t)INT64_MAX) return JINJA_CMETA_OK;
      *index = (int64_t)scalar.uint;
    } else {
      return JINJA_CMETA_ERR_METADATA;
    }
    *valid = 1;
    return JINJA_CMETA_OK;
  case JINJA_CMETA_VALUE_UNDEFINED:
  case JINJA_CMETA_VALUE_NONE:
  case JINJA_CMETA_VALUE_FLOAT:
  case JINJA_CMETA_VALUE_STRING:
  case JINJA_CMETA_VALUE_LIST:
  case JINJA_CMETA_VALUE_TUPLE:
  case JINJA_CMETA_VALUE_DICT:
  case JINJA_CMETA_VALUE_RANGE:
  case JINJA_CMETA_VALUE_ITERATOR:
  case JINJA_CMETA_VALUE_NAMESPACE:
  case JINJA_CMETA_VALUE_CYCLER:
  case JINJA_CMETA_VALUE_JOINER:
  case JINJA_CMETA_VALUE_CALLABLE:
  case JINJA_CMETA_VALUE_LOOP:
  case JINJA_CMETA_VALUE_MISSING:
  case JINJA_CMETA_VALUE_MODULE:
  case JINJA_CMETA_VALUE_TEMPLATE:
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_call_range_method(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *receiver, const JINJA_CMETA_VALUE *argument, JINJA_CMETA_VALUE *value) {
  uint64_t position;
  int found;
  JINJA_CMETA_STATUS status = jinja_range_find(provider, &receiver->range, argument, &found, &position);
  if (status != JINJA_CMETA_OK) return status;
  value->kind = JINJA_CMETA_VALUE_INTEGER;
  if (receiver->callable_kind == JINJA_CMETA_EXPRESSION_RANGE_COUNT) value->integer = found ? 1 : 0;
  else {
    if (!found) return JINJA_CMETA_ERR_RENDER;
    if (position > INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
    value->integer = (int64_t)position;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_build_range(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *evaluated, size_t count, JINJA_CMETA_VALUE *value) {
  enum { RANGE_MAX_ARGUMENTS = 3 };
  int64_t arguments[RANGE_MAX_ARGUMENTS] = {0};
  JINJA_CMETA_RANGE range = {.step = 1};
  if (count == 0u || count > RANGE_MAX_ARGUMENTS) return JINJA_CMETA_ERR_RENDER;
  for (size_t i = 0u; i < count; ++i) {
    int valid;
    JINJA_CMETA_STATUS status = jinja_lookup_integer_key(provider, &evaluated[i], &arguments[i], &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid) return JINJA_CMETA_ERR_RENDER;
  }
  if (count == 1u) range.stop = arguments[0];
  else {
    range.start = arguments[0];
    range.stop = arguments[1];
    if (count == RANGE_MAX_ARGUMENTS) range.step = arguments[2];
  }
  if (range.step == 0) return JINJA_CMETA_ERR_RENDER;
  if ((range.step > 0 && range.start < range.stop) ||
      (range.step < 0 && range.start > range.stop)) {
    uint64_t distance = range.step > 0 ? (uint64_t)range.stop - (uint64_t)range.start
                                       : (uint64_t)range.start - (uint64_t)range.stop;
    uint64_t stride = range.step > 0 ? (uint64_t)range.step : UINT64_C(0) - (uint64_t)range.step;
    range.count = UINT64_C(1) + (distance - UINT64_C(1)) / stride;
  }
  if (provider->shared.range_identity == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
  range.identity = ++provider->shared.range_identity;
  value->kind = JINJA_CMETA_VALUE_RANGE;
  value->range = range;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_lookup_string_key(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *key, vstr *name,
                                                  int *valid) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status;

  if (provider == NULL || key == NULL || name == NULL || valid == NULL)
    return JINJA_CMETA_ERR_RENDER;
  *valid = 0;
  if (key->kind == JINJA_CMETA_VALUE_STRING) {
    if (!vstr_is_valid(key->string)) return JINJA_CMETA_ERR_RENDER;
    *name = key->string;
    *valid = 1;
    return JINJA_CMETA_OK;
  }
  if (key->kind != JINJA_CMETA_VALUE_NODE) return JINJA_CMETA_OK;
  if (key->node.object == NULL || !cmeta_data_desc_valid(key->node.desc))
    return JINJA_CMETA_ERR_METADATA;
  if (key->node.desc->kind != CMETA_DATA_STRING) return JINJA_CMETA_OK;
  status = jinja_scalar_from_node(provider, &key->node, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  if (scalar.kind != JINJA_CMETA_SCALAR_STRING) return JINJA_CMETA_ERR_METADATA;
  *name = vstr_from_buf((const char *)scalar.data, scalar.size);
  *valid = 1;
  return JINJA_CMETA_OK;
}

static int jinja_normalize_lookup_index(int64_t index, size_t count, size_t *normalized) {
  uint64_t magnitude;

  if (normalized == NULL) return 0;
  if (index >= 0) {
    if ((uint64_t)index >= (uint64_t)count) return 0;
    *normalized = (size_t)index;
    return 1;
  }
  magnitude = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);
  if (magnitude > (uint64_t)count) return 0;
  *normalized = count - (size_t)magnitude;
  return 1;
}

static JINJA_CMETA_STATUS jinja_utf8_scalar_lookup(vstr input, int64_t index, vstr *result,
                                                   int *found) {
  salts_unicode_scalar scalar;
  salts_unicode_status unicode_status;
  size_t cursor = 0u;
  size_t scalar_count = 0u;
  size_t target;

  if (result == NULL || found == NULL || !vstr_is_valid(input)) return JINJA_CMETA_ERR_RENDER;
  *found = 0;
  while (cursor < input.len) {
    unicode_status = salts_unicode_utf8_next(input, &cursor, &scalar);
    if (unicode_status != SALTS_UNICODE_OK) return JINJA_CMETA_ERR_METADATA;
    if (scalar_count == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
    ++scalar_count;
  }
  if (!jinja_normalize_lookup_index(index, scalar_count, &target)) return JINJA_CMETA_OK;

  cursor = 0u;
  scalar_count = 0u;
  while (cursor < input.len) {
    unicode_status = salts_unicode_utf8_next(input, &cursor, &scalar);
    if (unicode_status != SALTS_UNICODE_OK) return JINJA_CMETA_ERR_METADATA;
    if (scalar_count == target) {
      *result = vstr_from_buf(input.data + scalar.byte_offset, scalar.byte_length);
      *found = 1;
      return JINJA_CMETA_OK;
    }
    ++scalar_count;
  }
  return JINJA_CMETA_ERR_METADATA;
}

static JINJA_CMETA_STATUS jinja_validate_sequence_view(const JINJA_CMETA_SEQUENCE_VIEW *view) {
  if (view == NULL) return JINJA_CMETA_ERR_METADATA;
  if (view->count == 0u) return JINJA_CMETA_OK;
  if (view->data == NULL || view->stride == 0u || !cmeta_data_desc_valid(view->element) ||
      view->element->storage_type == NULL || view->element->storage_type->size > view->stride ||
      view->count - 1u > SIZE_MAX / view->stride)
    return JINJA_CMETA_ERR_METADATA;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_lookup_struct_item(const JINJA_CMETA_NODE *base, vstr name,
                                                   JINJA_CMETA_VALUE *result) {
  const cmeta_data_struct_shape *shape;
  size_t i;

  if (base == NULL || result == NULL || base->object == NULL ||
      !cmeta_data_desc_valid(base->desc) || base->desc->kind != CMETA_DATA_STRUCT)
    return JINJA_CMETA_ERR_METADATA;
  shape = (const cmeta_data_struct_shape *)base->desc->shape;
  if (shape == NULL || (shape->field_count != 0u && shape->fields == NULL))
    return JINJA_CMETA_ERR_METADATA;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    const void *child;
    size_t field_size;

    if (field->name == NULL) return JINJA_CMETA_ERR_METADATA;
    field_size = strlen(field->name);
    if (field_size != name.len || memcmp(field->name, name.data, name.len) != 0) continue;
    if (!cmeta_data_desc_valid(field->value) || !jinja_child_address(base, field, &child))
      return JINJA_CMETA_ERR_METADATA;
    result->kind = JINJA_CMETA_VALUE_NODE;
    result->node =
        (JINJA_CMETA_NODE){.object = child, .desc = field->value, .parent = base->parent};
    return JINJA_CMETA_OK;
  }
  result->kind = JINJA_CMETA_VALUE_UNDEFINED;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_macro_attribute(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CLOSURE *closure, vstr name, JINJA_CMETA_VALUE *result) {
  if (closure == NULL || closure->instance == NULL) return JINJA_CMETA_ERR_METADATA;
  const JINJA_CMETA_TEMPLATE *templ = closure->instance->templ;
  if (closure->function >= templ->function_count)
    return JINJA_CMETA_ERR_METADATA;
  const JINJA_CMETA_FUNCTION *function = &templ->functions[closure->function];
  if (vstr_eq(name, vstr_from_cstr("name"))) {
    *result = function->name_length == 0u
        ? (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NONE}
        : (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
            .identity = {.source = &function->name_offset},
            .string = vstr_from_buf(templ->program_strings + function->name_offset,
                function->name_length)};
  } else if (vstr_eq(name, vstr_from_cstr("arguments"))) {
    const size_t count = function->parameter_count;
    const size_t offset = provider->shared.collection_value_count;
    if (offset > provider->shared.values.limit || count > provider->shared.values.limit - offset)
      return JINJA_CMETA_ERR_CAPACITY;
    JINJA_CMETA_STATUS status = jinja_ensure_collection_storage(provider, count);
    if (status != JINJA_CMETA_OK) return status;
    for (size_t i = 0u; i < count; ++i) {
      const JINJA_CMETA_PARAMETER *parameter = &templ->parameters[function->first_parameter + i];
      (*jinja_cmeta_values_at(&provider->shared.values, offset + i)) = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
          .identity = {.source = &parameter->name_offset},
          .string = vstr_from_buf(templ->program_strings + parameter->name_offset,
              parameter->name_length)};
    }
    provider->shared.collection_value_count += count;
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
        .identity = {.source = &function->first_parameter},
        .collection_item_count = count,
        .collection_values = count == 0u ? NULL : jinja_cmeta_values_at(&provider->shared.values, offset)};
  } else {
    int flag;
    if (vstr_eq(name, vstr_from_cstr("catch_kwargs"))) flag = function->accepts_kwargs;
    else if (vstr_eq(name, vstr_from_cstr("catch_varargs"))) flag = function->accepts_varargs;
    else if (vstr_eq(name, vstr_from_cstr("caller"))) flag = function->uses_caller;
    else if (vstr_eq(name, vstr_from_cstr("explicit_caller"))) {
      flag = 0;
      for (size_t i = 0u; i < function->parameter_count; ++i) {
        const JINJA_CMETA_PARAMETER *parameter = &templ->parameters[function->first_parameter + i];
        if (vstr_eq(vstr_from_buf(templ->program_strings + parameter->name_offset,
            parameter->name_length), vstr_from_cstr("caller"))) { flag = 1; break; }
      }
    } else return JINJA_CMETA_OK;
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_BOOL, .boolean = flag != 0};
  }
  return JINJA_CMETA_OK;
}

static void jinja_helper_attribute(const JINJA_CMETA_VALUE *base, vstr name, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_HELPER *helper = base->helper;
  JINJA_CMETA_EXPRESSION_KIND method = 0;
  if (base->kind == JINJA_CMETA_VALUE_CYCLER) {
    if (vstr_eq(name, vstr_from_cstr("current"))) *result = helper->value.collection_values[helper->position];
    else if (vstr_eq(name, vstr_from_cstr("items"))) *result = helper->value;
    else if (vstr_eq(name, vstr_from_cstr("pos")))
      *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)helper->position};
    else if (vstr_eq(name, vstr_from_cstr("next")) || vstr_eq(name, vstr_from_cstr("__next__")))
      method = JINJA_CMETA_EXPRESSION_CYCLER_NEXT;
    else if (vstr_eq(name, vstr_from_cstr("reset"))) method = JINJA_CMETA_EXPRESSION_CYCLER_RESET;
  } else {
    if (vstr_eq(name, vstr_from_cstr("sep"))) *result = helper->value;
    else if (vstr_eq(name, vstr_from_cstr("used")))
      *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_BOOL, .boolean = helper->position != 0u};
    else if (vstr_eq(name, vstr_from_cstr("__call__"))) method = JINJA_CMETA_EXPRESSION_JOINER_CALL;
  }
  if (method != 0)
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .callable_kind = method, .helper = helper};
}

static const char jinja_groupby_identity = 0;

static JINJA_CMETA_STATUS jinja_lookup_item(JINJA_CMETA_PROVIDER *provider,
                                            const JINJA_CMETA_VALUE *base,
                                            const JINJA_CMETA_VALUE *key, size_t depth,
                                            JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_STATUS status;
  int64_t index = 0;
  int valid = 0;

  if (provider == NULL || base == NULL || key == NULL || result == NULL)
    return JINJA_CMETA_ERR_RENDER;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (base->kind == JINJA_CMETA_VALUE_UNDEFINED) return JINJA_CMETA_ERR_RENDER;
  if (base->kind == JINJA_CMETA_VALUE_CYCLER || base->kind == JINJA_CMETA_VALUE_JOINER) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    jinja_helper_attribute(base, name, result);
    return JINJA_CMETA_OK;
  }
  if (base->kind == JINJA_CMETA_VALUE_TEMPLATE || base->kind == JINJA_CMETA_VALUE_MODULE) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    if (base->instance == NULL) return JINJA_CMETA_ERR_METADATA;
    if (base->kind == JINJA_CMETA_VALUE_MODULE)
      return jinja_module_export(base->instance, name, result);
    return jinja_block_value(provider, base->instance, name, base->template_context, result);
  }
  if (base->kind == JINJA_CMETA_VALUE_LOOP) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    return jinja_loop_attribute(provider, base->loop, name, result);
  }
  if (base->kind == JINJA_CMETA_VALUE_CALLABLE && base->callable_kind == JINJA_CMETA_EXPRESSION_MACRO) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    return jinja_macro_attribute(provider, base->closure, name, result);
  }
  if (base->kind == JINJA_CMETA_VALUE_CALLABLE && base->callable_kind == JINJA_CMETA_EXPRESSION_BLOCK) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    if (vstr_eq(name, vstr_from_cstr("super")))
      return jinja_super_value(provider, base->closure, result);
    if (vstr_eq(name, vstr_from_cstr("name"))) {
      if (base->closure == NULL || base->closure->instance == NULL ||
          base->closure->function >= base->closure->instance->templ->function_count)
        return JINJA_CMETA_ERR_METADATA;
      const JINJA_CMETA_TEMPLATE *templ = base->closure->instance->templ;
      const JINJA_CMETA_FUNCTION *function = &templ->functions[base->closure->function];
      *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
          .string = vstr_from_buf(templ->program_strings + function->name_offset, function->name_length)};
    }
    return JINJA_CMETA_OK;
  }
  if (base->kind == JINJA_CMETA_VALUE_NAMESPACE) {
    int found;
    if (base->namespace_dict == NULL) return JINJA_CMETA_ERR_RENDER;
    if (!jinja_value_is_string(key)) return JINJA_CMETA_OK;
    return jinja_dict_lookup(provider, base->namespace_dict, key, depth, result, &found);
  }

  if (base->kind == JINJA_CMETA_VALUE_RANGE) {
    uint64_t position;
    if (key->kind == JINJA_CMETA_VALUE_STRING) {
      jinja_range_named_value(&base->range, key->string, result);
      return JINJA_CMETA_OK;
    } else {
      status = jinja_lookup_integer_key(provider, key, &index, &valid);
      if (status != JINJA_CMETA_OK || !valid) return status;
      if (index < 0) {
        uint64_t magnitude = UINT64_C(0) - (uint64_t)index;
        if (magnitude > base->range.count) return JINJA_CMETA_OK;
        position = base->range.count - magnitude;
      } else position = (uint64_t)index;
      if (position >= base->range.count) return JINJA_CMETA_OK;
      index = jinja_range_item(&base->range, position);
    }
    result->kind = JINJA_CMETA_VALUE_INTEGER;
    result->integer = index;
    return JINJA_CMETA_OK;
  }

  if (base->kind == JINJA_CMETA_VALUE_DICT) {
    int found;
    status = jinja_dict_lookup(provider, base, key, depth, result, &found);
    if (status != JINJA_CMETA_OK) return status;
    if (!found) *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
    return JINJA_CMETA_OK;
  }
  if (base->kind == JINJA_CMETA_VALUE_TUPLE &&
      base->identity.source == &jinja_groupby_identity) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (valid) {
      if (vstr_eq(name, vstr_from_cstr("grouper"))) *result = base->collection_values[0];
      else if (vstr_eq(name, vstr_from_cstr("list"))) *result = base->collection_values[1];
      return JINJA_CMETA_OK;
    }
  }
  if (jinja_value_is_collection(base->kind)) {
    size_t normalized;
    status = jinja_lookup_integer_key(provider, key, &index, &valid);
    if (status != JINJA_CMETA_OK || !valid ||
        !jinja_normalize_lookup_index(index, base->collection_item_count, &normalized))
      return status;
    return jinja_collection_item_value(provider, base, normalized, depth, result);
  }

  if (base->kind == JINJA_CMETA_VALUE_STRING ||
      (base->kind == JINJA_CMETA_VALUE_NODE && base->node.desc != NULL &&
       base->node.desc->kind == CMETA_DATA_STRING)) {
    JINJA_CMETA_SCALAR scalar;
    vstr input;
    vstr selected;
    int found;

    status = jinja_lookup_integer_key(provider, key, &index, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    status = jinja_scalar_from_value(provider, base, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    if (scalar.kind != JINJA_CMETA_SCALAR_STRING) return JINJA_CMETA_ERR_METADATA;
    input = vstr_from_buf((const char *)scalar.data, scalar.size);
    status = jinja_utf8_scalar_lookup(input, index, &selected, &found);
    if (status != JINJA_CMETA_OK || !found) return status;
    result->kind = JINJA_CMETA_VALUE_STRING;
    result->string = selected;
    result->string_safe = jinja_value_is_safe(base);
    return JINJA_CMETA_OK;
  }

  if (base->kind != JINJA_CMETA_VALUE_NODE) return JINJA_CMETA_OK;
  if (base->node.object == NULL || !cmeta_data_desc_valid(base->node.desc))
    return JINJA_CMETA_ERR_METADATA;
  if (jinja_is_sequence_desc(base->node.desc)) {
    const JINJA_CMETA_SEQUENCE_VIEW *view = (const JINJA_CMETA_SEQUENCE_VIEW *)base->node.object;
    size_t normalized;

    status = jinja_validate_sequence_view(view);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_lookup_integer_key(provider, key, &index, &valid);
    if (status != JINJA_CMETA_OK || !valid ||
        !jinja_normalize_lookup_index(index, view->count, &normalized))
      return status;
    result->kind = JINJA_CMETA_VALUE_NODE;
    result->node =
        (JINJA_CMETA_NODE){.object = (const unsigned char *)view->data + normalized * view->stride,
                           .desc = view->element,
                           .parent = base->node.parent};
    return JINJA_CMETA_OK;
  }
  if (base->node.desc->kind == CMETA_DATA_STRUCT) {
    vstr name;
    status = jinja_lookup_string_key(provider, key, &name, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status;
    return jinja_lookup_struct_item(&base->node, name, result);
  }
  return JINJA_CMETA_OK;
}

int jinja_cmeta_is_builtin_global(vstr name) {
  if (jinja_name_equal(name.data, name.len, "range")) return JINJA_CMETA_EXPRESSION_RANGE;
  if (jinja_name_equal(name.data, name.len, "dict")) return JINJA_CMETA_EXPRESSION_DICT_CALL;
  if (jinja_name_equal(name.data, name.len, "namespace")) return JINJA_CMETA_EXPRESSION_NAMESPACE;
  if (jinja_name_equal(name.data, name.len, "cycler")) return JINJA_CMETA_EXPRESSION_CYCLER;
  if (jinja_name_equal(name.data, name.len, "joiner")) return JINJA_CMETA_EXPRESSION_JOINER;
  return 0;
}

static JINJA_CMETA_EXPRESSION_KIND jinja_global_kind(vstr name) {
  if (jinja_name_equal(name.data, name.len, "range")) return JINJA_CMETA_EXPRESSION_RANGE;
  if (jinja_name_equal(name.data, name.len, "dict")) return JINJA_CMETA_EXPRESSION_DICT_CALL;
  if (jinja_name_equal(name.data, name.len, "namespace")) return JINJA_CMETA_EXPRESSION_NAMESPACE;
  if (jinja_name_equal(name.data, name.len, "cycler")) return JINJA_CMETA_EXPRESSION_CYCLER;
  if (jinja_name_equal(name.data, name.len, "joiner")) return JINJA_CMETA_EXPRESSION_JOINER;
  return 0;
}

static JINJA_CMETA_STATUS jinja_resolve_value_path(JINJA_CMETA_PROVIDER *provider,
                                                   JINJA_CMETA_NODE *context, vstr path,
                                                   JINJA_CMETA_VALUE *value) {
  size_t end = 0u;
  const JINJA_CMETA_VALUE *binding;
  JINJA_CMETA_STATUS status;
  if (!vstr_is_valid(path) || path.len == 0u) return JINJA_CMETA_ERR_RENDER;
  while (end < path.len && path.data[end] != '.') ++end;
  binding = jinja_binding_find(provider, vstr_from_buf(path.data, end));
  if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
  if (binding == NULL) {
    int found;
    JINJA_CMETA_EXPRESSION_KIND builtin = jinja_global_kind(vstr_from_buf(path.data, end));
    *value = (JINJA_CMETA_VALUE){0};
    status = jinja_resolve_path(provider, context, builtin == 0 ? path : vstr_from_buf(path.data, end),
                                &value->node, &found);
    if (status != JINJA_CMETA_OK) return status;
    if (found) {
      value->kind = JINJA_CMETA_VALUE_NODE;
      jinja_normalize_call_argument(value);
    }
    if (builtin == 0) {
      const JINJA_CMETA_CALLABLE *host = jinja_cmeta_env_find_function(
          provider->instance->templ->env, vstr_from_buf(path.data, end));
      if (host == NULL) host = jinja_cmeta_env_find_tag(
          provider->instance->templ->env, vstr_from_buf(path.data, end));
      if (host != NULL && !found)
        *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .host_callable = host};
      return JINJA_CMETA_OK;
    }
    if (!found) *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .callable_kind = builtin};
  }
  else *value = *binding;
  while (end < path.len) {
    size_t start = ++end;
    JINJA_CMETA_VALUE base = *value;
    JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_STRING};
    while (end < path.len && path.data[end] != '.') ++end;
    if (end == start) return JINJA_CMETA_ERR_RENDER;
    key.string = vstr_from_buf(path.data + start, end - start);
    status = jinja_lookup_item(provider, &base, &key, 0u, value);
    if (status != JINJA_CMETA_OK) return status;
    jinja_normalize_call_argument(value);
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_global_value(JINJA_CMETA_PROVIDER *provider,
    vstr name, JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_TEMPLATE_INSTANCE *owner = provider->context->owner;
  if (owner == NULL) {
    for (size_t i = 0u; i < provider->context->count; ++i) {
      const JINJA_CMETA_VALUE *entry = &provider->context->entries[i * 2u];
      if (vstr_eq(entry->string, name)) { *value = entry[1]; return JINJA_CMETA_OK; }
    }
  } else if (owner->root_activation != NULL) {
    const JINJA_CMETA_LEXICAL_SCOPE *root = &owner->templ->lexical_scopes[0];
    for (size_t i = 0u; i < root->binding_count; ++i) {
      const size_t cell_index = owner->templ->cell_bindings[root->first_binding + i].cell;
      if (!vstr_eq(owner->templ->cells[cell_index].name, name)) continue;
      JINJA_CMETA_CELL_VALUE *cell;
      JINJA_CMETA_STATUS status = jinja_cmeta_activation_cell(owner->root_activation, cell_index, &cell);
      if (status != JINJA_CMETA_OK) return status;
      if (cell->bound && cell->value.kind != JINJA_CMETA_VALUE_MISSING) {
        *value = cell->value;
        return JINJA_CMETA_OK;
      }
      break;
    }
  }
  if (owner != NULL) {
    for (size_t i = 0u; i < provider->context->count; ++i) {
      const JINJA_CMETA_VALUE *entry = &provider->context->entries[i * 2u];
      if (vstr_eq(entry->string, name)) { *value = entry[1]; return JINJA_CMETA_OK; }
    }
  }
  int found = 0;
  *value = (JINJA_CMETA_VALUE){0};
  JINJA_CMETA_STATUS status = jinja_resolve_path(provider, &provider->shared.nodes[0], name, &value->node, &found);
  if (status != JINJA_CMETA_OK) return status;
  if (found) {
    value->kind = JINJA_CMETA_VALUE_NODE;
    jinja_normalize_call_argument(value);
    return JINJA_CMETA_OK;
  }
  const JINJA_CMETA_ENV_GLOBAL *global = jinja_cmeta_env_find_global(
      provider->instance->templ->env, name);
  if (global != NULL) {
    switch (global->value.kind) {
    case JINJA_CMETA_CALL_VALUE_UNDEFINED:
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
      break;
    case JINJA_CMETA_CALL_VALUE_NONE:
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NONE};
      break;
    case JINJA_CMETA_CALL_VALUE_BOOL:
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_BOOL, .boolean = global->value.boolean};
      break;
    case JINJA_CMETA_CALL_VALUE_INTEGER:
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = global->value.integer};
      break;
    case JINJA_CMETA_CALL_VALUE_FLOAT:
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_FLOAT, .floating = global->value.floating};
      break;
    case JINJA_CMETA_CALL_VALUE_STRING:
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
          .string = global->value.string, .string_safe = global->value.string_safe};
      break;
    default:
      return JINJA_CMETA_ERR_METADATA;
    }
    return JINJA_CMETA_OK;
  }
  JINJA_CMETA_EXPRESSION_KIND kind = jinja_global_kind(name);
  if (kind != 0) *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .callable_kind = kind};
  else {
    const JINJA_CMETA_CALLABLE *host = jinja_cmeta_env_find_function(
        provider->instance->templ->env, name);
    if (host == NULL) host = jinja_cmeta_env_find_tag(provider->instance->templ->env, name);
    if (host != NULL) *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE, .host_callable = host};
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_scope_load(JINJA_CMETA_PROVIDER *provider,
    size_t scope, JINJA_CMETA_ACTIVATION *activation) {
  if (provider->instance->templ->lexical_scope_count == 0u) return JINJA_CMETA_ERR_METADATA;
  if (scope >= provider->instance->templ->lexical_scope_count) return JINJA_CMETA_ERR_METADATA;
  const JINJA_CMETA_LEXICAL_SCOPE *frame = &provider->instance->templ->lexical_scopes[scope];
  JINJA_CMETA_STATUS status;
  if (activation == NULL) return JINJA_CMETA_ERR_METADATA;
  provider->activation = activation;
  provider->lexical_scope = scope;
  if (provider->scope_depth != 0u)
    provider->scopes[provider->scope_depth - 1u].clear_scope = scope;
  for (size_t i = 0u; i < frame->binding_count; ++i) {
    const JINJA_CMETA_CELL_BINDING *binding = &provider->instance->templ->cell_bindings[frame->first_binding + i];
    JINJA_CMETA_CELL_VALUE *cell;
    status = jinja_cmeta_activation_cell(activation, binding->cell, &cell);
    if (status != JINJA_CMETA_OK) return status;
    *cell = (JINJA_CMETA_CELL_VALUE){0};
    if (binding->load == JINJA_CMETA_CELL_ALIAS) {
      JINJA_CMETA_CELL_VALUE *source;
      status = jinja_cmeta_activation_cell(activation, binding->source_cell, &source);
      if (status != JINJA_CMETA_OK) return status;
      *cell = *source;
    } else if (binding->load == JINJA_CMETA_CELL_RESOLVE) {
      status = jinja_global_value(provider, provider->instance->templ->cells[binding->cell].name, &cell->value);
      if (status != JINJA_CMETA_OK) return status;
      cell->bound = 1;
    } else if (binding->load == JINJA_CMETA_CELL_ARGUMENT && frame->parent == SIZE_MAX) {
      const vstr name = provider->instance->templ->cells[binding->cell].name;
      if (vstr_eq(name, vstr_from_cstr("self"))) {
        cell->value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TEMPLATE,
            .instance = provider->instance->chain_root, .template_context = provider->context};
        status = jinja_value_identify(provider, &cell->value);
        if (status != JINJA_CMETA_OK) return status;
        cell->bound = 1;
      } else if (vstr_eq(name, vstr_from_cstr("super"))) {
        status = jinja_super_value(provider, provider->active_block, &cell->value);
        if (status != JINJA_CMETA_OK) return status;
        cell->bound = 1;
      }
    }
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_scope_initialize(JINJA_CMETA_PROVIDER *provider,
    size_t scope, JINJA_CMETA_ACTIVATION *parent) {
  if (provider->instance->templ->lexical_scope_count == 0u) return JINJA_CMETA_ERR_METADATA;
  if (scope >= provider->instance->templ->lexical_scope_count) return JINJA_CMETA_ERR_METADATA;
  const JINJA_CMETA_LEXICAL_SCOPE *frame = &provider->instance->templ->lexical_scopes[scope];
  JINJA_CMETA_ACTIVATION *activation = parent != NULL ? parent : provider->activation;
  if (frame->parent == SIZE_MAX ||
      frame->owner != provider->instance->templ->lexical_scopes[frame->parent].owner) {
    JINJA_CMETA_STATUS status = jinja_cmeta_activation_new_for_template(&provider->shared.cells, provider->instance->templ,
        frame->parent == SIZE_MAX ? NULL : activation, frame->owner, &activation);
    if (status != JINJA_CMETA_OK) return status;
  }
  return jinja_scope_load(provider, scope, activation);
}

static size_t jinja_scope_index(const JINJA_CMETA_PROVIDER *provider,
    size_t pc, JINJA_CMETA_SCOPE_PART part) {
  size_t offset = provider->instance->templ->instructions[pc].source_offset;
  for (size_t i = 1u; i < provider->instance->templ->lexical_scope_count; ++i) {
    const JINJA_CMETA_LEXICAL_SCOPE *scope = &provider->instance->templ->lexical_scopes[i];
    if (scope->source_offset == offset && scope->part == part)
      return i;
  }
  return SIZE_MAX;
}

static JINJA_CMETA_STATUS jinja_scope_at(JINJA_CMETA_PROVIDER *provider,
    size_t pc, JINJA_CMETA_SCOPE_PART part, JINJA_CMETA_ACTIVATION *parent) {
  if (provider->instance->templ->lexical_scope_count == 0u) return JINJA_CMETA_ERR_METADATA;
  return jinja_scope_initialize(provider, jinja_scope_index(provider, pc, part), parent);
}

static JINJA_CMETA_STATUS jinja_loop_bind(JINJA_CMETA_PROVIDER *provider, JINJA_CMETA_NODE *node) {
  if (provider->activation == NULL) return JINJA_CMETA_ERR_METADATA;
  const JINJA_CMETA_CELL_BINDING *binding = jinja_lexical_binding(provider, vstr_from_cstr("loop"), 1);
  if (binding == NULL || binding->load != JINJA_CMETA_CELL_ARGUMENT) return JINJA_CMETA_OK;
  JINJA_CMETA_VALUE value = {.kind = JINJA_CMETA_VALUE_LOOP, .loop = node->loop_sequence};
  return jinja_binding_write(provider, vstr_from_cstr("loop"), &value);
}

static JINJA_CMETA_STATUS jinja_slice_normalize(JINJA_CMETA_PROVIDER *provider,
                                                const JINJA_CMETA_VALUE *bounds, uint64_t length,
                                                JINJA_CMETA_RANGE *slice) {
  enum { SLICE_BOUND_COUNT = 3 };
  int64_t parts[SLICE_BOUND_COUNT] = {0, 0, 1};
  int omitted[SLICE_BOUND_COUNT];
  int64_t lower;
  int64_t upper;
  size_t i;
  if (bounds->kind != JINJA_CMETA_VALUE_TUPLE ||
      bounds->collection_item_count != SLICE_BOUND_COUNT || bounds->collection_values == NULL)
    return JINJA_CMETA_ERR_RENDER;
  if (length > INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
  for (i = 0u; i < SLICE_BOUND_COUNT; ++i) {
    int valid;
    JINJA_CMETA_STATUS status;
    const JINJA_CMETA_VALUE *part = &bounds->collection_values[i];
    omitted[i] = part->kind == JINJA_CMETA_VALUE_NONE;
    if (omitted[i]) continue;
    status = jinja_lookup_integer_key(provider, part, &parts[i], &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid) return JINJA_CMETA_ERR_RENDER;
  }
  if (parts[2] == 0) return JINJA_CMETA_ERR_RENDER;
  lower = parts[2] < 0 ? -1 : 0;
  upper = parts[2] < 0 ? (int64_t)length - 1 : (int64_t)length;
  for (i = 0u; i < 2u; ++i) {
    if (omitted[i]) parts[i] = (i == 0u) == (parts[2] < 0) ? upper : lower;
    else {
      if (parts[i] < 0) parts[i] = parts[i] < -(int64_t)length ? lower : parts[i] + (int64_t)length;
      if (parts[i] < lower) parts[i] = lower;
      if (parts[i] > upper) parts[i] = upper;
    }
  }
  *slice = (JINJA_CMETA_RANGE){.start = parts[0], .stop = parts[1], .step = parts[2]};
  if ((slice->step > 0 && slice->start < slice->stop) ||
      (slice->step < 0 && slice->start > slice->stop)) {
    uint64_t distance = slice->step > 0 ? (uint64_t)slice->stop - (uint64_t)slice->start
                                        : (uint64_t)slice->start - (uint64_t)slice->stop;
    uint64_t stride = slice->step > 0 ? (uint64_t)slice->step : UINT64_C(0) - (uint64_t)slice->step;
    slice->count = UINT64_C(1) + (distance - UINT64_C(1)) / stride;
  }
  return JINJA_CMETA_OK;
}

static int jinja_slice_selects(const JINJA_CMETA_RANGE *slice, uint64_t position) {
  if (slice->count == 0u) return 0;
  if (slice->step > 0)
    return position >= (uint64_t)slice->start && position < (uint64_t)slice->stop &&
           (position - (uint64_t)slice->start) % (uint64_t)slice->step == 0u;
  return position <= (uint64_t)slice->start &&
         (slice->stop < 0 || position > (uint64_t)slice->stop) &&
         ((uint64_t)slice->start - position) % (UINT64_C(0) - (uint64_t)slice->step) == 0u;
}

static int jinja_string_bytes_fit(const JINJA_CMETA_PROVIDER *provider, size_t size) {
  return provider->shared.slice_byte_count <= provider->shared.max_string_bytes &&
      provider->shared.pending_string_bytes <= provider->shared.max_string_bytes - provider->shared.slice_byte_count &&
      size <= provider->shared.max_string_bytes - provider->shared.slice_byte_count - provider->shared.pending_string_bytes;
}

/* Three linear scans: validate/count scalars, size output, copy selected whole scalars. */
static JINJA_CMETA_STATUS jinja_slice_string(JINJA_CMETA_PROVIDER *provider,
                                             const JINJA_CMETA_VALUE *base, vstr input,
                                             const JINJA_CMETA_VALUE *bounds,
                                             JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_RANGE slice;
  JINJA_CMETA_STATUS status;
  salts_unicode_scalar scalar;
  size_t cursor = 0u;
  uint64_t count = 0u;
  size_t bytes = 0u;
  size_t output;
  char *destination;
  while (cursor < input.len) {
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    ++count;
  }
  status = jinja_slice_normalize(provider, bounds, count, &slice);
  if (status != JINJA_CMETA_OK) return status;
  if (!jinja_value_is_safe(base) && slice.step == 1 && slice.count == count) {
    *result = *base;
    return JINJA_CMETA_OK;
  }
  cursor = 0u;
  count = 0u;
  while (cursor < input.len) {
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    if (jinja_slice_selects(&slice, count)) bytes += scalar.byte_length;
    ++count;
  }
  if (!jinja_string_bytes_fit(provider, bytes))
    return JINJA_CMETA_ERR_CAPACITY;
  if (bytes == 0u) {
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string = vstr_from_cstr("")};
    return JINJA_CMETA_OK;
  }
  if (provider->shared.slice_bytes == NULL) {
    provider->shared.slice_bytes = (char *)jinja_provider_allocate(provider, provider->shared.max_string_bytes, sizeof(char));
    if (provider->shared.slice_bytes == NULL) return provider->shared.status;
  }
  destination = provider->shared.slice_bytes + provider->shared.slice_byte_count;
  output = slice.step < 0 ? bytes : 0u;
  cursor = 0u;
  count = 0u;
  while (cursor < input.len) {
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    if (jinja_slice_selects(&slice, count)) {
      if (slice.step < 0) output -= scalar.byte_length;
      memcpy(destination + output, input.data + scalar.byte_offset, scalar.byte_length);
      if (slice.step > 0) output += scalar.byte_length;
    }
    ++count;
  }
  provider->shared.slice_byte_count += bytes;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
                                .string = vstr_from_buf(destination, bytes)};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_concat_values(JINJA_CMETA_PROVIDER *provider,
                                              JINJA_CMETA_VALUE *left, JINJA_CMETA_VALUE *right,
                                              JINJA_CMETA_VALUE *result);

static int jinja_value_is_string(const JINJA_CMETA_VALUE *value) {
  return value->kind == JINJA_CMETA_VALUE_STRING ||
         (value->kind == JINJA_CMETA_VALUE_NODE && value->node.desc != NULL &&
          value->node.desc->kind == CMETA_DATA_STRING);
}

static JINJA_CMETA_STATUS jinja_repeat_string(JINJA_CMETA_PROVIDER *provider,
                                              const JINJA_CMETA_VALUE *sequence,
                                              const JINJA_CMETA_VALUE *multiplier,
                                              JINJA_CMETA_VALUE *result);

static JINJA_CMETA_STATUS jinja_repeat_collection(JINJA_CMETA_PROVIDER *provider,
                                                  const JINJA_CMETA_VALUE *sequence,
                                                  const JINJA_CMETA_VALUE *multiplier,
                                                  JINJA_CMETA_VALUE *result) {
  int64_t repetitions;
  int valid;
  size_t count = 0u;
  size_t offset = provider->shared.collection_value_count;
  size_t i;
  JINJA_CMETA_STATUS status = jinja_lookup_integer_key(provider, multiplier, &repetitions, &valid);
  if (status != JINJA_CMETA_OK) return status;
  if (!valid) return JINJA_CMETA_ERR_RENDER;
  if (offset > provider->shared.values.limit) return JINJA_CMETA_ERR_CAPACITY;
  if (sequence->kind == JINJA_CMETA_VALUE_TUPLE && repetitions == 1) {
    *result = *sequence;
    return JINJA_CMETA_OK;
  }
  if (repetitions > 0 && sequence->collection_item_count != 0u) {
    if ((uint64_t)repetitions >
        (provider->shared.values.limit - offset) / sequence->collection_item_count)
      return JINJA_CMETA_ERR_CAPACITY;
    count = (size_t)repetitions * sequence->collection_item_count;
    if (sequence->collection_values == NULL) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_STATUS storage_status = jinja_ensure_collection_storage(provider, count);
    if (storage_status != JINJA_CMETA_OK) return storage_status;
    /* O(result length), bounded by the checked workspace remainder. */
    for (i = 0u; i < count; i += sequence->collection_item_count)
      memcpy(jinja_cmeta_values_at(&provider->shared.values, offset + i), sequence->collection_values,
             sequence->collection_item_count * sizeof(JINJA_CMETA_VALUE));
  }
  provider->shared.collection_value_count += count;
  *result = (JINJA_CMETA_VALUE){.kind = sequence->kind,
                                .collection_item_count = count,
                                .collection_values =
                                    count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_add_collections(JINJA_CMETA_PROVIDER *provider,
                                                const JINJA_CMETA_VALUE *left,
                                                const JINJA_CMETA_VALUE *right,
                                                JINJA_CMETA_VALUE *result) {
  size_t count;
  size_t offset = provider->shared.collection_value_count;
  if (left->kind != right->kind) return JINJA_CMETA_ERR_RENDER;
  if (left->kind == JINJA_CMETA_VALUE_TUPLE &&
      (left->collection_item_count == 0u || right->collection_item_count == 0u)) {
    *result = left->collection_item_count == 0u ? *right : *left;
    return JINJA_CMETA_OK;
  }
  if (right->collection_item_count > SIZE_MAX - left->collection_item_count)
    return JINJA_CMETA_ERR_CAPACITY;
  count = left->collection_item_count + right->collection_item_count;
  if (offset > provider->shared.values.limit ||
      count > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  if (count != 0u && (
                      (left->collection_item_count != 0u && left->collection_values == NULL) ||
                      (right->collection_item_count != 0u && right->collection_values == NULL)))
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STATUS storage_status = jinja_ensure_collection_storage(provider, count);
  if (storage_status != JINJA_CMETA_OK) return storage_status;
  /* Operands are immutable snapshots preceding the newly reserved result region. */
  if (left->collection_item_count != 0u)
    memcpy(jinja_cmeta_values_at(&provider->shared.values, offset), left->collection_values,
           left->collection_item_count * sizeof(JINJA_CMETA_VALUE));
  if (right->collection_item_count != 0u)
    memcpy(jinja_cmeta_values_at(&provider->shared.values, offset + left->collection_item_count),
           right->collection_values,
           right->collection_item_count * sizeof(JINJA_CMETA_VALUE));
  provider->shared.collection_value_count += count;
  *result = (JINJA_CMETA_VALUE){.kind = left->kind,
                                .collection_item_count = count,
                                .collection_values =
                                    count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_absolute_value(JINJA_CMETA_PROVIDER *provider,
                                               const JINJA_CMETA_VALUE *operand,
                                               JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_NUMBER number;
  JINJA_CMETA_STATUS status = jinja_number_from_value(provider, operand, &number);
  if (status != JINJA_CMETA_OK) return status;
  if (number.kind == JINJA_CMETA_NUMBER_FLOAT)
    *result =
        (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_FLOAT, .floating = fabs(number.floating)};
  else {
    if (number.integer == INT64_MIN) return JINJA_CMETA_ERR_CAPACITY;
    const int integer_object = operand->kind == JINJA_CMETA_VALUE_INTEGER ||
        (operand->kind == JINJA_CMETA_VALUE_NODE && operand->node.desc != NULL &&
         (operand->node.desc->kind == CMETA_DATA_SINT || operand->node.desc->kind == CMETA_DATA_UINT));
    if (integer_object && number.integer >= 0) {
      *result = *operand;
      return JINJA_CMETA_OK;
    }
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER,
                                  .integer = number.integer < 0 ? -number.integer : number.integer};
  }
  return JINJA_CMETA_OK;
}

/* Handles borrow stable VALUE slots until render cleanup. Copies share the
   source slot: it alone owns the mutable consumption cursor. */
static JINJA_CMETA_STATUS jinja_create_iterator(JINJA_CMETA_PROVIDER *provider,
                                              JINJA_CMETA_VALUE *operand,
                                              JINJA_CMETA_ITERATOR_KIND kind,
                                              JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE *source;
  JINJA_CMETA_STATUS status;
  if (provider->shared.collection_value_count >= provider->shared.values.limit)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, 1u);
  if (status != JINJA_CMETA_OK) return status;
  jinja_normalize_call_argument(operand);
  source = jinja_cmeta_values_at(&provider->shared.values, provider->shared.collection_value_count++);
  *source = *operand;
  source->iterator_cursor = 0u;
  source->iterator_kind = kind;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_ITERATOR, .iterator = source};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_batch_remaining_bound(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_BATCH *batch, size_t *count);
static JINJA_CMETA_STATUS jinja_transform_remaining_bound(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TRANSFORM *transform, size_t *count);

static JINJA_CMETA_STATUS jinja_iterator_remaining_bound(JINJA_CMETA_PROVIDER *provider,
                                                        const JINJA_CMETA_VALUE *source,
                                                        size_t *count) {
  if (source == NULL) return JINJA_CMETA_ERR_RENDER;
  if (source->iterator_kind == JINJA_CMETA_ITERATOR_TRANSFORM) {
    if (provider->shared.iterator_depth >= provider->shared.max_render_depth) return JINJA_CMETA_ERR_CAPACITY;
    ++provider->shared.iterator_depth;
    JINJA_CMETA_STATUS status = jinja_transform_remaining_bound(provider, source->transform, count);
    --provider->shared.iterator_depth;
    return status;
  }
  if (source->iterator_kind == JINJA_CMETA_ITERATOR_SLICE) {
    const JINJA_CMETA_SLICER *slicer = source->slicer;
    if (slicer == NULL) return JINJA_CMETA_ERR_METADATA;
    if (slicer->initialized) {
      if (slicer->cursor > slicer->count) return JINJA_CMETA_ERR_METADATA;
      *count = slicer->count - slicer->cursor;
      return JINJA_CMETA_OK;
    }
    int64_t columns = 0;
    int valid = 0;
    JINJA_CMETA_STATUS status = jinja_lookup_integer_key(provider, &slicer->columns, &columns, &valid);
    if (status == JINJA_CMETA_OK && valid && columns > 0 &&
        (uint64_t)columns <= provider->shared.node_capacity) {
      *count = (size_t)columns;
    } else {
      /* Invalid counts cannot yield a column, but next must validate input
       * before reporting their error. Negative counts also consume first. */
      *count = 1u;
    }
    return JINJA_CMETA_OK;
  }
  if (source->iterator_kind == JINJA_CMETA_ITERATOR_BATCH) {
    if (source->batch == NULL) return JINJA_CMETA_ERR_METADATA;
    if (provider->shared.iterator_depth >= provider->shared.max_render_depth) return JINJA_CMETA_ERR_CAPACITY;
    ++provider->shared.iterator_depth;
    JINJA_CMETA_STATUS status = jinja_batch_remaining_bound(provider, source->batch, count);
    --provider->shared.iterator_depth;
    return status;
  }
  if (source->kind == JINJA_CMETA_VALUE_LOOP &&
      source->iterator_kind == JINJA_CMETA_ITERATOR_LOOP) {
    const JINJA_CMETA_NODE *sequence = source->loop;
    if (sequence == NULL || sequence->loop_current == NULL) return JINJA_CMETA_ERR_RENDER;
    size_t total = sequence->iterator_initialized ? sequence->iterator_cache_capacity
                                                  : sequence->loop_current->loop_length;
    if (sequence->loop_next_index > total) return JINJA_CMETA_ERR_RENDER;
    *count = total - sequence->loop_next_index;
    if (sequence->loop_peek != NULL) {
      if (*count == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
      ++*count;
    }
    return JINJA_CMETA_OK;
  }
  if (source->iterator_kind == JINJA_CMETA_ITERATOR_REVERSE) {
    if (source->kind != JINJA_CMETA_VALUE_LIST ||
        source->iterator_cursor > source->collection_item_count ||
        (source->collection_item_count != 0u && source->collection_values == NULL))
      return JINJA_CMETA_ERR_RENDER;
    *count = source->collection_item_count - source->iterator_cursor;
    return JINJA_CMETA_OK;
  }
  if (source->iterator_kind != JINJA_CMETA_ITERATOR_ITEMS) return JINJA_CMETA_ERR_RENDER;
  if (source->kind == JINJA_CMETA_VALUE_UNDEFINED) {
    *count = 0u;
    return JINJA_CMETA_OK;
  }
  if (source->kind != JINJA_CMETA_VALUE_DICT ||
      source->iterator_cursor > source->collection_item_count)
    return JINJA_CMETA_ERR_RENDER;
  *count = source->collection_item_count - source->iterator_cursor;
  return JINJA_CMETA_OK;
}

/* Single-render ownership: aliases advance the original sequence, while yielded
 * tuples retain that same receiver until render cleanup. No alias owns a cursor. */
static JINJA_CMETA_STATUS jinja_loop_iterator_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *sequence, JINJA_CMETA_VALUE *result, int *found) {
  enum { LOOP_PAIR_WIDTH = 2 };
  JINJA_CMETA_NODE *child;
  JINJA_CMETA_STATUS status;
  size_t offset;
  if (sequence == NULL || sequence->loop_current == NULL) return JINJA_CMETA_ERR_RENDER;
  child = jinja_loop_advance(provider, sequence);
  if (child == NULL) return provider->shared.status;
  offset = provider->shared.collection_value_count;
  if (offset > provider->shared.values.limit ||
      LOOP_PAIR_WIDTH > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, LOOP_PAIR_WIDTH);
  if (status != JINJA_CMETA_OK) return status;
  (*jinja_cmeta_values_at(&provider->shared.values, offset)) =
      (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NODE, .node = *child};
  jinja_normalize_call_argument(jinja_cmeta_values_at(&provider->shared.values, offset));
  (*jinja_cmeta_values_at(&provider->shared.values, offset + 1u)) =
      (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LOOP, .loop = sequence};
  provider->shared.collection_value_count += LOOP_PAIR_WIDTH;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
      .collection_item_count = LOOP_PAIR_WIDTH,
      .collection_values = jinja_cmeta_values_at(&provider->shared.values, offset)};
  *found = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_batch_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_BATCH *batch, JINJA_CMETA_VALUE *result, int *found);
static JINJA_CMETA_STATUS jinja_slicer_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_SLICER *slicer, JINJA_CMETA_VALUE *result, int *found);
static JINJA_CMETA_STATUS jinja_transform_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TRANSFORM *transform, JINJA_CMETA_VALUE *result, int *found);

static JINJA_CMETA_STATUS jinja_iterator_next_impl(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_VALUE *source,
                                            JINJA_CMETA_VALUE *result, int *found) {
  enum { ITEM_PAIR_WIDTH = 2 };
  if (source != NULL && source->iterator_kind == JINJA_CMETA_ITERATOR_TRANSFORM) {
    if (provider->shared.iterator_depth >= provider->shared.max_render_depth) return JINJA_CMETA_ERR_CAPACITY;
    ++provider->shared.iterator_depth;
    JINJA_CMETA_STATUS status = jinja_transform_next(provider, source->transform, result, found);
    --provider->shared.iterator_depth;
    return status;
  }
  if (source != NULL && source->iterator_kind == JINJA_CMETA_ITERATOR_SLICE) {
    if (provider->shared.iterator_depth >= provider->shared.max_render_depth) return JINJA_CMETA_ERR_CAPACITY;
    ++provider->shared.iterator_depth;
    JINJA_CMETA_STATUS status = jinja_slicer_next(provider, source->slicer, result, found);
    --provider->shared.iterator_depth;
    return status;
  }
  size_t remaining;
  size_t position;
  if (source != NULL && source->iterator_kind == JINJA_CMETA_ITERATOR_BATCH) {
    if (provider->shared.iterator_depth >= provider->shared.max_render_depth) return JINJA_CMETA_ERR_CAPACITY;
    ++provider->shared.iterator_depth;
    JINJA_CMETA_STATUS batch_status = jinja_batch_next(provider, source->batch, result, found);
    --provider->shared.iterator_depth;
    return batch_status;
  }
  JINJA_CMETA_STATUS status = jinja_iterator_remaining_bound(provider, source, &remaining);
  *found = 0;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (status != JINJA_CMETA_OK || remaining == 0u) return status;
  if (source->kind == JINJA_CMETA_VALUE_LOOP)
    return jinja_loop_iterator_next(provider, source->loop, result, found);
  if (source->iterator_kind == JINJA_CMETA_ITERATOR_REVERSE) {
    *result = source->collection_values[remaining - 1u];
    ++source->iterator_cursor;
    *found = 1;
    return JINJA_CMETA_OK;
  }
  /* The existing dictionary equality policy gives O(n^2) traversal, bounded
     by expression/collection limits; duplicate keys keep their first position. */
  for (position = source->iterator_cursor; position < source->collection_item_count; ++position) {
    JINJA_CMETA_VALUE key;
    JINJA_CMETA_VALUE value;
    size_t offset;
    int first;
    int present;
    status = jinja_dict_entry_is_first(provider, source, position, 0u, &first);
    if (status != JINJA_CMETA_OK) return status;
    if (!first) continue;
    offset = provider->shared.collection_value_count;
    if (offset > provider->shared.values.limit ||
        ITEM_PAIR_WIDTH > provider->shared.values.limit - offset)
      return JINJA_CMETA_ERR_CAPACITY;
    status = jinja_dict_entry_value(provider, source, position, 0u, &key, &value);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_dict_lookup(provider, source, &key, 0u, &value, &present);
    if (status != JINJA_CMETA_OK) return status;
    if (!present) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_STATUS storage_status = jinja_ensure_collection_storage(provider, ITEM_PAIR_WIDTH);
    if (storage_status != JINJA_CMETA_OK) return storage_status;
    (*jinja_cmeta_values_at(&provider->shared.values, offset)) = key;
    (*jinja_cmeta_values_at(&provider->shared.values, offset + 1u)) = value;
    provider->shared.collection_value_count += ITEM_PAIR_WIDTH;
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
                                 .collection_item_count = ITEM_PAIR_WIDTH,
                                 .collection_values = jinja_cmeta_values_at(&provider->shared.values, offset)};
    source->iterator_cursor = position + 1u;
    *found = 1;
    return JINJA_CMETA_OK;
  }
  source->iterator_cursor = source->collection_item_count;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_value_identify(JINJA_CMETA_PROVIDER *provider, JINJA_CMETA_VALUE *value) {
  if (value->kind == JINJA_CMETA_VALUE_NODE) value->identity = value->node.value_identity;
  if (value->identity.serial != 0u || value->identity.source != NULL) return JINJA_CMETA_OK;
  if (value->kind == JINJA_CMETA_VALUE_NODE && value->node.expression_kind == 0 &&
      value->node.object != NULL) {
    value->identity = (JINJA_CMETA_IDENTITY){.source = value->node.object, .type = value->node.desc};
    value->node.value_identity = value->identity;
    return JINJA_CMETA_OK;
  }
  if (provider->shared.value_identity == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
  value->identity.serial = ++provider->shared.value_identity;
  if (value->kind == JINJA_CMETA_VALUE_NODE) value->node.value_identity = value->identity;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_iterator_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *source, JINJA_CMETA_VALUE *result, int *found) {
  JINJA_CMETA_STATUS status = jinja_iterator_next_impl(provider, source, result, found);
  return status == JINJA_CMETA_OK && *found ? jinja_value_identify(provider, result) : status;
}

static JINJA_CMETA_STATUS jinja_filtered_cache_until(JINJA_CMETA_PROVIDER *provider,
                                                    JINJA_CMETA_NODE *node, size_t index);

static JINJA_CMETA_STATUS jinja_iterator_cache_until(JINJA_CMETA_PROVIDER *provider,
                                                    JINJA_CMETA_NODE *node, size_t index) {
  JINJA_CMETA_STATUS status;
  if (node->loop_state != NULL && node->loop_state->source != NULL)
    return jinja_filtered_cache_until(provider, node, index);
  if (!node->iterator_initialized) {
    size_t count;
    status = jinja_iterator_remaining_bound(provider, node->iterator, &count);
    if (status != JINJA_CMETA_OK) return status;
    size_t offset = provider->shared.collection_value_count;
    if (count > provider->shared.node_capacity || offset > provider->shared.values.limit ||
        count > provider->shared.values.limit - offset)
      return JINJA_CMETA_ERR_CAPACITY;
    status = jinja_ensure_collection_storage(provider, count);
    if (status != JINJA_CMETA_OK) return status;
    node->iterator_cache = count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL;
    node->iterator_cache_capacity = count;
    provider->shared.collection_value_count += count;
    node->iterator_initialized = 1;
  }
  while (!node->iterator_done && node->iterator_cached_count <= index) {
    JINJA_CMETA_VALUE item;
    int found;
    status = jinja_iterator_next(provider, node->iterator, &item, &found);
    if (status != JINJA_CMETA_OK) return status;
    if (!found) {
      node->iterator_done = 1;
      break;
    }
    if (node->iterator_cached_count >= node->iterator_cache_capacity)
      return JINJA_CMETA_ERR_RENDER;
    node->iterator_cache[node->iterator_cached_count++] = item;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_sequence_edge(JINJA_CMETA_PROVIDER *provider,
                                              JINJA_CMETA_VALUE *operand, int last,
                                              JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = last ? -1 : 0};
  jinja_normalize_call_argument(operand);
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (operand->kind == JINJA_CMETA_VALUE_ITERATOR || operand->kind == JINJA_CMETA_VALUE_LOOP) {
    int found;
    return last ? JINJA_CMETA_ERR_RENDER
                : jinja_iterator_next(provider,
                    operand->kind == JINJA_CMETA_VALUE_LOOP ? operand : operand->iterator,
                    result, &found);
  }
  if (operand->kind == JINJA_CMETA_VALUE_UNDEFINED) return JINJA_CMETA_OK;
  if (operand->kind == JINJA_CMETA_VALUE_DICT) {
    size_t i;
    for (i = 0u; i < operand->collection_item_count; ++i) {
      int first;
      JINJA_CMETA_VALUE ignored;
      JINJA_CMETA_STATUS status = jinja_dict_entry_is_first(provider, operand, i, 0u, &first);
      if (status != JINJA_CMETA_OK) return status;
      if (!first) continue;
      status = jinja_dict_entry_value(provider, operand, i, 0u, result, &ignored);
      if (status != JINJA_CMETA_OK) return status;
      if (!last) break;
    }
    return JINJA_CMETA_OK;
  }
  if (!jinja_value_is_string(operand) && !jinja_value_is_collection(operand->kind) &&
      operand->kind != JINJA_CMETA_VALUE_RANGE &&
      !(operand->kind == JINJA_CMETA_VALUE_NODE && jinja_is_sequence_desc(operand->node.desc)))
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STATUS status = jinja_lookup_item(provider, operand, &key, 0u, result);
  /* first iterates characters; last uses indexing in upstream Jinja. */
  if (status == JINJA_CMETA_OK && !last && jinja_value_is_string(operand))
    result->string_safe = 0;
  return status;
}

static JINJA_CMETA_STATUS jinja_value_length(JINJA_CMETA_PROVIDER *provider,
                                             JINJA_CMETA_VALUE *operand,
                                             JINJA_CMETA_VALUE *result) {
  uint64_t count = 0u;
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  jinja_normalize_call_argument(operand);
  if (operand->kind == JINJA_CMETA_VALUE_LOOP)
    return jinja_loop_attribute(provider, operand->loop, vstr_from_cstr("length"), result);
  if (jinja_value_is_string(operand)) {
    JINJA_CMETA_SCALAR scalar;
    salts_unicode_scalar item;
    size_t cursor = 0u;
    status = jinja_scalar_from_value(provider, operand, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    while (cursor < scalar.size) {
      if (salts_unicode_utf8_next(vstr_from_buf((const char *)scalar.data, scalar.size), &cursor,
                                  &item) != SALTS_UNICODE_OK)
        return JINJA_CMETA_ERR_METADATA;
      ++count;
    }
  } else if (jinja_value_is_collection(operand->kind)) count = operand->collection_item_count;
  else if (operand->kind == JINJA_CMETA_VALUE_RANGE) count = operand->range.count;
  else if (operand->kind == JINJA_CMETA_VALUE_DICT) {
    size_t unique;
    status = jinja_dict_unique_count(provider, operand, 0u, &unique);
    if (status != JINJA_CMETA_OK) return status;
    count = unique;
  } else if (operand->kind == JINJA_CMETA_VALUE_NODE &&
             jinja_is_sequence_desc(operand->node.desc)) {
    const JINJA_CMETA_SEQUENCE_VIEW *view = (const JINJA_CMETA_SEQUENCE_VIEW *)operand->node.object;
    status = jinja_validate_sequence_view(view);
    if (status != JINJA_CMETA_OK) return status;
    count = view->count;
  } else if (operand->kind != JINJA_CMETA_VALUE_UNDEFINED) return JINJA_CMETA_ERR_RENDER;
  if (count > INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)count};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_iteration_bound(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_ITERATION *iteration, size_t *count) {
  if (iteration->input.kind == JINJA_CMETA_VALUE_ITERATOR || iteration->input.kind == JINJA_CMETA_VALUE_LOOP)
    return jinja_iterator_remaining_bound(provider, iteration->input.kind == JINJA_CMETA_VALUE_LOOP
        ? &iteration->input : iteration->input.iterator, count);
  if (!iteration->initialized) {
    JINJA_CMETA_VALUE length;
    JINJA_CMETA_STATUS status = jinja_value_length(provider, &iteration->input, &length);
    if (status != JINJA_CMETA_OK) return status;
    iteration->length = (size_t)length.integer;
    iteration->initialized = 1;
  }
  if (iteration->length < iteration->position) return JINJA_CMETA_ERR_METADATA;
  *count = iteration->length - iteration->position;
  return JINJA_CMETA_OK;
}

/* SIZE_MAX means no possible equal-length boundary within the input quota. */
static JINJA_CMETA_STATUS jinja_batch_width(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_BATCH *batch, size_t *width) {
  const JINJA_CMETA_VALUE *value = &batch->linecount;
  *width = SIZE_MAX;
  if (value->kind != JINJA_CMETA_VALUE_BOOL && value->kind != JINJA_CMETA_VALUE_INTEGER &&
      value->kind != JINJA_CMETA_VALUE_FLOAT &&
      !(value->kind == JINJA_CMETA_VALUE_NODE && value->node.desc != NULL &&
        (value->node.desc->kind == CMETA_DATA_BOOL || value->node.desc->kind == CMETA_DATA_SINT ||
         value->node.desc->kind == CMETA_DATA_UINT || value->node.desc->kind == CMETA_DATA_FLOAT)))
    return JINJA_CMETA_OK;
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, value, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  uint64_t integer = UINT64_MAX;
  if (scalar.kind == JINJA_CMETA_SCALAR_BOOL) integer = scalar.boolean;
  else if (scalar.kind == JINJA_CMETA_SCALAR_SINT && scalar.sint >= 0) integer = (uint64_t)scalar.sint;
  else if (scalar.kind == JINJA_CMETA_SCALAR_UINT) integer = scalar.uint;
  else if (scalar.kind == JINJA_CMETA_SCALAR_FLOAT && scalar.floating >= 0.0 &&
      scalar.floating <= (double)batch->source.max_items && floor(scalar.floating) == scalar.floating)
    integer = (uint64_t)scalar.floating;
  if (integer <= batch->source.max_items) *width = (size_t)integer;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_batch_remaining_bound(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_BATCH *batch, size_t *count) {
  *count = 0u;
  if (batch->done) return JINJA_CMETA_OK;
  size_t items, width;
  JINJA_CMETA_STATUS status = jinja_iteration_bound(provider, &batch->source, &items);
  if (status != JINJA_CMETA_OK) return status;
  if (batch->has_pending) {
    if (items == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
    ++items;
  }
  if (items == 0u) return JINJA_CMETA_OK;
  status = jinja_batch_width(provider, batch, &width);
  if (status != JINJA_CMETA_OK) return status;
  if (width == SIZE_MAX) *count = 1u;
  else if (width == 0u) *count = batch->has_pending ? 1u : 2u;
  else *count = items / width + (items % width != 0u);
  return JINJA_CMETA_OK;
}

/* One cursor per traversal; iterator inputs retain their shared consumption state. */
static JINJA_CMETA_STATUS jinja_iteration_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_ITERATION *iteration, JINJA_CMETA_VALUE *item, int *found) {
  JINJA_CMETA_VALUE *input = &iteration->input;
  JINJA_CMETA_STATUS status;
  *found = 0;
  if (input->kind == JINJA_CMETA_VALUE_ITERATOR || input->kind == JINJA_CMETA_VALUE_LOOP) {
    status = jinja_iterator_next(provider,
        input->kind == JINJA_CMETA_VALUE_LOOP ? input : input->iterator, item, found);
  } else {
    if (!iteration->initialized) {
      JINJA_CMETA_VALUE length;
      status = jinja_value_length(provider, input, &length);
      if (status != JINJA_CMETA_OK) return status;
      iteration->length = (size_t)length.integer;
      iteration->initialized = 1;
    }
    if (iteration->position >= iteration->length) return JINJA_CMETA_OK;
    if (jinja_value_is_string(input)) {
      JINJA_CMETA_SCALAR string;
      salts_unicode_scalar scalar;
      status = jinja_scalar_from_value(provider, input, &string);
      if (status != JINJA_CMETA_OK) return status;
      if (salts_unicode_utf8_next(vstr_from_buf((const char *)string.data, string.size),
          &iteration->string_cursor, &scalar) != SALTS_UNICODE_OK) return JINJA_CMETA_ERR_METADATA;
      *item = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
          .string = vstr_from_buf((const char *)string.data + scalar.byte_offset, scalar.byte_length)};
    } else if (input->kind == JINJA_CMETA_VALUE_DICT) {
      status = jinja_dict_key_by_unique_index(provider, input, iteration->position, 0u, item);
    } else {
      JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)iteration->position};
      status = jinja_lookup_item(provider, input, &key, 0u, item);
    }
    if (status == JINJA_CMETA_OK) *found = 1;
  }
  if (status != JINJA_CMETA_OK || !*found) return status;
  if (iteration->position >= iteration->max_items) return JINJA_CMETA_ERR_CAPACITY;
  ++iteration->position;
  jinja_normalize_call_argument(item);
  return jinja_value_identify(provider, item);
}

static JINJA_CMETA_STATUS jinja_batch_append(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_BATCH *batch, JINJA_CMETA_VALUE **row, size_t *count,
    const JINJA_CMETA_VALUE *item) {
  if (*count >= batch->row_limit) return JINJA_CMETA_ERR_CAPACITY;
  if (*row == NULL) {
    size_t remaining, width;
    JINJA_CMETA_STATUS status = jinja_iteration_bound(provider, &batch->source, &remaining);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_batch_width(provider, batch, &width);
    if (status != JINJA_CMETA_OK) return status;
    if (width != 0u && width < batch->row_limit) batch->row_limit = width;
    /* Only a reachable positive width can require retained padding slots. */
    if ((batch->fill.kind == JINJA_CMETA_VALUE_NONE || width == 0u || width == SIZE_MAX) &&
        remaining < batch->row_limit)
      batch->row_limit = remaining + 1u;
    if (provider->shared.collection_value_count > provider->shared.values.limit ||
        batch->row_limit > provider->shared.values.limit - provider->shared.collection_value_count)
      return JINJA_CMETA_ERR_CAPACITY;
    status = jinja_ensure_collection_storage(provider, batch->row_limit);
    if (status != JINJA_CMETA_OK) return status;
    *row = jinja_cmeta_values_at(&provider->shared.values, provider->shared.collection_value_count);
    provider->shared.collection_value_count += batch->row_limit;
  }
  (*row)[(*count)++] = *item;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_batch_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_BATCH *batch, JINJA_CMETA_VALUE *result, int *found) {
  JINJA_CMETA_VALUE *row = NULL;
  size_t count = 0u;
  JINJA_CMETA_STATUS status;
  *found = 0;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (batch == NULL) return JINJA_CMETA_ERR_METADATA;
  if (batch->done) return JINJA_CMETA_OK;
  if (batch->has_pending) {
    status = jinja_batch_append(provider, batch, &row, &count, &batch->pending);
    if (status != JINJA_CMETA_OK) return status;
    batch->has_pending = 0;
  }
  for (;;) {
    JINJA_CMETA_VALUE item;
    int present, equal;
    status = jinja_iteration_next(provider, &batch->source, &item, &present);
    if (status != JINJA_CMETA_OK) return status;
    JINJA_CMETA_VALUE size = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)count};
    if (!present) {
      batch->done = 1;
      if (count == 0u) return JINJA_CMETA_OK;
      if (batch->fill.kind != JINJA_CMETA_VALUE_NONE) {
        int less;
        status = jinja_compare_values(provider, &size, &batch->linecount,
            JINJA_CMETA_COMPARISON_LESS, 0u, &less);
        if (status != JINJA_CMETA_OK) return status;
        if (less) {
          JINJA_CMETA_SCALAR width;
          status = jinja_scalar_from_value(provider, &batch->linecount, &width);
          if (status != JINJA_CMETA_OK) return status;
          uint64_t target;
          if (width.kind == JINJA_CMETA_SCALAR_BOOL) target = width.boolean;
          else if (width.kind == JINJA_CMETA_SCALAR_UINT) target = width.uint;
          else if (width.kind == JINJA_CMETA_SCALAR_SINT && width.sint > 0) target = (uint64_t)width.sint;
          else return JINJA_CMETA_ERR_RENDER;
          if (target > batch->row_limit) return JINJA_CMETA_ERR_CAPACITY;
          while (count < target) {
            status = jinja_batch_append(provider, batch, &row, &count, &batch->fill);
            if (status != JINJA_CMETA_OK) return status;
          }
        }
      }
      break;
    }
    status = jinja_compare_values(provider, &size, &batch->linecount,
        JINJA_CMETA_COMPARISON_EQUAL, 0u, &equal);
    if (status != JINJA_CMETA_OK) return status;
    if (equal) {
      batch->pending = item;
      batch->has_pending = 1;
      break;
    }
    status = jinja_batch_append(provider, batch, &row, &count, &item);
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
      .collection_values = row, .collection_item_count = count};
  *found = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_batch_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand,
    const JINJA_CMETA_VALUE *width, const JINJA_CMETA_VALUE *fill, JINJA_CMETA_VALUE *result) {
  if (provider->shared.batch_count >= provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_BATCH *batch = (JINJA_CMETA_BATCH *)jinja_provider_zero(provider, 1u, sizeof(*batch));
  if (batch == NULL) return provider->shared.status;
  batch->source.input = *operand;
  jinja_normalize_call_argument(&batch->source.input);
  batch->linecount = *width;
  batch->fill = fill != NULL ? *fill : (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NONE};
  jinja_normalize_call_argument(&batch->fill);
  batch->row_limit = batch->source.max_items = provider->shared.node_capacity;
  if (width->kind == JINJA_CMETA_VALUE_INTEGER && width->integer > 0 &&
      (uint64_t)width->integer < batch->row_limit) batch->row_limit = (size_t)width->integer;
  else if (width->kind == JINJA_CMETA_VALUE_BOOL && width->boolean) batch->row_limit = 1u;
  else if (width->kind == JINJA_CMETA_VALUE_FLOAT && width->floating >= 1.0 &&
      width->floating < (double)batch->row_limit && floor(width->floating) == width->floating)
    batch->row_limit = (size_t)width->floating;
  JINJA_CMETA_STATUS status = jinja_create_iterator(provider, operand, JINJA_CMETA_ITERATOR_BATCH, result);
  if (status != JINJA_CMETA_OK) { jinja_cmeta_memory_drop(batch); return status; }
  result->iterator->batch = batch;
  batch->next = provider->shared.batches;
  provider->shared.batches = batch;
  ++provider->shared.batch_count;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_materialize_list(JINJA_CMETA_PROVIDER *provider,
                                                 JINJA_CMETA_VALUE *operand,
                                                 JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE length;
  JINJA_CMETA_SCALAR string = {0};
  size_t offset = provider->shared.collection_value_count;
  size_t cursor = 0u;
  size_t dictionary_position = 0u;
  size_t count;
  size_t i;
  JINJA_CMETA_STATUS status;
  jinja_normalize_call_argument(operand);
  if (operand->kind == JINJA_CMETA_VALUE_ITERATOR || operand->kind == JINJA_CMETA_VALUE_LOOP) {
    JINJA_CMETA_NODE consumer = {
        .iterator = operand->kind == JINJA_CMETA_VALUE_LOOP ? operand : operand->iterator};
    status = jinja_iterator_cache_until(provider, &consumer, SIZE_MAX);
    if (status != JINJA_CMETA_OK) return status;
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
                                 .collection_item_count = consumer.iterator_cached_count,
                                 .collection_values = consumer.iterator_cache};
    return JINJA_CMETA_OK;
  }
  status = jinja_value_length(provider, operand, &length);
  if (status != JINJA_CMETA_OK) return status;
  if ((uint64_t)length.integer > provider->shared.node_capacity ||
      offset > provider->shared.values.limit ||
      (uint64_t)length.integer > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  count = (size_t)length.integer;
  if (jinja_value_is_string(operand)) {
    status = jinja_scalar_from_value(provider, operand, &string);
    if (status != JINJA_CMETA_OK) return status;
  }
  status = jinja_ensure_collection_storage(provider, count);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.collection_value_count += count;
  /* String decoding is linear; dictionary uniqueness uses the bounded existing key policy. */
  for (i = 0u; i < count; ++i) {
    JINJA_CMETA_VALUE *item = jinja_cmeta_values_at(&provider->shared.values, offset + i);
    if (jinja_value_is_string(operand)) {
      salts_unicode_scalar scalar;
      if (salts_unicode_utf8_next(vstr_from_buf((const char *)string.data, string.size), &cursor,
                                  &scalar) != SALTS_UNICODE_OK)
        return JINJA_CMETA_ERR_METADATA;
      *item = (JINJA_CMETA_VALUE){
          .kind = JINJA_CMETA_VALUE_STRING,
          .string =
              vstr_from_buf((const char *)string.data + scalar.byte_offset, scalar.byte_length)};
    } else if (operand->kind == JINJA_CMETA_VALUE_DICT) {
      int first = 0;
      JINJA_CMETA_VALUE ignored;
      while (dictionary_position < operand->collection_item_count) {
        status = jinja_dict_entry_is_first(provider, operand, dictionary_position, 0u, &first);
        if (status != JINJA_CMETA_OK) return status;
        if (first) break;
        ++dictionary_position;
      }
      if (!first) return JINJA_CMETA_ERR_RENDER;
      status = jinja_dict_entry_value(provider, operand, dictionary_position++, 0u, item, &ignored);
    } else {
      JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)i};
      status = jinja_lookup_item(provider, operand, &key, 0u, item);
    }
    if (status != JINJA_CMETA_OK) return status;
    jinja_normalize_call_argument(item);
    status = jinja_value_identify(provider, item);
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
                                .collection_item_count = count,
                                .collection_values =
                                    count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  return JINJA_CMETA_OK;
}

/* Input is snapshotted once on first consumption; each published column owns
 * stable VALUE slots. Total copy work is O(input items + columns). */
static JINJA_CMETA_STATUS jinja_slicer_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_SLICER *slicer, JINJA_CMETA_VALUE *result, int *found) {
  *found = 0;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (slicer == NULL) return JINJA_CMETA_ERR_METADATA;
  if (!slicer->initialized) {
    JINJA_CMETA_VALUE input;
    JINJA_CMETA_STATUS status = jinja_materialize_list(provider, &slicer->input, &input);
    if (status != JINJA_CMETA_OK) return status;
    int64_t columns = 0;
    int valid = 0;
    status = jinja_lookup_integer_key(provider, &slicer->columns, &columns, &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid || columns == 0) return JINJA_CMETA_ERR_RENDER;
    if (columns > 0 && (uint64_t)columns > provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
    slicer->input = input;
    slicer->count = columns > 0 ? (size_t)columns : 0u;
    slicer->initialized = 1;
  }
  if (slicer->cursor >= slicer->count) return JINJA_CMETA_OK;
  const size_t length = slicer->input.collection_item_count;
  const size_t base = length / slicer->count;
  const size_t extra = length % slicer->count;
  const size_t index = slicer->cursor;
  const size_t start = index * base + (index < extra ? index : extra);
  const size_t items = base + (index < extra);
  const int padding = slicer->fill.kind != JINJA_CMETA_VALUE_NONE && index >= extra;
  if (items > provider->shared.node_capacity || (padding && items == provider->shared.node_capacity))
    return JINJA_CMETA_ERR_CAPACITY;
  const size_t count = items + (size_t)padding;
  const size_t offset = provider->shared.collection_value_count;
  if (offset > provider->shared.values.limit || count > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_STATUS status = jinja_ensure_collection_storage(provider, count);
  if (status != JINJA_CMETA_OK) return status;
  if (items != 0u)
    memcpy(jinja_cmeta_values_at(&provider->shared.values, offset), slicer->input.collection_values + start,
        items * sizeof(JINJA_CMETA_VALUE));
  if (padding) (*jinja_cmeta_values_at(&provider->shared.values, offset + items)) = slicer->fill;
  provider->shared.collection_value_count += count;
  ++slicer->cursor;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
      .collection_item_count = count,
      .collection_values = count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  *found = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_slicer_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *columns,
    const JINJA_CMETA_VALUE *fill, JINJA_CMETA_VALUE *result) {
  if (provider->shared.slicer_count >= provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_SLICER *slicer = (JINJA_CMETA_SLICER *)jinja_provider_zero(provider, 1u, sizeof(*slicer));
  if (slicer == NULL) return provider->shared.status;
  slicer->input = *operand;
  jinja_normalize_call_argument(&slicer->input);
  slicer->columns = *columns;
  slicer->fill = fill != NULL ? *fill : (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NONE};
  jinja_normalize_call_argument(&slicer->fill);
  JINJA_CMETA_STATUS status = jinja_create_iterator(provider, operand, JINJA_CMETA_ITERATOR_SLICE, result);
  if (status != JINJA_CMETA_OK) { jinja_cmeta_memory_drop(slicer); return status; }
  result->iterator->slicer = slicer;
  slicer->next = provider->shared.slicers;
  provider->shared.slicers = slicer;
  ++provider->shared.slicer_count;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_slice_value(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_VALUE *base,
                                            const JINJA_CMETA_VALUE *bounds, size_t depth,
                                            JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_RANGE slice;
  JINJA_CMETA_STATUS status;
  uint64_t length;
  size_t offset;
  size_t i;
  jinja_normalize_call_argument(base);
  if (base->kind == JINJA_CMETA_VALUE_STRING ||
      (base->kind == JINJA_CMETA_VALUE_NODE && base->node.desc != NULL &&
       base->node.desc->kind == CMETA_DATA_STRING)) {
    JINJA_CMETA_SCALAR scalar;
    status = jinja_scalar_from_value(provider, base, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_slice_string(provider, base, vstr_from_buf((const char *)scalar.data, scalar.size),
                                bounds, result);
    if (status == JINJA_CMETA_OK) result->string_safe = jinja_value_is_safe(base);
    return status;
  }
  if (base->kind == JINJA_CMETA_VALUE_RANGE) length = base->range.count;
  else if (jinja_value_is_collection(base->kind)) length = base->collection_item_count;
  else if (base->kind == JINJA_CMETA_VALUE_NODE && jinja_is_sequence_desc(base->node.desc)) {
    const JINJA_CMETA_SEQUENCE_VIEW *view = (const JINJA_CMETA_SEQUENCE_VIEW *)base->node.object;
    status = jinja_validate_sequence_view(view);
    if (status != JINJA_CMETA_OK) return status;
    length = view->count;
  } else return JINJA_CMETA_ERR_RENDER;
  status = jinja_slice_normalize(provider, bounds, length, &slice);
  if (status != JINJA_CMETA_OK) return status;
  if (base->kind == JINJA_CMETA_VALUE_TUPLE && slice.step == 1 && slice.count == length) {
    *result = *base;
    return JINJA_CMETA_OK;
  }
  if (base->kind == JINJA_CMETA_VALUE_RANGE) {
    int64_t start;
    int64_t stop;
    int64_t step;
    status = jinja_checked_multiply(base->range.step, slice.step, &step);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_checked_multiply(base->range.step, slice.start, &start);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_checked_add(base->range.start, start, &start);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_checked_multiply(base->range.step, slice.stop, &stop);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_checked_add(base->range.start, stop, &stop);
    if (status != JINJA_CMETA_OK) return status;
    if (provider->shared.range_identity == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_RANGE,
        .range = {.start = start, .stop = stop, .step = step, .count = slice.count,
                  .identity = ++provider->shared.range_identity}};
    return JINJA_CMETA_OK;
  }
  if (provider->shared.collection_value_count > provider->shared.values.limit ||
      slice.count > provider->shared.values.limit - provider->shared.collection_value_count)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, (size_t)slice.count);
  if (status != JINJA_CMETA_OK) return status;
  offset = provider->shared.collection_value_count;
  provider->shared.collection_value_count += (size_t)slice.count;
  for (i = 0u; i < (size_t)slice.count; ++i) {
    JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_INTEGER,
                             .integer = jinja_range_item(&slice, i)};
    status = jinja_lookup_item(provider, base, &key, depth + 1u,
                               jinja_cmeta_values_at(&provider->shared.values, offset + i));
    if (status != JINJA_CMETA_OK) return status;
    jinja_normalize_call_argument(jinja_cmeta_values_at(&provider->shared.values, offset + i));
  }
  *result = (JINJA_CMETA_VALUE){
      .kind =
          base->kind == JINJA_CMETA_VALUE_TUPLE ? JINJA_CMETA_VALUE_TUPLE : JINJA_CMETA_VALUE_LIST,
      .collection_item_count = (size_t)slice.count,
      .collection_values = slice.count != 0u ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_reverse_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  static const JINJA_CMETA_VALUE parts[] = {
      {.kind = JINJA_CMETA_VALUE_NONE}, {.kind = JINJA_CMETA_VALUE_NONE},
      {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = -1}};
  const JINJA_CMETA_VALUE bounds = {.kind = JINJA_CMETA_VALUE_TUPLE,
      .collection_values = parts, .collection_item_count = sizeof(parts) / sizeof(parts[0])};
  JINJA_CMETA_VALUE list;
  JINJA_CMETA_STATUS status;
  jinja_normalize_call_argument(operand);
  if (jinja_value_is_string(operand))
    return jinja_slice_value(provider, operand, &bounds, 0u, result);
  status = jinja_materialize_list(provider, operand, &list);
  if (status != JINJA_CMETA_OK) return status;
  if (operand->kind == JINJA_CMETA_VALUE_ITERATOR || operand->kind == JINJA_CMETA_VALUE_LOOP)
    return jinja_slice_value(provider, &list, &bounds, 0u, result);
  return jinja_create_iterator(provider, &list, JINJA_CMETA_ITERATOR_REVERSE, result);
}

static JINJA_CMETA_STATUS jinja_string_value(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_VALUE *operand,
                                            JINJA_CMETA_VALUE *value) {
  if (provider->strict_undefined && operand != NULL &&
      operand->kind == JINJA_CMETA_VALUE_UNDEFINED)
    return JINJA_CMETA_ERR_RENDER;
  jinja_normalize_call_argument(operand);
  if (jinja_value_is_string(operand)) {
    JINJA_CMETA_SCALAR scalar;
    JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, operand, &scalar);
    if (status == JINJA_CMETA_OK)
      *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
          .identity = operand->identity,
          .string_safe = jinja_value_is_safe(operand),
          .string = vstr_from_buf((const char *)scalar.data, scalar.size)};
    return status;
  }
  JINJA_CMETA_VALUE empty = {.kind = JINJA_CMETA_VALUE_STRING, .string = vstr_from_cstr("")};
  return jinja_concat_values(provider, operand, &empty, value);
}

static int jinja_concat_write(const char *text, size_t size, void *opaque);
static JINJA_CMETA_STATUS jinja_case_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, salts_unicode_case_mode mode, JINJA_CMETA_VALUE *result);
static JINJA_CMETA_STATUS jinja_dump_value_repr(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *value, size_t depth,
    int (*out_fn)(const char *, size_t, void *), void *renderer_data);
static JINJA_CMETA_STATUS jinja_urlencode_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result);
static JINJA_CMETA_STATUS jinja_format_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *format, const JINJA_CMETA_VALUE *arguments, JINJA_CMETA_VALUE *result);
static JINJA_CMETA_STATUS jinja_xmlattr_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *autospace, JINJA_CMETA_VALUE *result);
static JINJA_CMETA_STATUS jinja_wordwrap_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *const parameters[], JINJA_CMETA_VALUE *result);

static JINJA_CMETA_STATUS jinja_wordcount_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  if (string.string.len > provider->shared.max_string_bytes) return JINJA_CMETA_ERR_CAPACITY;
  size_t count;
  if (!jinja_text_wordcount(string.string, &count)) return JINJA_CMETA_ERR_METADATA;
  if (count > (size_t)INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)count};
  return JINJA_CMETA_OK;
}

static int jinja_html_write(const char *text, size_t size,
                            int (*write)(const char *, size_t, void *), void *opaque);

static JINJA_CMETA_STATUS jinja_safety_value(JINJA_CMETA_PROVIDER *provider,
                                             JINJA_CMETA_VALUE *operand,
                                             JINJA_CMETA_EXPRESSION_KIND kind,
                                             JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, value);
  size_t start = provider->shared.slice_byte_count;
  if (status != JINJA_CMETA_OK) return status;
  if (kind != JINJA_CMETA_EXPRESSION_ESCAPE || !value->string_safe)
    value->identity = (JINJA_CMETA_IDENTITY){0};
  if (kind != JINJA_CMETA_EXPRESSION_SAFE &&
      (kind == JINJA_CMETA_EXPRESSION_FORCEESCAPE || !value->string_safe) && value->string.len != 0u) {
    if (jinja_html_write(value->string.data, value->string.len, jinja_concat_write, provider) != 0)
      return provider->shared.status;
    value->string = vstr_from_buf(provider->shared.slice_bytes + start, provider->shared.slice_byte_count - start);
  }
  value->string_safe = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_markup_concat(JINJA_CMETA_PROVIDER *provider,
                                               JINJA_CMETA_VALUE *left,
                                               JINJA_CMETA_VALUE *right,
                                               JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE escaped_left, escaped_right;
  JINJA_CMETA_STATUS status;
  if (!jinja_value_is_safe(left) && !jinja_value_is_safe(right))
    return jinja_concat_values(provider, left, right, result);
  status = jinja_safety_value(provider, left, JINJA_CMETA_EXPRESSION_ESCAPE, &escaped_left);
  if (status == JINJA_CMETA_OK)
    status = jinja_safety_value(provider, right, JINJA_CMETA_EXPRESSION_ESCAPE, &escaped_right);
  if (status == JINJA_CMETA_OK)
    status = jinja_concat_values(provider, &escaped_left, &escaped_right, result);
  if (status == JINJA_CMETA_OK) result->string_safe = 1;
  return status;
}

static JINJA_CMETA_STATUS jinja_center_padding(JINJA_CMETA_PROVIDER *provider, size_t count) {
  static const char spaces[] = "                                ";
  while (count != 0u) {
    size_t chunk = count < sizeof(spaces) - 1u ? count : sizeof(spaces) - 1u;
    if (jinja_concat_write(spaces, chunk, provider) != 0) return provider->shared.status;
    count -= chunk;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_center_value(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_VALUE *operand,
                                            const JINJA_CMETA_VALUE *width_value,
                                            JINJA_CMETA_VALUE *value) {
  enum { CENTER_DEFAULT_WIDTH = 80 };
  JINJA_CMETA_VALUE string, length;
  int64_t width = CENTER_DEFAULT_WIDTH;
  int valid;
  size_t padding, left, offset;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  if (width_value != NULL) {
    status = jinja_lookup_integer_key(provider, width_value, &width, &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid) return JINJA_CMETA_ERR_RENDER;
  }
  status = jinja_value_length(provider, &string, &length);
  if (status != JINJA_CMETA_OK) return status;
  if (width <= length.integer) {
    *value = string;
    if (value->string_safe) value->identity = (JINJA_CMETA_IDENTITY){0};
    return JINJA_CMETA_OK;
  }
  if ((uint64_t)(width - length.integer) > SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
  padding = (size_t)(width - length.integer);
  offset = provider->shared.slice_byte_count;
  if (offset > provider->shared.max_string_bytes ||
      string.string.len > provider->shared.max_string_bytes - offset ||
      padding > provider->shared.max_string_bytes - offset - string.string.len)
    return JINJA_CMETA_ERR_CAPACITY;
  /* Match Python's odd-margin placement; width counts scalars, storage counts bytes.
     O(input bytes + padding) time, output retained in the render-owned byte workspace. */
  left = padding / 2u + (padding & (size_t)width & 1u);
  status = jinja_center_padding(provider, left);
  if (status == JINJA_CMETA_OK &&
      jinja_concat_write(string.string.data, string.string.len, provider) != 0)
    status = provider->shared.status;
  if (status == JINJA_CMETA_OK) status = jinja_center_padding(provider, padding - left);
  if (status != JINJA_CMETA_OK) return status;
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = string.string_safe,
      .string = vstr_from_buf(provider->shared.slice_bytes + offset, string.string.len + padding)};
  return JINJA_CMETA_OK;
}

/* Jinja splitlines normalizes these separators, but not tab or U+001F. */
static int jinja_text_linebreak(uint32_t scalar) {
  enum { NEXT_LINE = 0x85, LINE_SEPARATOR = 0x2028, PARAGRAPH_SEPARATOR = 0x2029,
         FILE_SEPARATOR = 0x1c, GROUP_SEPARATOR = 0x1d, RECORD_SEPARATOR = 0x1e };
  return scalar == '\n' || scalar == '\r' || scalar == '\v' || scalar == '\f' ||
      scalar == NEXT_LINE || scalar == LINE_SEPARATOR || scalar == PARAGRAPH_SEPARATOR ||
      scalar == FILE_SEPARATOR || scalar == GROUP_SEPARATOR || scalar == RECORD_SEPARATOR;
}

static JINJA_CMETA_STATUS jinja_indent_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *const parameters[],
    JINJA_CMETA_VALUE *result) {
  enum { WIDTH, FIRST, BLANK, DEFAULT_WIDTH = 4 };
  JINJA_CMETA_VALUE string, prefix = {.kind = JINJA_CMETA_VALUE_STRING, .string = {"", 0u}};
  int64_t width = DEFAULT_WIDTH;
  int first = 0, blank = 0, valid, string_width = 0;
  JINJA_CMETA_STATUS status;
  if (!jinja_value_is_string(operand)) return JINJA_CMETA_ERR_RENDER;
  status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  if (parameters[WIDTH] != NULL) {
    string_width = jinja_value_is_string(parameters[WIDTH]);
    if (string_width) {
      JINJA_CMETA_VALUE argument = *parameters[WIDTH];
      status = jinja_string_value(provider, &argument, &prefix);
    } else {
      status = jinja_lookup_integer_key(provider, parameters[WIDTH], &width, &valid);
      if (status == JINJA_CMETA_OK && !valid) status = JINJA_CMETA_ERR_RENDER;
      if (width < 0) width = 0;
      if ((uint64_t)width > SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;
    }
    if (status != JINJA_CMETA_OK) return status;
  }
  if (parameters[FIRST] != NULL) status = jinja_value_truthy(provider, parameters[FIRST], &first);
  if (status == JINJA_CMETA_OK && parameters[BLANK] != NULL)
    status = jinja_value_truthy(provider, parameters[BLANK], &blank);
  if (status != JINJA_CMETA_OK) return status;
  prefix.string_safe |= string.string_safe;
  size_t start = provider->shared.slice_byte_count, cursor = 0u;
  /* O(input + output bytes). The virtual final LF reproduces Jinja's trailing
   * empty line, including its merge with a trailing CR, without copying input. */
  for (size_t line_index = 0u;; ++line_index) {
    size_t begin = cursor, end = cursor;
    int last = 1;
    while (cursor < string.string.len) {
      salts_unicode_scalar scalar;
      if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
        return JINJA_CMETA_ERR_METADATA;
      if (jinja_text_linebreak(scalar.value)) {
        end = scalar.byte_offset;
        last = scalar.value == '\r' && cursor == string.string.len;
        if (scalar.value == '\r' && cursor < string.string.len && string.string.data[cursor] == '\n')
          ++cursor;
        break;
      }
      end = cursor;
    }
    if (line_index != 0u) {
      if (jinja_concat_write("\n", 1u, provider) != 0) return provider->shared.status;
      if (blank || end != begin) {
        status = string_width
            ? (jinja_concat_write(prefix.string.data, prefix.string.len, provider) == 0
                ? JINJA_CMETA_OK : provider->shared.status)
            : jinja_center_padding(provider, (size_t)width);
        if (status != JINJA_CMETA_OK) return status;
      }
    }
    vstr line = end == begin ? vstr_from_cstr("")
        : vstr_from_buf(string.string.data + begin, end - begin);
    int escape = !string.string_safe && prefix.string_safe && (blank || line_index != 0u);
    if ((escape ? jinja_html_write(line.data, line.len, jinja_concat_write, provider)
                : jinja_concat_write(line.data, line.len, provider)) != 0)
      return provider->shared.status;
    if (last) break;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = string.string_safe || (blank && prefix.string_safe),
      .string = provider->shared.slice_byte_count == start ? vstr_from_cstr("")
          : vstr_from_buf(provider->shared.slice_bytes + start, provider->shared.slice_byte_count - start)};
  if (!first) return JINJA_CMETA_OK;
  if (!string_width) {
    size_t padding = provider->shared.slice_byte_count;
    status = jinja_center_padding(provider, (size_t)width);
    if (status != JINJA_CMETA_OK) return status;
    prefix.string = width == 0 ? vstr_from_cstr("")
        : vstr_from_buf(provider->shared.slice_bytes + padding, (size_t)width);
  }
  /* A Markup prefix promotes and escapes the complete first=True result. */
  JINJA_CMETA_VALUE body = *result;
  return jinja_markup_concat(provider, &prefix, &body, result);
}

static JINJA_CMETA_STATUS jinja_truncate_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *const parameters[],
    JINJA_CMETA_VALUE *result) {
  enum { LENGTH, KILLWORDS, END, LEEWAY, DEFAULT_LENGTH = 255, DEFAULT_LEEWAY = 5 };
  JINJA_CMETA_NUMBER length = {.kind = JINJA_CMETA_NUMBER_INTEGER, .integer = DEFAULT_LENGTH};
  JINJA_CMETA_NUMBER leeway = {.kind = JINJA_CMETA_NUMBER_INTEGER, .integer = DEFAULT_LEEWAY};
  JINJA_CMETA_VALUE suffix = {.kind = JINJA_CMETA_VALUE_STRING, .string = {"...", sizeof("...") - 1u}};
  JINJA_CMETA_VALUE input_length, suffix_length, prefix;
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  if (parameters[LENGTH] != NULL) status = jinja_number_from_value(provider, parameters[LENGTH], &length);
  if (status == JINJA_CMETA_OK && parameters[LEEWAY] != NULL && parameters[LEEWAY]->kind != JINJA_CMETA_VALUE_NONE)
    status = jinja_number_from_value(provider, parameters[LEEWAY], &leeway);
  if (status != JINJA_CMETA_OK) return status;
  if (parameters[END] != NULL) suffix = *parameters[END];
  status = jinja_value_length(provider, &suffix, &suffix_length);
  if (status != JINJA_CMETA_OK) return status;
  if ((length.kind == JINJA_CMETA_NUMBER_INTEGER ? length.integer < suffix_length.integer
          : !(length.floating >= (double)suffix_length.integer)) ||
      (leeway.kind == JINJA_CMETA_NUMBER_INTEGER ? leeway.integer < 0 : !(leeway.floating >= 0.0)))
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_value_length(provider, operand, &input_length);
  if (status != JINJA_CMETA_OK) return status;
  int unchanged = length.kind == JINJA_CMETA_NUMBER_INTEGER && leeway.kind == JINJA_CMETA_NUMBER_INTEGER
      ? (uint64_t)input_length.integer <= (uint64_t)length.integer + (uint64_t)leeway.integer
      : (double)input_length.integer <=
          (length.kind == JINJA_CMETA_NUMBER_INTEGER ? (double)length.integer : length.floating) +
          (leeway.kind == JINJA_CMETA_NUMBER_INTEGER ? (double)leeway.integer : leeway.floating);
  if (unchanged) { *result = *operand; return JINJA_CMETA_OK; }
  if (length.kind != JINJA_CMETA_NUMBER_INTEGER ||
      !jinja_value_is_string(operand) || !jinja_value_is_string(&suffix)) return JINJA_CMETA_ERR_RENDER;
  int killwords = 0;
  if (parameters[KILLWORDS] != NULL) {
    status = jinja_value_truthy(provider, parameters[KILLWORDS], &killwords);
    if (status != JINJA_CMETA_OK) return status;
  }
  status = jinja_string_value(provider, operand, &prefix);
  if (status != JINJA_CMETA_OK) return status;
  /* Lengths count Unicode scalars; word truncation splits only on ASCII space. */
  prefix.string = vstr_utf8_sub(prefix.string, 0u, (size_t)(length.integer - suffix_length.integer));
  prefix.identity = (JINJA_CMETA_IDENTITY){0};
  if (!killwords) {
    vstr before, match, after;
    if (vstr_rpartition(prefix.string, vstr_from_cstr(" "), &before, &match, &after)) prefix.string = before;
  }
  return jinja_markup_concat(provider, &prefix, &suffix, result);
}

static JINJA_CMETA_STATUS jinja_trim_value(JINJA_CMETA_PROVIDER *provider,
                                          JINJA_CMETA_VALUE *operand,
                                          const JINJA_CMETA_VALUE *chars,
                                          JINJA_CMETA_VALUE *value) {
  enum { PYTHON_SPACE_FIRST = 0x1c, PYTHON_SPACE_LAST = 0x1f };
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_SCALAR charset;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  vstr characters = vstr_from_cstr("");
  int custom = chars != NULL && chars->kind != JINJA_CMETA_VALUE_NONE;
  size_t cursor = 0u, first, last = 0u;
  if (status != JINJA_CMETA_OK) return status;
  if (custom) {
    if (!jinja_value_is_string(chars)) return JINJA_CMETA_ERR_RENDER;
    status = jinja_scalar_from_value(provider, chars, &charset);
    if (status != JINJA_CMETA_OK) return status;
    characters = vstr_from_buf((const char *)charset.data, charset.size);
    if (vstr_utf8_invalid_offset(characters) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  }
  first = string.string.len;
  /* O(input scalars * charset bytes), O(1) extra storage, bounded by render bytes.
     A complete scalar cannot match inside another valid UTF-8 scalar. */
  while (cursor < string.string.len) {
    salts_unicode_scalar scalar;
    int strip;
    if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    strip = custom
        ? vstr_contains(characters, vstr_from_buf(string.string.data + scalar.byte_offset,
                                                  scalar.byte_length))
        : ((scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) != 0u ||
           (scalar.value >= PYTHON_SPACE_FIRST && scalar.value <= PYTHON_SPACE_LAST));
    if (!strip) {
      if (first == string.string.len) first = scalar.byte_offset;
      last = cursor;
    }
  }
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = string.string_safe,
      .string = first == string.string.len ? vstr_from_cstr("")
          : vstr_from_buf(string.string.data + first, last - first)};
  if (!string.string_safe && value->string.len == string.string.len) value->identity = string.identity;
  return JINJA_CMETA_OK;
}

/* Arguments are fully evaluated before retaining the mapping assembly scratch. */
static JINJA_CMETA_STATUS jinja_build_mapping(JINJA_CMETA_PROVIDER *provider,
    const vstr *keywords, size_t count,
    JINJA_CMETA_EXPRESSION_KIND kind, size_t positional,
    JINJA_CMETA_VALUE *value, JINJA_CMETA_VALUE *arguments) {
  JINJA_CMETA_VALUE dict = {.kind = JINJA_CMETA_VALUE_DICT};
  JINJA_CMETA_STATUS status;
  if (kind == JINJA_CMETA_EXPRESSION_NAMESPACE &&
      provider->shared.namespace_count >= provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  if (positional != 0u) {
    if (arguments[0].kind == JINJA_CMETA_VALUE_UNDEFINED) return JINJA_CMETA_ERR_RENDER;
    if (arguments[0].kind == JINJA_CMETA_VALUE_DICT) {
      for (size_t i = 0u; i < arguments[0].collection_item_count; ++i) {
        JINJA_CMETA_VALUE key, item;
        status = jinja_dict_entry_value(provider, &arguments[0], i, 0u, &key, &item);
        if (status == JINJA_CMETA_OK) status = jinja_dict_snapshot_write(provider, &dict, &key, &item);
        if (status != JINJA_CMETA_OK) return status;
      }
    } else {
      JINJA_CMETA_VALUE pairs;
      status = jinja_materialize_list(provider, &arguments[0], &pairs);
      if (status != JINJA_CMETA_OK) return status;
      for (size_t i = 0u; i < pairs.collection_item_count; ++i) {
        JINJA_CMETA_VALUE pair;
        JINJA_CMETA_VALUE source = pairs.collection_values[i];
        status = jinja_materialize_list(provider, &source, &pair);
        if (status != JINJA_CMETA_OK) return status;
        if (pair.collection_item_count != 2u) return JINJA_CMETA_ERR_RENDER;
        status = jinja_dict_snapshot_write(provider, &dict, &pair.collection_values[0], &pair.collection_values[1]);
        if (status != JINJA_CMETA_OK) return status;
      }
    }
  }
  for (size_t i = positional; i < count; ++i) {
    JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_STRING,
        .string = keywords[i]};
    status = jinja_dict_snapshot_write(provider, &dict, &key, &arguments[i]);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (kind == JINJA_CMETA_EXPRESSION_DICT_CALL) {
    *value = dict;
    return JINJA_CMETA_OK;
  }
  if (provider->shared.collection_value_count >= provider->shared.values.limit)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, 1u);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_VALUE *handle = jinja_cmeta_values_at(&provider->shared.values, provider->shared.collection_value_count++);
  *handle = dict;
  ++provider->shared.namespace_count;
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NAMESPACE, .namespace_dict = handle};
  return JINJA_CMETA_OK;
}

/* Non-expanded calls reuse their reserved snapshots. Expanded calls append to
 * the same LIFO workspace, so nested evaluation cannot overwrite outer values.
 * Duplicate checks are O(arguments^2), bounded by the shared 64-slot budget. */
static JINJA_CMETA_STATUS jinja_call_input_append(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_CALL_INPUT *input, const JINJA_CMETA_VALUE *value, const vstr *keyword) {
  if (keyword != NULL) {
    for (size_t i = input->positional; i < input->count; ++i)
      if (input->keywords[i].len == keyword->len &&
          (keyword->len == 0u || memcmp(input->keywords[i].data, keyword->data, keyword->len) == 0)) {
        if (!input->merge_expanded_keywords) return JINJA_CMETA_ERR_RENDER;
        input->values[i] = *value;
        return JINJA_CMETA_OK;
      }
  }
  if (input->count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      (input->expanded && provider->shared.call_argument_count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS))
    return JINJA_CMETA_ERR_CAPACITY;
  input->values[input->count] = *value;
  input->keywords[input->count] = keyword == NULL ? (vstr){0} : *keyword;
  ++input->count;
  if (keyword == NULL) ++input->positional;
  if (input->expanded) ++provider->shared.call_argument_count;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_expand_call_argument(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_CALL_INPUT *input,
    const JINJA_CMETA_COLLECTION_ITEM *item, JINJA_CMETA_VALUE *argument) {
  if (item->expansion == JINJA_CMETA_EXPAND_NONE)
    return jinja_call_input_append(provider, input, argument, item->keyword.len == 0u ? NULL : &item->keyword);
  if (item->expansion == JINJA_CMETA_EXPAND_POSITIONAL) {
    JINJA_CMETA_VALUE sequence;
    JINJA_CMETA_STATUS status = jinja_materialize_list(provider, argument, &sequence);
    if (status != JINJA_CMETA_OK) return status;
    for (size_t i = 0u; i < sequence.collection_item_count; ++i) {
      status = jinja_call_input_append(provider, input, &sequence.collection_values[i], NULL);
      if (status != JINJA_CMETA_OK) return status;
    }
    return JINJA_CMETA_OK;
  }
  if (item->expansion != JINJA_CMETA_EXPAND_KEYWORD || argument->kind != JINJA_CMETA_VALUE_DICT)
    return JINJA_CMETA_ERR_RENDER;
  for (size_t i = 0u; i < argument->collection_item_count; ++i) {
    JINJA_CMETA_VALUE key, value;
    int first = 0, found = 0;
    JINJA_CMETA_STATUS status = jinja_dict_entry_is_first(provider, argument, i, 0u, &first);
    if (status != JINJA_CMETA_OK) return status;
    if (!first) continue;
    status = jinja_dict_entry_value(provider, argument, i, 0u, &key, &value);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_dict_lookup(provider, argument, &key, 0u, &value, &found);
    if (status != JINJA_CMETA_OK) return status;
    if (!found) return JINJA_CMETA_ERR_RENDER;
    if (!jinja_value_is_string(&key)) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_SCALAR scalar = {0};
    status = jinja_scalar_from_value(provider, &key, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    vstr keyword = vstr_from_buf((const char *)scalar.data, scalar.size);
    if (vstr_utf8_invalid_offset(keyword) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
    status = jinja_call_input_append(provider, input, &value, &keyword);
    if (status != JINJA_CMETA_OK) return status;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_collect_call_input(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t index, size_t depth, JINJA_CMETA_VALUE *arguments, JINJA_CMETA_CALL_INPUT *input) {
  input->merge_expanded_keywords = expression->merge_expanded_keywords;
  JINJA_CMETA_CLOSURE *caller = provider->caller_expression == index ? provider->caller : NULL;
  input->expanded = caller != NULL;
  for (size_t i = 0u; i < expression->collection_item_count; ++i)
    input->expanded |= provider->instance->templ->collection_items[expression->first_collection_item + i].expansion != JINJA_CMETA_EXPAND_NONE;
  input->values = input->expanded ? provider->shared.call_arguments + provider->shared.call_argument_count : arguments;
  /* Python groups positional and starred operands ahead of keywords even when the source
   * writes a keyword before *. Expansion failures precede keyword evaluation. */
  for (int keywords = 0; keywords <= 1; ++keywords) {
    for (size_t i = 0u; i < expression->collection_item_count; ++i) {
      const JINJA_CMETA_COLLECTION_ITEM *item = &provider->instance->templ->collection_items[expression->first_collection_item + i];
      int is_keyword = item->expansion == JINJA_CMETA_EXPAND_KEYWORD || item->keyword.len != 0u;
      if (is_keyword != keywords) continue;
      if (caller != NULL && item->expansion == JINJA_CMETA_EXPAND_KEYWORD) {
        const vstr keyword = vstr_from_cstr("caller");
        JINJA_CMETA_VALUE injected = {.kind = JINJA_CMETA_VALUE_CALLABLE,
            .callable_kind = JINJA_CMETA_EXPRESSION_MACRO, .closure = caller};
        JINJA_CMETA_STATUS status = jinja_call_input_append(provider, input, &injected, &keyword);
        if (status != JINJA_CMETA_OK) return status;
        caller = NULL;
      }
      if (item->value_node >= index) return JINJA_CMETA_ERR_RENDER;
      JINJA_CMETA_STATUS status = jinja_expression_value(provider, context, item->value_node, depth + 1u, &arguments[i]);
      if (status != JINJA_CMETA_OK) return status;
      jinja_normalize_call_argument(&arguments[i]);
      status = jinja_expand_call_argument(provider, input, item, &arguments[i]);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  if (caller != NULL) {
    const vstr keyword = vstr_from_cstr("caller");
    JINJA_CMETA_VALUE injected = {.kind = JINJA_CMETA_VALUE_CALLABLE,
        .callable_kind = JINJA_CMETA_EXPRESSION_MACRO, .closure = caller};
    return jinja_call_input_append(provider, input, &injected, &keyword);
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_build_helper(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_EXPRESSION_KIND kind, const JINJA_CMETA_CALL_INPUT *input, JINJA_CMETA_VALUE *result) {
  static const char default_separator[] = ", ";
  const int cycle = kind == JINJA_CMETA_EXPRESSION_CYCLER;
  if (cycle ? input->count == 0u || input->positional != input->count
            : input->count > 1u || (input->count != input->positional &&
                !vstr_eq(input->keywords[0], vstr_from_cstr("sep"))))
    return JINJA_CMETA_ERR_RENDER;
  if (provider->shared.helper_count == provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_VALUE value = {.kind = JINJA_CMETA_VALUE_STRING,
      .identity = {.source = default_separator},
      .string = {default_separator, sizeof(default_separator) - 1u}};
  if (cycle) {
    const size_t offset = provider->shared.collection_value_count;
    if (input->count > provider->shared.values.limit - offset) return JINJA_CMETA_ERR_CAPACITY;
    JINJA_CMETA_STATUS status = jinja_ensure_collection_storage(provider, input->count);
    if (status != JINJA_CMETA_OK) return status;
    value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
        .collection_values = jinja_cmeta_values_at(&provider->shared.values, offset), .collection_item_count = input->count};
    status = jinja_value_identify(provider, &value);
    if (status != JINJA_CMETA_OK) return status;
    memcpy(jinja_cmeta_values_at(&provider->shared.values, offset), input->values, input->count * sizeof(*input->values));
  } else if (input->count != 0u) value = input->values[0];
  JINJA_CMETA_HELPER *helper = (JINJA_CMETA_HELPER *)jinja_provider_zero(provider, 1u, sizeof(*helper));
  if (helper == NULL) return provider->shared.status;
  helper->value = value;
  helper->next = provider->shared.helpers;
  provider->shared.helpers = helper;
  ++provider->shared.helper_count;
  if (cycle) provider->shared.collection_value_count += input->count;
  *result = (JINJA_CMETA_VALUE){.kind = cycle ? JINJA_CMETA_VALUE_CYCLER : JINJA_CMETA_VALUE_JOINER,
      .helper = helper};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_call_helper(JINJA_CMETA_HELPER *helper,
    JINJA_CMETA_EXPRESSION_KIND method, const JINJA_CMETA_CALL_INPUT *input, JINJA_CMETA_VALUE *result) {
  if (input->count != 0u) return JINJA_CMETA_ERR_RENDER;
  if (method == JINJA_CMETA_EXPRESSION_CYCLER_NEXT) {
    *result = helper->value.collection_values[helper->position];
    helper->position = (helper->position + 1u) % helper->value.collection_item_count;
  } else if (method == JINJA_CMETA_EXPRESSION_CYCLER_RESET) {
    helper->position = 0u;
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NONE};
  } else {
    *result = helper->position == 0u
        ? (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string = {"", 0u}} : helper->value;
    helper->position = 1u;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_host_call_value(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *source, JINJA_CMETA_CALL_VALUE *destination) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, source, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  *destination = (JINJA_CMETA_CALL_VALUE){0};
  switch (scalar.kind) {
  case JINJA_CMETA_SCALAR_UNDEFINED:
    destination->kind = JINJA_CMETA_CALL_VALUE_UNDEFINED;
    break;
  case JINJA_CMETA_SCALAR_NONE:
    destination->kind = JINJA_CMETA_CALL_VALUE_NONE;
    break;
  case JINJA_CMETA_SCALAR_BOOL:
    destination->kind = JINJA_CMETA_CALL_VALUE_BOOL;
    destination->boolean = scalar.boolean;
    break;
  case JINJA_CMETA_SCALAR_SINT:
    destination->kind = JINJA_CMETA_CALL_VALUE_INTEGER;
    destination->integer = scalar.sint;
    break;
  case JINJA_CMETA_SCALAR_UINT:
    if (scalar.uint > (uint64_t)INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
    destination->kind = JINJA_CMETA_CALL_VALUE_INTEGER;
    destination->integer = (int64_t)scalar.uint;
    break;
  case JINJA_CMETA_SCALAR_FLOAT:
    destination->kind = JINJA_CMETA_CALL_VALUE_FLOAT;
    destination->floating = scalar.floating;
    break;
  case JINJA_CMETA_SCALAR_STRING:
    destination->kind = JINJA_CMETA_CALL_VALUE_STRING;
    destination->string = vstr_from_buf((const char *)scalar.data, scalar.size);
    destination->string_safe = jinja_value_is_safe(source);
    break;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_host_call_argument(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CALLABLE *callable, const JINJA_CMETA_VALUE *source,
    JINJA_CMETA_CALL_ARGUMENT *destination) {
  destination->descriptor = NULL;
  destination->object = NULL;
  if (source->kind == JINJA_CMETA_VALUE_NODE && source->node.desc != NULL) {
    destination->descriptor = source->node.desc;
    destination->object = source->node.object;
    if (callable->generic) {
      destination->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_UNDEFINED};
      return JINJA_CMETA_OK;
    }
  }
  return jinja_host_call_value(provider, source, &destination->value);
}

static JINJA_CMETA_STATUS jinja_invoke_host_function(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CALLABLE *callable, const JINJA_CMETA_CALL_INPUT *input,
    const JINJA_CMETA_VALUE *leading, JINJA_CMETA_VALUE *value) {
  const size_t leading_count = leading != NULL ? 1u : 0u;
  const size_t total_count = input->count > SIZE_MAX - leading_count
      ? SIZE_MAX : input->count + leading_count;
  const size_t total_positional = input->positional > SIZE_MAX - leading_count
      ? SIZE_MAX : input->positional + leading_count;
  const size_t keyword_count = input->count - input->positional;
  JINJA_CMETA_CALL_ARGUMENT *arguments = NULL;
  JINJA_CMETA_CALL_RESULT result = {0};
  JINJA_CMETA_STATUS status;
  if (callable == NULL || callable->invoke == NULL || total_positional < callable->min_positional ||
      total_positional > callable->max_positional || keyword_count > callable->max_keywords ||
      total_count == SIZE_MAX)
    return JINJA_CMETA_ERR_RENDER;
  if (total_count != 0u) {
    arguments = (JINJA_CMETA_CALL_ARGUMENT *)jinja_provider_allocate(provider,
        total_count, sizeof(*arguments));
    if (arguments == NULL) return provider->shared.status;
  }
  if (leading != NULL) {
    arguments[0].name = vstr_from_cstr("");
    status = jinja_host_call_argument(provider, callable, leading, &arguments[0]);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_memory_drop(arguments);
      return status;
    }
  }
  for (size_t i = 0u; i < input->count; ++i) {
    const size_t argument = i + leading_count;
    arguments[argument].name = i < input->positional ? vstr_from_cstr("") : input->keywords[i];
    status = jinja_host_call_argument(provider, callable, &input->values[i], &arguments[argument]);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_memory_drop(arguments);
      return status;
    }
  }
  const JINJA_CMETA_CALL_CONTEXT context = {
      callable->name, arguments, total_count, total_positional};
  status = callable->invoke(callable->userdata, &context, &result);
  if (status != JINJA_CMETA_OK) {
    jinja_cmeta_memory_drop(arguments);
    jinja_cmeta_error_set(provider->shared.error, status, *provider->shared.error_offset,
        "registered function callback failed");
    jinja_cmeta_error_name(provider->shared.error, callable->name);
    return status;
  }
  switch (result.value.kind) {
  case JINJA_CMETA_CALL_VALUE_UNDEFINED:
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
    break;
  case JINJA_CMETA_CALL_VALUE_NONE:
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NONE};
    break;
  case JINJA_CMETA_CALL_VALUE_BOOL:
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_BOOL, .boolean = result.value.boolean};
    break;
  case JINJA_CMETA_CALL_VALUE_INTEGER:
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = result.value.integer};
    break;
  case JINJA_CMETA_CALL_VALUE_FLOAT:
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_FLOAT, .floating = result.value.floating};
    break;
  case JINJA_CMETA_CALL_VALUE_STRING: {
    const size_t offset = provider->shared.slice_byte_count;
    if (!vstr_is_valid(result.value.string) || vstr_utf8_invalid_offset(result.value.string) != VSTR_NPOS) {
      jinja_cmeta_memory_drop(arguments);
      return JINJA_CMETA_ERR_METADATA;
    }
    if (jinja_concat_write(result.value.string.data, result.value.string.len, provider) != 0) {
      jinja_cmeta_memory_drop(arguments);
      return provider->shared.status;
    }
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
        .string_safe = result.value.string_safe,
        .string = result.value.string.len == 0u ? vstr_from_cstr("")
            : vstr_from_buf(provider->shared.slice_bytes + offset, result.value.string.len)};
    break;
  }
  default:
    jinja_cmeta_memory_drop(arguments);
    return JINJA_CMETA_ERR_METADATA;
  }
  jinja_cmeta_memory_drop(arguments);
  return JINJA_CMETA_OK;
}


static JINJA_CMETA_STATUS jinja_invoke_callable(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t index, size_t depth, JINJA_CMETA_VALUE *value, JINJA_CMETA_VALUE *arguments) {
  JINJA_CMETA_VALUE *target = arguments++;
  JINJA_CMETA_CALL_INPUT input = {0};
  JINJA_CMETA_STATUS status;
  if (expression->left_node >= index || expression->collection_item_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      expression->first_collection_item > provider->instance->templ->collection_item_count ||
      expression->collection_item_count > provider->instance->templ->collection_item_count - expression->first_collection_item)
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, target);
  if (status != JINJA_CMETA_OK) return status;
  jinja_normalize_call_argument(target);
  status = jinja_collect_call_input(provider, context, expression, index, depth, arguments, &input);
  if (status != JINJA_CMETA_OK) return status;
  if (target->kind == JINJA_CMETA_VALUE_LOOP)
    return jinja_invoke_loop(provider, target->loop, &input, value);
  if (target->kind == JINJA_CMETA_VALUE_JOINER)
    return jinja_call_helper(target->helper, JINJA_CMETA_EXPRESSION_JOINER_CALL, &input, value);
  if (target->kind != JINJA_CMETA_VALUE_CALLABLE) return JINJA_CMETA_ERR_RENDER;
  if (target->host_callable != NULL)
    return jinja_invoke_host_function(provider, target->host_callable, &input, NULL, value);
  switch (target->callable_kind) {
  case JINJA_CMETA_EXPRESSION_CYCLER:
  case JINJA_CMETA_EXPRESSION_JOINER:
    return jinja_build_helper(provider, target->callable_kind, &input, value);
  case JINJA_CMETA_EXPRESSION_CYCLER_NEXT:
  case JINJA_CMETA_EXPRESSION_CYCLER_RESET:
  case JINJA_CMETA_EXPRESSION_JOINER_CALL:
    return jinja_call_helper(target->helper, target->callable_kind, &input, value);
  case JINJA_CMETA_EXPRESSION_LOOP_CYCLE:
  case JINJA_CMETA_EXPRESSION_LOOP_CHANGED:
    return jinja_call_loop_method(provider, target->loop, target->callable_kind, &input, depth, value);
  case JINJA_CMETA_EXPRESSION_MACRO:
  case JINJA_CMETA_EXPRESSION_BLOCK:
    return jinja_invoke_function(provider, target->closure, &input, value);
  case JINJA_CMETA_EXPRESSION_DICT_CALL:
  case JINJA_CMETA_EXPRESSION_NAMESPACE:
    if (input.positional > 1u) return JINJA_CMETA_ERR_RENDER;
    return jinja_build_mapping(provider, input.keywords, input.count,
        target->callable_kind, input.positional, value, input.values);
  case JINJA_CMETA_EXPRESSION_RANGE:
    if (input.positional != input.count) return JINJA_CMETA_ERR_RENDER;
    return jinja_build_range(provider, input.values, input.positional, value);
  case JINJA_CMETA_EXPRESSION_RANGE_COUNT:
  case JINJA_CMETA_EXPRESSION_RANGE_INDEX:
    if (input.positional != 1u || input.positional != input.count) return JINJA_CMETA_ERR_RENDER;
    return jinja_call_range_method(provider, target, &input.values[0], value);
  default: return JINJA_CMETA_ERR_RENDER;
  }
}

static JINJA_CMETA_STATUS jinja_attribute_key(vstr segment, JINJA_CMETA_VALUE *key) {
  enum { DECIMAL_RADIX = 10 };
  size_t cursor = 0u;
  int64_t integer = 0;
  int overflow = 0;
  *key = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string = segment};
  if (segment.len == 0u) return JINJA_CMETA_OK;
  while (cursor < segment.len) {
    salts_unicode_scalar scalar;
    uint32_t digit;
    if (salts_unicode_utf8_next(segment, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    if (salts_unicode_decimal_value(scalar.value, &digit) != SALTS_UNICODE_OK)
      return JINJA_CMETA_OK;
    if (integer > (INT64_MAX - (int64_t)digit) / DECIMAL_RADIX) overflow = 1;
    else if (!overflow) integer = integer * DECIMAL_RADIX + (int64_t)digit;
  }
  if (overflow) return JINJA_CMETA_ERR_CAPACITY;
  key->kind = JINJA_CMETA_VALUE_INTEGER;
  key->integer = integer;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_attribute_parts(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *attribute, JINJA_CMETA_VALUE *parts) {
  JINJA_CMETA_SCALAR scalar = {0};
  JINJA_CMETA_STATUS status;
  size_t count = 1u;
  const size_t offset = provider->shared.collection_value_count;
  memset(parts, 0, sizeof(*parts));
  if (attribute == NULL || attribute->kind == JINJA_CMETA_VALUE_NONE) return JINJA_CMETA_OK;
  const int string = jinja_value_is_string(attribute);
  if (string) {
    status = jinja_scalar_from_value(provider, attribute, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    if (vstr_utf8_invalid_offset(vstr_from_buf((const char *)scalar.data, scalar.size)) != SIZE_MAX)
      return JINJA_CMETA_ERR_METADATA;
    for (size_t i = 0u; i < scalar.size; ++i) {
      if (((const char *)scalar.data)[i] == '.') ++count;
      if (count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS) return JINJA_CMETA_ERR_CAPACITY;
    }
  }
  if (offset > provider->shared.values.limit ||
      count > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, count);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.collection_value_count += count;
  if (!string) (*jinja_cmeta_values_at(&provider->shared.values, offset)) = *attribute;
  else {
    const char *text = scalar.size == 0u ? "" : (const char *)scalar.data;
    size_t begin = 0u;
    for (size_t i = 0u; i < count; ++i) {
      size_t end = begin;
      while (end < scalar.size && text[end] != '.') ++end;
      status = jinja_attribute_key(vstr_from_buf(text + begin, end - begin),
                                  jinja_cmeta_values_at(&provider->shared.values, offset + i));
      if (status != JINJA_CMETA_OK) return status;
      begin = end + 1u;
    }
  }
  parts->collection_item_count = count;
  parts->collection_values = jinja_cmeta_values_at(&provider->shared.values, offset);
  return JINJA_CMETA_OK;
}

typedef enum JINJA_CMETA_SUM_PHASE {
  JINJA_CMETA_SUM_INITIAL,
  JINJA_CMETA_SUM_INTEGER,
  JINJA_CMETA_SUM_FLOAT,
  JINJA_CMETA_SUM_GENERIC
} JINJA_CMETA_SUM_PHASE;

static JINJA_CMETA_STATUS jinja_sum_add(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *total, const JINJA_CMETA_VALUE *item,
    JINJA_CMETA_SUM_PHASE *phase, double *correction) {
  if (jinja_value_is_collection(total->kind)) {
    JINJA_CMETA_VALUE next;
    JINJA_CMETA_STATUS status = jinja_add_collections(provider, total, item, &next);
    if (status == JINJA_CMETA_OK) *total = next;
    return status;
  }
  JINJA_CMETA_NUMBER left = {0}, right = {0};
  JINJA_CMETA_STATUS status = jinja_number_from_value(provider, total, &left);
  if (status == JINJA_CMETA_OK) status = jinja_number_from_value(provider, item, &right);
  if (status != JINJA_CMETA_OK) return status;
  if (*phase == JINJA_CMETA_SUM_INITIAL) {
    const int boolean = total->kind == JINJA_CMETA_VALUE_BOOL ||
        (total->kind == JINJA_CMETA_VALUE_NODE && total->node.desc != NULL &&
         total->node.desc->kind == CMETA_DATA_BOOL);
    *phase = boolean ? JINJA_CMETA_SUM_GENERIC : left.kind == JINJA_CMETA_NUMBER_FLOAT
        ? JINJA_CMETA_SUM_FLOAT : left.integer >= LONG_MIN && left.integer <= LONG_MAX
        ? JINJA_CMETA_SUM_INTEGER : JINJA_CMETA_SUM_GENERIC;
  }
  /* Python 3.12 leaves the native fast phase when conversion to C long fails.
   * Once left, later floats must not restart compensation. */
  if (*phase != JINJA_CMETA_SUM_GENERIC && right.kind == JINJA_CMETA_NUMBER_INTEGER &&
      (right.integer < LONG_MIN || right.integer > LONG_MAX)) {
    if (*phase == JINJA_CMETA_SUM_FLOAT && *correction != 0.0 && isfinite(*correction))
      left.floating += *correction;
    *correction = 0.0;
    *phase = JINJA_CMETA_SUM_GENERIC;
  }
  if (left.kind == JINJA_CMETA_NUMBER_INTEGER && right.kind == JINJA_CMETA_NUMBER_INTEGER) {
    status = jinja_checked_add(left.integer, right.integer, &total->integer);
    if (status == JINJA_CMETA_OK) {
      total->kind = JINJA_CMETA_VALUE_INTEGER;
      total->identity = (JINJA_CMETA_IDENTITY){0};
    }
    return status;
  }
  const double a = left.kind == JINJA_CMETA_NUMBER_FLOAT ? left.floating : (double)left.integer;
  const double b = right.kind == JINJA_CMETA_NUMBER_FLOAT ? right.floating : (double)right.integer;
  const double sum = a + b;
  /* Neumaier compensation applies to the float phase, as in the Python 3.12 oracle. */
  if (*phase == JINJA_CMETA_SUM_FLOAT && right.kind == JINJA_CMETA_NUMBER_FLOAT)
    *correction += fabs(a) >= fabs(b) ? (a - sum) + b : (b - sum) + a;
  if (*phase == JINJA_CMETA_SUM_INTEGER) *phase = JINJA_CMETA_SUM_FLOAT;
  total->kind = JINJA_CMETA_VALUE_FLOAT;
  total->floating = sum;
  total->identity = (JINJA_CMETA_IDENTITY){0};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_project_attribute(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *parts, const JINJA_CMETA_VALUE *default_value, JINJA_CMETA_VALUE *item) {
  for (size_t p = 0u; p < parts->collection_item_count; ++p) {
    JINJA_CMETA_VALUE projected;
    JINJA_CMETA_STATUS status = jinja_lookup_item(provider, item, &parts->collection_values[p], 0u, &projected);
    if (status != JINJA_CMETA_OK) return status;
    jinja_normalize_call_argument(&projected);
    *item = default_value != NULL && projected.kind == JINJA_CMETA_VALUE_UNDEFINED
        ? *default_value : projected;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_join_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand,
    const JINJA_CMETA_VALUE *delimiter, const JINJA_CMETA_VALUE *attribute, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE parts, list, separator, item;
  JINJA_CMETA_STATUS status = jinja_attribute_parts(provider, attribute, &parts);
  size_t offset, count, start;
  int safe;
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_materialize_list(provider, operand, &list);
  if (status != JINJA_CMETA_OK) return status;
  offset = provider->shared.collection_value_count;
  count = list.collection_item_count;
  if (offset > provider->shared.values.limit || count > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, count);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.collection_value_count += count;
  item = delimiter != NULL ? *delimiter
      : (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string = vstr_from_cstr("")};
  status = jinja_string_value(provider, &item, &separator);
  if (status != JINJA_CMETA_OK) return status;
  if (vstr_utf8_invalid_offset(separator.string) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  safe = separator.string_safe;
  /* Conversion snapshots must precede final output: both use the same fixed byte storage. */
  for (size_t i = 0u; i < count; ++i) {
    JINJA_CMETA_VALUE *text = jinja_cmeta_values_at(&provider->shared.values, offset + i);
    item = list.collection_values[i];
    status = jinja_project_attribute(provider, &parts, NULL, &item);
    if (status == JINJA_CMETA_OK) status = jinja_string_value(provider, &item, text);
    if (status != JINJA_CMETA_OK) return status;
    if (vstr_utf8_invalid_offset(text->string) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
    safe = safe || text->string_safe;
  }
  safe = provider->autoescape && safe;
  if (safe) {
    item = separator;
    status = jinja_safety_value(provider, &item, JINJA_CMETA_EXPRESSION_ESCAPE, &separator);
    if (status != JINJA_CMETA_OK) return status;
    for (size_t i = 0u; i < count; ++i) {
      item = (*jinja_cmeta_values_at(&provider->shared.values, offset + i));
      status = jinja_safety_value(provider, &item, JINJA_CMETA_EXPRESSION_ESCAPE,
                                  jinja_cmeta_values_at(&provider->shared.values, offset + i));
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  start = provider->shared.slice_byte_count;
  for (size_t i = 0u; i < count; ++i) {
    const vstr text = (*jinja_cmeta_values_at(&provider->shared.values, offset + i)).string;
    if (i != 0u && jinja_concat_write(separator.string.data, separator.string.len, provider) != 0)
      return provider->shared.status;
    if (jinja_concat_write(text.data, text.len, provider) != 0) return provider->shared.status;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string_safe = safe,
      .string = provider->shared.slice_byte_count == start ? vstr_from_cstr("")
          : vstr_from_buf(provider->shared.slice_bytes + start, provider->shared.slice_byte_count - start)};
  if (!safe && count == 1u && !(*jinja_cmeta_values_at(&provider->shared.values, offset)).string_safe)
    result->identity = (*jinja_cmeta_values_at(&provider->shared.values, offset)).identity;
  return JINJA_CMETA_OK;
}

/* CPython's numeric fast accumulators box a fresh result even without items;
 * generic starts (including bool and integers outside C long) are returned as-is. */
static JINJA_CMETA_STATUS jinja_sum_empty(JINJA_CMETA_PROVIDER *provider, JINJA_CMETA_VALUE *value) {
  if (value->kind != JINJA_CMETA_VALUE_INTEGER && value->kind != JINJA_CMETA_VALUE_FLOAT &&
      !(value->kind == JINJA_CMETA_VALUE_NODE && value->node.desc != NULL &&
        (value->node.desc->kind == CMETA_DATA_SINT || value->node.desc->kind == CMETA_DATA_UINT ||
         value->node.desc->kind == CMETA_DATA_FLOAT))) return JINJA_CMETA_OK;
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, value, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  if (scalar.kind == JINJA_CMETA_SCALAR_FLOAT)
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_FLOAT, .floating = scalar.floating};
  else if (scalar.kind == JINJA_CMETA_SCALAR_SINT && scalar.sint >= LONG_MIN && scalar.sint <= LONG_MAX)
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = scalar.sint};
  else if (scalar.kind == JINJA_CMETA_SCALAR_UINT && scalar.uint <= (uint64_t)LONG_MAX)
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)scalar.uint};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_sum_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand,
    const JINJA_CMETA_VALUE *attribute, const JINJA_CMETA_VALUE *start, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE parts, list, total;
  double correction = 0.0;
  JINJA_CMETA_SUM_PHASE phase = JINJA_CMETA_SUM_INITIAL;
  JINJA_CMETA_STATUS status = jinja_attribute_parts(provider, attribute, &parts);
  if (status != JINJA_CMETA_OK) return status;
  total = start != NULL ? *start : (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = 0};
  if (jinja_value_is_string(&total)) return JINJA_CMETA_ERR_RENDER;
  status = jinja_materialize_list(provider, operand, &list);
  if (status != JINJA_CMETA_OK) return status;
  for (size_t i = 0u; i < list.collection_item_count; ++i) {
    JINJA_CMETA_VALUE item = list.collection_values[i];
    status = jinja_project_attribute(provider, &parts, NULL, &item);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_sum_add(provider, &total, &item, &phase, &correction);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (total.kind == JINJA_CMETA_VALUE_FLOAT && correction != 0.0 && isfinite(correction))
    total.floating += correction;
  if (list.collection_item_count == 0u) {
    status = jinja_sum_empty(provider, &total);
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = total;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_minmax_key(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *parts, int case_sensitive, JINJA_CMETA_VALUE *item) {
  JINJA_CMETA_STATUS status = jinja_project_attribute(provider, parts, NULL, item);
  if (status != JINJA_CMETA_OK || case_sensitive || !jinja_value_is_string(item)) return status;
  return jinja_case_value(provider, item, SALTS_UNICODE_CASE_LOWER, item);
}

static JINJA_CMETA_STATUS jinja_minmax_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *case_value,
    const JINJA_CMETA_VALUE *attribute, int maximum, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE parts, list, candidate, candidate_key;
  int case_sensitive = 0;
  JINJA_CMETA_STATUS status = jinja_attribute_parts(provider, attribute, &parts);
  if (status != JINJA_CMETA_OK) return status;
  if (case_value != NULL) {
    status = jinja_value_truthy(provider, case_value, &case_sensitive);
    if (status != JINJA_CMETA_OK) return status;
  }
  status = jinja_materialize_list(provider, operand, &list);
  if (status != JINJA_CMETA_OK) return status;
  if (list.collection_item_count == 0u) {
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
    return JINJA_CMETA_OK;
  }
  candidate = list.collection_values[0];
  candidate_key = candidate;
  status = jinja_minmax_key(provider, &parts, case_sensitive, &candidate_key);
  for (size_t i = 1u; status == JINJA_CMETA_OK && i < list.collection_item_count; ++i) {
    JINJA_CMETA_VALUE item = list.collection_values[i];
    JINJA_CMETA_VALUE item_key = item;
    int selected = 0;
    status = jinja_minmax_key(provider, &parts, case_sensitive, &item_key);
    if (status == JINJA_CMETA_OK)
      status = jinja_compare_values(provider, &item_key, &candidate_key,
          maximum ? JINJA_CMETA_COMPARISON_GREATER : JINJA_CMETA_COMPARISON_LESS, 0u, &selected);
    if (status == JINJA_CMETA_OK && selected) {
      candidate = item;
      candidate_key = item_key;
    }
  }
  if (status == JINJA_CMETA_OK) *result = candidate;
  return status;
}

static JINJA_CMETA_STATUS jinja_sort_attribute_parts(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *attribute, JINJA_CMETA_VALUE **parts, size_t *count) {
  JINJA_CMETA_SCALAR scalar = {0};
  JINJA_CMETA_STATUS status;
  *parts = NULL;
  *count = 0u;
  if (attribute == NULL || attribute->kind == JINJA_CMETA_VALUE_NONE) return JINJA_CMETA_OK;
  if (!jinja_value_is_string(attribute)) {
    *parts = (JINJA_CMETA_VALUE *)jinja_provider_zero(provider, 1u, sizeof(**parts));
    if (*parts == NULL) return provider->shared.status;
    status = jinja_attribute_parts(provider, attribute, *parts);
    if (status == JINJA_CMETA_OK) *count = 1u;
    return status;
  }
  status = jinja_scalar_from_value(provider, attribute, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  const vstr text = vstr_from_buf((const char *)scalar.data, scalar.size);
  if (vstr_utf8_invalid_offset(text) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  size_t part_count = 1u;
  for (size_t i = 0u; i < text.len; ++i) {
    if (text.data[i] == ',') {
      if (part_count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS) return JINJA_CMETA_ERR_CAPACITY;
      ++part_count;
    }
  }
  *parts = (JINJA_CMETA_VALUE *)jinja_provider_zero(provider, part_count, sizeof(**parts));
  if (*parts == NULL) return provider->shared.status;
  size_t begin = 0u;
  for (size_t i = 0u; i < part_count; ++i) {
    size_t end = begin;
    while (end < text.len && text.data[end] != ',') ++end;
    const JINJA_CMETA_VALUE segment = {.kind = JINJA_CMETA_VALUE_STRING,
        .string = vstr_from_buf(text.data + begin, end - begin)};
    status = jinja_attribute_parts(provider, &segment, &(*parts)[i]);
    if (status != JINJA_CMETA_OK) return status;
    begin = end + 1u;
  }
  *count = part_count;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_sort_key(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *parts, size_t part_count, int case_sensitive,
    const JINJA_CMETA_VALUE *item, JINJA_CMETA_VALUE *key) {
  for (size_t i = 0u; i < (part_count == 0u ? 1u : part_count); ++i) {
    key[i] = *item;
    JINJA_CMETA_STATUS status = part_count == 0u ? JINJA_CMETA_OK
        : jinja_project_attribute(provider, &parts[i], NULL, &key[i]);
    if (status != JINJA_CMETA_OK) return status;
    if (!case_sensitive && jinja_value_is_string(&key[i])) {
      status = jinja_case_value(provider, &key[i], SALTS_UNICODE_CASE_LOWER, &key[i]);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_sort_before(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *left, const JINJA_CMETA_VALUE *right, size_t key_count,
    int reverse, int *before) {
  *before = 0;
  for (size_t i = 0u; i < key_count; ++i) {
    int ordered = 0;
    JINJA_CMETA_STATUS status = jinja_compare_values(provider, &left[i], &right[i],
        reverse ? JINJA_CMETA_COMPARISON_LESS : JINJA_CMETA_COMPARISON_GREATER, 0u, &ordered);
    if (status != JINJA_CMETA_OK) return status;
    if (ordered) {
      *before = 1;
      return JINJA_CMETA_OK;
    }
    status = jinja_compare_values(provider, &left[i], &right[i],
        reverse ? JINJA_CMETA_COMPARISON_GREATER : JINJA_CMETA_COMPARISON_LESS, 0u, &ordered);
    if (status != JINJA_CMETA_OK) return status;
    if (ordered) return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_OK;
}

/* Stable insertion sort keeps chained Jinja sorts deterministic. It is O(n^2)
 * comparisons and O(n * attributes) render-owned slots, both independently bounded. */
static JINJA_CMETA_STATUS jinja_sort_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *reverse_value,
    const JINJA_CMETA_VALUE *case_value, const JINJA_CMETA_VALUE *attribute,
    JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE *parts = NULL, list;
  size_t part_count, key_count, slots_per_item;
  int reverse = 0, case_sensitive = 0;
  JINJA_CMETA_STATUS status = jinja_sort_attribute_parts(provider, attribute, &parts, &part_count);
  if (status != JINJA_CMETA_OK) goto cleanup;
  if (reverse_value != NULL) {
    status = jinja_value_truthy(provider, reverse_value, &reverse);
    if (status != JINJA_CMETA_OK) goto cleanup;
  }
  if (case_value != NULL) {
    status = jinja_value_truthy(provider, case_value, &case_sensitive);
    if (status != JINJA_CMETA_OK) goto cleanup;
  }
  status = jinja_materialize_list(provider, operand, &list);
  if (status != JINJA_CMETA_OK) goto cleanup;
  key_count = part_count == 0u ? 1u : part_count;
  if (list.collection_item_count == 0u) {
    *result = list;
    goto cleanup;
  }
  slots_per_item = key_count + 1u;
  if (list.collection_item_count >
      (provider->shared.values.limit - provider->shared.collection_value_count) / slots_per_item) {
    status = JINJA_CMETA_ERR_CAPACITY;
    goto cleanup;
  }
  status = jinja_ensure_collection_storage(provider, list.collection_item_count * slots_per_item);
  if (status != JINJA_CMETA_OK) goto cleanup;
  JINJA_CMETA_VALUE *sorted = jinja_cmeta_values_at(&provider->shared.values,
      provider->shared.collection_value_count);
  JINJA_CMETA_VALUE *keys = sorted + list.collection_item_count;
  provider->shared.collection_value_count += list.collection_item_count * slots_per_item;
  for (size_t i = 0u; i < list.collection_item_count; ++i) {
    sorted[i] = list.collection_values[i];
    status = jinja_sort_key(provider, parts, part_count, case_sensitive, &sorted[i],
        keys + i * key_count);
    if (status != JINJA_CMETA_OK) goto cleanup;
  }
  for (size_t i = 1u; i < list.collection_item_count; ++i) {
    size_t cursor = i;
    while (cursor != 0u) {
      int move = 0;
      status = jinja_sort_before(provider, keys + (cursor - 1u) * key_count,
          keys + cursor * key_count, key_count, reverse, &move);
      if (status != JINJA_CMETA_OK || !move) break;
      JINJA_CMETA_VALUE item = sorted[cursor];
      sorted[cursor] = sorted[cursor - 1u];
      sorted[cursor - 1u] = item;
      for (size_t key = 0u; key < key_count; ++key) {
        item = keys[cursor * key_count + key];
        keys[cursor * key_count + key] = keys[(cursor - 1u) * key_count + key];
        keys[(cursor - 1u) * key_count + key] = item;
      }
      --cursor;
    }
    if (status != JINJA_CMETA_OK) goto cleanup;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
      .collection_item_count = list.collection_item_count, .collection_values = sorted};
cleanup:
  jinja_cmeta_memory_drop(parts);
  return status;
}

static JINJA_CMETA_STATUS jinja_dictsort_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *case_value,
    const JINJA_CMETA_VALUE *by_value, const JINJA_CMETA_VALUE *reverse_value,
    JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE keys_list, by_string;
  size_t key_position = 0u;
  int case_sensitive = 0, reverse = 0;
  JINJA_CMETA_STATUS status;
  jinja_normalize_call_argument(operand);
  if (operand->kind != JINJA_CMETA_VALUE_DICT) return JINJA_CMETA_ERR_RENDER;
  if (case_value != NULL) {
    status = jinja_value_truthy(provider, case_value, &case_sensitive);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (reverse_value != NULL) {
    status = jinja_value_truthy(provider, reverse_value, &reverse);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (by_value != NULL) {
    status = jinja_string_value(provider, (JINJA_CMETA_VALUE *)by_value, &by_string);
    if (status != JINJA_CMETA_OK || vstr_utf8_invalid_offset(by_string.string) != SIZE_MAX)
      return status != JINJA_CMETA_OK ? status : JINJA_CMETA_ERR_METADATA;
    if (vstr_eq(by_string.string, vstr_from_cstr("key"))) key_position = 0u;
    else if (vstr_eq(by_string.string, vstr_from_cstr("value"))) key_position = 1u;
    else return JINJA_CMETA_ERR_RENDER;
  }
  status = jinja_materialize_list(provider, operand, &keys_list);
  if (status != JINJA_CMETA_OK) return status;
  if (keys_list.collection_item_count == 0u) {
    *result = keys_list;
    return JINJA_CMETA_OK;
  }
  if (keys_list.collection_item_count >
      (provider->shared.values.limit - provider->shared.collection_value_count) / 4u)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, keys_list.collection_item_count * 4u);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_VALUE *items = jinja_cmeta_values_at(&provider->shared.values,
      provider->shared.collection_value_count);
  JINJA_CMETA_VALUE *pairs = items + keys_list.collection_item_count;
  JINJA_CMETA_VALUE *sort_keys = pairs + keys_list.collection_item_count * 2u;
  provider->shared.collection_value_count += keys_list.collection_item_count * 4u;
  for (size_t i = 0u; i < keys_list.collection_item_count; ++i) {
    pairs[i * 2u] = keys_list.collection_values[i];
    status = jinja_lookup_item(provider, operand, &pairs[i * 2u], 0u, &pairs[i * 2u + 1u]);
    if (status != JINJA_CMETA_OK) return status;
    items[i] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
        .collection_values = pairs + i * 2u, .collection_item_count = 2u};
    sort_keys[i] = pairs[i * 2u + key_position];
    if (!case_sensitive && jinja_value_is_string(&sort_keys[i])) {
      status = jinja_case_value(provider, &sort_keys[i], SALTS_UNICODE_CASE_LOWER, &sort_keys[i]);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  for (size_t i = 1u; i < keys_list.collection_item_count; ++i) {
    size_t cursor = i;
    while (cursor != 0u) {
      int move = 0;
      status = jinja_sort_before(provider, &sort_keys[cursor - 1u], &sort_keys[cursor], 1u, reverse, &move);
      if (status != JINJA_CMETA_OK || !move) break;
      JINJA_CMETA_VALUE item = items[cursor];
      items[cursor] = items[cursor - 1u];
      items[cursor - 1u] = item;
      item = sort_keys[cursor];
      sort_keys[cursor] = sort_keys[cursor - 1u];
      sort_keys[cursor - 1u] = item;
      --cursor;
    }
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
      .collection_values = items, .collection_item_count = keys_list.collection_item_count};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_unique_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *case_value,
    const JINJA_CMETA_VALUE *attribute, JINJA_CMETA_VALUE *result) {
  if (provider->shared.transform_count >= provider->shared.node_capacity)
    return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_TRANSFORM *transform = (JINJA_CMETA_TRANSFORM *)jinja_provider_zero(provider, 1u,
      sizeof(*transform));
  if (transform == NULL) return provider->shared.status;
  transform->source.input = *operand;
  jinja_normalize_call_argument(&transform->source.input);
  transform->source.max_items = provider->shared.node_capacity;
  transform->kind = JINJA_CMETA_EXPRESSION_UNIQUE;
  transform->prepared = 1;
  if (case_value != NULL) {
    JINJA_CMETA_STATUS status = jinja_value_truthy(provider, case_value, &transform->case_sensitive);
    if (status != JINJA_CMETA_OK) { jinja_cmeta_memory_drop(transform); return status; }
  }
  JINJA_CMETA_STATUS status = jinja_attribute_parts(provider, attribute, &transform->parts);
  if (status != JINJA_CMETA_OK) { jinja_cmeta_memory_drop(transform); return status; }
  transform->unique_capacity = provider->shared.node_capacity;
  if (transform->unique_capacity != 0u) {
    transform->unique_seen = (JINJA_CMETA_VALUE *)jinja_provider_zero(provider,
        transform->unique_capacity, sizeof(*transform->unique_seen));
    if (transform->unique_seen == NULL) { jinja_cmeta_memory_drop(transform); return provider->shared.status; }
  }
  transform->next = provider->shared.transforms;
  provider->shared.transforms = transform;
  ++provider->shared.transform_count;
  status = jinja_create_iterator(provider, operand, JINJA_CMETA_ITERATOR_TRANSFORM, result);
  if (status == JINJA_CMETA_OK) result->iterator->transform = transform;
  return status;
}

static JINJA_CMETA_STATUS jinja_groupby_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *attribute,
    const JINJA_CMETA_VALUE *default_value, const JINJA_CMETA_VALUE *case_value,
    JINJA_CMETA_VALUE *result) {
  enum { GROUPBY_VALUE_SLOTS = 5 };
  JINJA_CMETA_VALUE parts;
  JINJA_CMETA_VALUE list;
  JINJA_CMETA_VALUE *sorted;
  JINJA_CMETA_VALUE *keys;
  JINJA_CMETA_VALUE *groups;
  JINJA_CMETA_VALUE *pairs;
  size_t group_count = 0u;
  int case_sensitive = 0;
  JINJA_CMETA_STATUS status;

  if (attribute == NULL) return JINJA_CMETA_ERR_RENDER;
  status = jinja_attribute_parts(provider, attribute, &parts);
  if (status != JINJA_CMETA_OK) return status;
  if (case_value != NULL) {
    status = jinja_value_truthy(provider, case_value, &case_sensitive);
    if (status != JINJA_CMETA_OK) return status;
  }
  status = jinja_materialize_list(provider, operand, &list);
  if (status != JINJA_CMETA_OK) return status;
  if (list.collection_item_count == 0u) {
    *result = list;
    return JINJA_CMETA_OK;
  }
  if (list.collection_item_count >
      (provider->shared.values.limit - provider->shared.collection_value_count) / GROUPBY_VALUE_SLOTS)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider,
      list.collection_item_count * GROUPBY_VALUE_SLOTS);
  if (status != JINJA_CMETA_OK) return status;
  sorted = jinja_cmeta_values_at(&provider->shared.values, provider->shared.collection_value_count);
  keys = sorted + list.collection_item_count;
  groups = keys + list.collection_item_count;
  pairs = groups + list.collection_item_count;
  provider->shared.collection_value_count += list.collection_item_count * GROUPBY_VALUE_SLOTS;
  for (size_t i = 0u; i < list.collection_item_count; ++i) {
    sorted[i] = list.collection_values[i];
    keys[i] = sorted[i];
    status = jinja_project_attribute(provider, &parts, default_value, &keys[i]);
    if (status == JINJA_CMETA_OK && !case_sensitive && jinja_value_is_string(&keys[i]))
      status = jinja_case_value(provider, &keys[i], SALTS_UNICODE_CASE_LOWER, &keys[i]);
    if (status != JINJA_CMETA_OK) return status;
  }
  for (size_t i = 1u; i < list.collection_item_count; ++i) {
    size_t cursor = i;
    while (cursor != 0u) {
      int move = 0;
      status = jinja_sort_before(provider, &keys[cursor - 1u], &keys[cursor], 1u, 0, &move);
      if (status != JINJA_CMETA_OK || !move) break;
      JINJA_CMETA_VALUE item = sorted[cursor];
      sorted[cursor] = sorted[cursor - 1u];
      sorted[cursor - 1u] = item;
      item = keys[cursor];
      keys[cursor] = keys[cursor - 1u];
      keys[cursor - 1u] = item;
      --cursor;
    }
    if (status != JINJA_CMETA_OK) return status;
  }
  for (size_t begin = 0u; begin < list.collection_item_count;) {
    size_t end = begin + 1u;
    while (end < list.collection_item_count) {
      int equal = 0;
      status = jinja_container_equal(provider, &keys[begin], &keys[end], 0u, &equal);
      if (status != JINJA_CMETA_OK) return status;
      if (!equal) break;
      ++end;
    }
    pairs[group_count * 2u] = sorted[begin];
    status = jinja_project_attribute(provider, &parts, default_value, &pairs[group_count * 2u]);
    if (status != JINJA_CMETA_OK) return status;
    pairs[group_count * 2u + 1u] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
        .collection_item_count = end - begin, .collection_values = sorted + begin};
    groups[group_count] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
        .identity = {.source = &jinja_groupby_identity}, .collection_item_count = 2u,
        .collection_values = pairs + group_count * 2u};
    ++group_count;
    begin = end;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
      .collection_item_count = group_count, .collection_values = groups};
  return JINJA_CMETA_OK;
}

static uint64_t jinja_random_next(JINJA_CMETA_PROVIDER *provider) {
  uint64_t state = provider->random_state;
  if (state == 0u) {
    state = (uint64_t)(uintptr_t)provider ^ ((uint64_t)(uintptr_t)provider->instance << 1u) ^
        (uint64_t)clock();
    if (state == 0u) state = UINT64_C(0x9e3779b97f4a7c15);
  }
  state += UINT64_C(0x9e3779b97f4a7c15);
  provider->random_state = state;
  uint64_t value = state;
  value = (value ^ (value >> 30u)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27u)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31u);
}

static JINJA_CMETA_STATUS jinja_random_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE length;
  uint64_t bound;
  uint64_t limit;
  uint64_t sample;
  JINJA_CMETA_STATUS status;
  jinja_normalize_call_argument(operand);
  if (!jinja_value_is_collection(operand->kind) && operand->kind != JINJA_CMETA_VALUE_RANGE &&
      !jinja_value_is_string(operand))
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_value_length(provider, operand, &length);
  if (status != JINJA_CMETA_OK) return status;
  if (length.integer == 0) {
    *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
    return JINJA_CMETA_OK;
  }
  if (length.integer < 0) return JINJA_CMETA_ERR_RENDER;
  bound = (uint64_t)length.integer;
  limit = UINT64_MAX - UINT64_MAX % bound;
  do sample = jinja_random_next(provider); while (sample >= limit);
  JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)(sample % bound)};
  return jinja_lookup_item(provider, operand, &key, 0u, result);
}

static JINJA_CMETA_STATUS jinja_pprint_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  const size_t start = provider->shared.slice_byte_count;
  JINJA_CMETA_STATUS status = jinja_dump_value_repr(provider, operand, 0u,
      jinja_concat_write, provider);
  if (status != JINJA_CMETA_OK) return status;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string = vstr_from_buf(provider->shared.slice_bytes + start,
          provider->shared.slice_byte_count - start)};
  return JINJA_CMETA_OK;
}

static int jinja_striptags_space(salts_unicode_scalar scalar) {
  return (scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) != 0u ||
      (scalar.value >= 0x1cu && scalar.value <= 0x1fu);
}

static int jinja_striptags_scalar_space(uint32_t scalar) {
  return (scalar >= 0x09u && scalar <= 0x0du) ||
      (scalar >= 0x1cu && scalar <= 0x20u) || scalar == 0x85u || scalar == 0xa0u ||
      scalar == 0x1680u || (scalar >= 0x2000u && scalar <= 0x200au) ||
      scalar == 0x2028u || scalar == 0x2029u || scalar == 0x202fu ||
      scalar == 0x205fu || scalar == 0x3000u;
}

static size_t jinja_striptags_utf8(uint32_t scalar, char output[4]) {
  if (scalar <= 0x7fu) {
    output[0] = (char)scalar;
    return 1u;
  }
  if (scalar <= 0x7ffu) {
    output[0] = (char)(0xc0u | (scalar >> 6u));
    output[1] = (char)(0x80u | (scalar & 0x3fu));
    return 2u;
  }
  if (scalar <= 0xffffu) {
    output[0] = (char)(0xe0u | (scalar >> 12u));
    output[1] = (char)(0x80u | ((scalar >> 6u) & 0x3fu));
    output[2] = (char)(0x80u | (scalar & 0x3fu));
    return 3u;
  }
  output[0] = (char)(0xf0u | (scalar >> 18u));
  output[1] = (char)(0x80u | ((scalar >> 12u) & 0x3fu));
  output[2] = (char)(0x80u | ((scalar >> 6u) & 0x3fu));
  output[3] = (char)(0x80u | (scalar & 0x3fu));
  return 4u;
}

static int jinja_striptags_entity(vstr name, uint32_t *scalar) {
  uint64_t value = 0u;
  size_t index = 0u;
  unsigned base = 10u;
  if (name.len != 0u && name.data[0] == '#') {
    index = 1u;
    if (index < name.len && (name.data[index] == 'x' || name.data[index] == 'X')) {
      base = 16u;
      ++index;
    }
    if (index == name.len) return 0;
    for (; index < name.len; ++index) {
      unsigned digit;
      const unsigned char c = (unsigned char)name.data[index];
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (base == 16u && c >= 'a' && c <= 'f') digit = c - 'a' + 10u;
      else if (base == 16u && c >= 'A' && c <= 'F') digit = c - 'A' + 10u;
      else return 0;
      if (digit >= base || value > (UINT32_MAX - digit) / base) return 0;
      value = value * base + digit;
    }
    *scalar = value == 0u || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu) ? 0xfffdu : (uint32_t)value;
    return 1;
  }
  if (vstr_eq(name, vstr_from_cstr("amp"))) *scalar = '&';
  else if (vstr_eq(name, vstr_from_cstr("lt"))) *scalar = '<';
  else if (vstr_eq(name, vstr_from_cstr("gt"))) *scalar = '>';
  else if (vstr_eq(name, vstr_from_cstr("quot"))) *scalar = '"';
  else if (vstr_eq(name, vstr_from_cstr("apos"))) *scalar = '\'';
  else if (vstr_eq(name, vstr_from_cstr("nbsp"))) *scalar = 0x00a0u;
  else if (vstr_eq(name, vstr_from_cstr("laquo"))) *scalar = 0x00abu;
  else if (vstr_eq(name, vstr_from_cstr("raquo"))) *scalar = 0x00bbu;
  else if (vstr_eq(name, vstr_from_cstr("copy"))) *scalar = 0x00a9u;
  else if (vstr_eq(name, vstr_from_cstr("reg"))) *scalar = 0x00aeu;
  else if (vstr_eq(name, vstr_from_cstr("trade"))) *scalar = 0x2122u;
  else if (vstr_eq(name, vstr_from_cstr("hellip"))) *scalar = 0x2026u;
  else if (vstr_eq(name, vstr_from_cstr("ndash"))) *scalar = 0x2013u;
  else if (vstr_eq(name, vstr_from_cstr("mdash"))) *scalar = 0x2014u;
  else return 0;
  return 1;
}

static JINJA_CMETA_STATUS jinja_striptags_write_scalar(JINJA_CMETA_PROVIDER *provider,
    salts_unicode_scalar scalar, const char *bytes, size_t size, int *pending_space, int *emitted) {
  if (jinja_striptags_space(scalar)) {
    if (*emitted) *pending_space = 1;
    return JINJA_CMETA_OK;
  }
  if (*pending_space) {
    if (jinja_concat_write(" ", 1u, provider) != 0) return provider->shared.status;
    *pending_space = 0;
  }
  if (jinja_concat_write(bytes, size, provider) != 0) return provider->shared.status;
  *emitted = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_striptags_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  size_t cursor = 0u;
  const size_t start = provider->shared.slice_byte_count;
  int pending_space = 0;
  int emitted = 0;
  if (status != JINJA_CMETA_OK) return status;
  if (vstr_utf8_invalid_offset(string.string) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  while (cursor < string.string.len) {
    if (cursor + 4u <= string.string.len &&
        memcmp(string.string.data + cursor, "<!--", 4u) == 0) {
      size_t end = cursor + 4u;
      while (end + 3u <= string.string.len &&
             memcmp(string.string.data + end, "-->", 3u) != 0) ++end;
      if (end + 3u <= string.string.len) {
        cursor = end + 3u;
        continue;
      }
    }
    if (string.string.data[cursor] == '<') {
      size_t end = cursor + 1u;
      while (end < string.string.len && string.string.data[end] != '>') ++end;
      if (end < string.string.len) {
        cursor = end + 1u;
        continue;
      }
    }
    if (string.string.data[cursor] == '&') {
      size_t end = cursor + 1u;
      while (end < string.string.len && string.string.data[end] != ';' && end - cursor <= 32u) ++end;
      if (end < string.string.len && string.string.data[end] == ';') {
        uint32_t decoded;
        if (jinja_striptags_entity(vstr_from_buf(string.string.data + cursor + 1u, end - cursor - 1u),
                                  &decoded)) {
          char encoded[4];
          const size_t size = jinja_striptags_utf8(decoded, encoded);
          salts_unicode_scalar scalar = {.value = decoded,
              .properties = jinja_striptags_scalar_space(decoded)
                  ? SALTS_UNICODE_PROPERTY_WHITE_SPACE : SALTS_UNICODE_PROPERTY_NONE};
          status = jinja_striptags_write_scalar(provider, scalar, encoded, size, &pending_space, &emitted);
          if (status != JINJA_CMETA_OK) return status;
          cursor = end + 1u;
          continue;
        }
      }
    }
    salts_unicode_scalar scalar;
    const size_t before = cursor;
    if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    status = jinja_striptags_write_scalar(provider, scalar, string.string.data + before,
        cursor - before, &pending_space, &emitted);
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string = vstr_from_buf(provider->shared.slice_bytes + start,
          provider->shared.slice_byte_count - start)};
  return JINJA_CMETA_OK;
}

typedef struct JINJA_CMETA_JSON_ENTRY {
  JINJA_CMETA_VALUE key;
  JINJA_CMETA_VALUE value;
} JINJA_CMETA_JSON_ENTRY;

static JINJA_CMETA_STATUS jinja_json_write(JINJA_CMETA_PROVIDER *provider,
                                           const char *text, size_t size) {
  if (jinja_concat_write(text, size, provider) != 0) return provider->shared.status;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_json_escape_u16(JINJA_CMETA_PROVIDER *provider, uint16_t value) {
  static const char hex[] = "0123456789abcdef";
  const char escaped[] = {'\\', 'u', hex[(value >> 12u) & 0x0fu], hex[(value >> 8u) & 0x0fu],
                          hex[(value >> 4u) & 0x0fu], hex[value & 0x0fu]};
  return jinja_json_write(provider, escaped, sizeof(escaped));
}

static JINJA_CMETA_STATUS jinja_json_string(JINJA_CMETA_PROVIDER *provider, vstr string) {
  size_t cursor = 0u;
  JINJA_CMETA_STATUS status;

  if (!vstr_is_valid(string) || vstr_utf8_invalid_offset(string) != SIZE_MAX)
    return JINJA_CMETA_ERR_METADATA;
  status = jinja_json_write(provider, "\"", 1u);
  while (status == JINJA_CMETA_OK && cursor < string.len) {
    salts_unicode_scalar scalar;
    const size_t before = cursor;
    if (salts_unicode_utf8_next(string, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    switch (scalar.value) {
    case '\b': status = jinja_json_write(provider, "\\b", 2u); break;
    case '\t': status = jinja_json_write(provider, "\\t", 2u); break;
    case '\n': status = jinja_json_write(provider, "\\n", 2u); break;
    case '\f': status = jinja_json_write(provider, "\\f", 2u); break;
    case '\r': status = jinja_json_write(provider, "\\r", 2u); break;
    case '"': status = jinja_json_write(provider, "\\\"", 2u); break;
    case '\\': status = jinja_json_write(provider, "\\\\", 2u); break;
    case '\'': status = jinja_json_write(provider, "\\u0027", 6u); break;
    case '&': status = jinja_json_write(provider, "\\u0026", 6u); break;
    case '<': status = jinja_json_write(provider, "\\u003c", 6u); break;
    case '>': status = jinja_json_write(provider, "\\u003e", 6u); break;
    default:
      if (scalar.value < 0x20u) status = jinja_json_escape_u16(provider, (uint16_t)scalar.value);
      else if (scalar.value <= 0x7fu)
        status = jinja_json_write(provider, string.data + before, cursor - before);
      else if (scalar.value <= 0xffffu)
        status = jinja_json_escape_u16(provider, (uint16_t)scalar.value);
      else {
        const uint32_t pair = scalar.value - 0x10000u;
        status = jinja_json_escape_u16(provider, (uint16_t)(0xd800u + (pair >> 10u)));
        if (status == JINJA_CMETA_OK)
          status = jinja_json_escape_u16(provider, (uint16_t)(0xdc00u + (pair & 0x3ffu)));
      }
      break;
    }
  }
  if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, "\"", 1u);
  return status;
}

static JINJA_CMETA_STATUS jinja_json_float_text(double value, char output[64], size_t *size) {
  int written;
  size_t length;

  if (!isfinite(value) || output == NULL || size == NULL) return JINJA_CMETA_ERR_RENDER;
  written = snprintf(output, 64u, "%.17g", value);
  if (written < 0 || written >= 64) return JINJA_CMETA_ERR_RENDER;
  length = (size_t)written;
  if (strchr(output, '.') == NULL && strchr(output, 'e') == NULL && strchr(output, 'E') == NULL) {
    if (length + 2u >= 64u) return JINJA_CMETA_ERR_RENDER;
    output[length++] = '.';
    output[length++] = '0';
    output[length] = '\0';
  }
  *size = length;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_json_scalar(JINJA_CMETA_PROVIDER *provider,
                                            const JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, value, &scalar);
  char number[64];
  size_t size = 0u;

  if (status != JINJA_CMETA_OK) return status;
  switch (scalar.kind) {
  case JINJA_CMETA_SCALAR_NONE: return jinja_json_write(provider, "null", 4u);
  case JINJA_CMETA_SCALAR_BOOL:
    return jinja_json_write(provider, scalar.boolean ? "true" : "false", scalar.boolean ? 4u : 5u);
  case JINJA_CMETA_SCALAR_SINT:
    size = (size_t)snprintf(number, sizeof(number), "%lld", (long long)scalar.sint);
    break;
  case JINJA_CMETA_SCALAR_UINT:
    size = (size_t)snprintf(number, sizeof(number), "%llu", (unsigned long long)scalar.uint);
    break;
  case JINJA_CMETA_SCALAR_FLOAT:
    status = jinja_json_float_text(scalar.floating, number, &size);
    if (status != JINJA_CMETA_OK) return status;
    break;
  case JINJA_CMETA_SCALAR_STRING:
    return jinja_json_string(provider, vstr_from_buf((const char *)scalar.data, scalar.size));
  case JINJA_CMETA_SCALAR_UNDEFINED:
    return JINJA_CMETA_ERR_RENDER;
  }
  if (size >= sizeof(number)) return JINJA_CMETA_ERR_RENDER;
  return jinja_json_write(provider, number, size);
}

static JINJA_CMETA_STATUS jinja_json_key(JINJA_CMETA_PROVIDER *provider,
                                         const JINJA_CMETA_VALUE *key) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, key, &scalar);
  char number[64];
  size_t size = 0u;

  if (status != JINJA_CMETA_OK) return status;
  switch (scalar.kind) {
  case JINJA_CMETA_SCALAR_STRING:
    return jinja_json_string(provider, vstr_from_buf((const char *)scalar.data, scalar.size));
  case JINJA_CMETA_SCALAR_NONE: return jinja_json_string(provider, vstr_from_cstr("null"));
  case JINJA_CMETA_SCALAR_BOOL:
    return jinja_json_string(provider, vstr_from_cstr(scalar.boolean ? "true" : "false"));
  case JINJA_CMETA_SCALAR_SINT:
    size = (size_t)snprintf(number, sizeof(number), "%lld", (long long)scalar.sint);
    break;
  case JINJA_CMETA_SCALAR_UINT:
    size = (size_t)snprintf(number, sizeof(number), "%llu", (unsigned long long)scalar.uint);
    break;
  case JINJA_CMETA_SCALAR_FLOAT:
    status = jinja_json_float_text(scalar.floating, number, &size);
    if (status != JINJA_CMETA_OK) return status;
    break;
  case JINJA_CMETA_SCALAR_UNDEFINED:
    return JINJA_CMETA_ERR_RENDER;
  }
  if (size >= sizeof(number)) return JINJA_CMETA_ERR_RENDER;
  return jinja_json_string(provider, vstr_from_buf(number, size));
}

static JINJA_CMETA_STATUS jinja_json_indent(JINJA_CMETA_PROVIDER *provider, size_t width,
                                            size_t depth) {
  static const char spaces[] = "                                ";
  size_t count;
  JINJA_CMETA_STATUS status = jinja_json_write(provider, "\n", 1u);
  if (status != JINJA_CMETA_OK) return status;
  if (width != 0u && depth > provider->shared.max_string_bytes / width)
    return JINJA_CMETA_ERR_CAPACITY;
  count = width * depth;
  while (count != 0u) {
    const size_t chunk = count < sizeof(spaces) - 1u ? count : sizeof(spaces) - 1u;
    status = jinja_json_write(provider, spaces, chunk);
    if (status != JINJA_CMETA_OK) return status;
    count -= chunk;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_json_value(JINJA_CMETA_PROVIDER *provider,
                                           const JINJA_CMETA_VALUE *value, size_t depth,
                                           int pretty, size_t indent);

static JINJA_CMETA_STATUS jinja_json_array(JINJA_CMETA_PROVIDER *provider,
                                           const JINJA_CMETA_VALUE *values, size_t count,
                                           size_t depth, int pretty, size_t indent) {
  JINJA_CMETA_STATUS status;
  if (count != 0u && values == NULL) return JINJA_CMETA_ERR_RENDER;
  status = jinja_json_write(provider, "[", 1u);
  for (size_t i = 0u; status == JINJA_CMETA_OK && i < count; ++i) {
    if (pretty) status = jinja_json_indent(provider, indent, depth + 1u);
    if (status == JINJA_CMETA_OK)
      status = jinja_json_value(provider, &values[i], depth + 1u, pretty, indent);
    if (status == JINJA_CMETA_OK && i + 1u < count)
      status = jinja_json_write(provider, pretty ? "," : ", ", pretty ? 1u : 2u);
  }
  if (status == JINJA_CMETA_OK && pretty && count != 0u)
    status = jinja_json_indent(provider, indent, depth);
  if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, "]", 1u);
  return status;
}

static JINJA_CMETA_STATUS jinja_json_range(JINJA_CMETA_PROVIDER *provider,
                                           const JINJA_CMETA_RANGE *range, size_t depth,
                                           int pretty, size_t indent) {
  JINJA_CMETA_STATUS status;
  if (range == NULL) return JINJA_CMETA_ERR_RENDER;
  status = jinja_json_write(provider, "[", 1u);
  for (uint64_t i = 0u; status == JINJA_CMETA_OK && i < range->count; ++i) {
    JINJA_CMETA_VALUE item = {.kind = JINJA_CMETA_VALUE_INTEGER,
                              .integer = jinja_range_item(range, i)};
    if (pretty) status = jinja_json_indent(provider, indent, depth + 1u);
    if (status == JINJA_CMETA_OK)
      status = jinja_json_value(provider, &item, depth + 1u, pretty, indent);
    if (status == JINJA_CMETA_OK && i + 1u < range->count)
      status = jinja_json_write(provider, pretty ? "," : ", ", pretty ? 1u : 2u);
  }
  if (status == JINJA_CMETA_OK && pretty && range->count != 0u)
    status = jinja_json_indent(provider, indent, depth);
  if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, "]", 1u);
  return status;
}

static JINJA_CMETA_STATUS jinja_json_key_order(JINJA_CMETA_PROVIDER *provider,
                                               const JINJA_CMETA_VALUE *left,
                                               const JINJA_CMETA_VALUE *right, int *after) {
  JINJA_CMETA_SCALAR a = {0}, b = {0};
  JINJA_CMETA_STATUS status;
  size_t common;
  int compare;

  if (after == NULL) return JINJA_CMETA_ERR_RENDER;
  *after = 0;
  status = jinja_scalar_from_value(provider, left, &a);
  if (status == JINJA_CMETA_OK) status = jinja_scalar_from_value(provider, right, &b);
  if (status != JINJA_CMETA_OK) return status;
  if (a.kind != JINJA_CMETA_SCALAR_STRING || b.kind != JINJA_CMETA_SCALAR_STRING) return JINJA_CMETA_OK;
  if (vstr_utf8_invalid_offset(vstr_from_buf((const char *)a.data, a.size)) != SIZE_MAX ||
      vstr_utf8_invalid_offset(vstr_from_buf((const char *)b.data, b.size)) != SIZE_MAX)
    return JINJA_CMETA_ERR_METADATA;
  common = a.size < b.size ? a.size : b.size;
  compare = common == 0u ? 0 : memcmp(a.data, b.data, common);
  *after = compare > 0 || (compare == 0 && a.size > b.size);
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_json_dict(JINJA_CMETA_PROVIDER *provider,
                                          const JINJA_CMETA_VALUE *dict, size_t depth,
                                          int pretty, size_t indent) {
  JINJA_CMETA_JSON_ENTRY *entries;
  size_t count, used = 0u;
  JINJA_CMETA_STATUS status;

  if (dict == NULL || dict->kind != JINJA_CMETA_VALUE_DICT) return JINJA_CMETA_ERR_RENDER;
  status = jinja_dict_unique_count(provider, dict, depth, &count);
  if (status != JINJA_CMETA_OK) return status;
  entries = count == 0u ? NULL : (JINJA_CMETA_JSON_ENTRY *)jinja_provider_zero(provider, count,
      sizeof(*entries));
  if (count != 0u && entries == NULL) return provider->shared.status;
  for (size_t i = 0u; status == JINJA_CMETA_OK && i < dict->collection_item_count; ++i) {
    JINJA_CMETA_VALUE ignored;
    int first;
    status = jinja_dict_entry_is_first(provider, dict, i, depth, &first);
    if (status == JINJA_CMETA_OK && first) {
      int found = 0;
      status = jinja_dict_entry_value(provider, dict, i, depth, &entries[used].key, &ignored);
      if (status == JINJA_CMETA_OK)
        status = jinja_dict_lookup(provider, dict, &entries[used].key, depth, &entries[used].value, &found);
      if (status == JINJA_CMETA_OK && !found) status = JINJA_CMETA_ERR_RENDER;
      if (status == JINJA_CMETA_OK) ++used;
    }
  }
  if (status != JINJA_CMETA_OK) goto cleanup;
  if (used != count) {
    status = JINJA_CMETA_ERR_RENDER;
    goto cleanup;
  }
  for (size_t i = 1u; i < count; ++i) {
    size_t cursor = i;
    while (cursor != 0u) {
      int after;
      status = jinja_json_key_order(provider, &entries[cursor - 1u].key, &entries[cursor].key, &after);
      if (status != JINJA_CMETA_OK || !after) break;
      JINJA_CMETA_JSON_ENTRY swap = entries[cursor];
      entries[cursor] = entries[cursor - 1u];
      entries[cursor - 1u] = swap;
      --cursor;
    }
    if (status != JINJA_CMETA_OK) goto cleanup;
  }
  status = jinja_json_write(provider, "{", 1u);
  for (size_t i = 0u; status == JINJA_CMETA_OK && i < count; ++i) {
    if (pretty) status = jinja_json_indent(provider, indent, depth + 1u);
    if (status == JINJA_CMETA_OK) status = jinja_json_key(provider, &entries[i].key);
    if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, ": ", 2u);
    if (status == JINJA_CMETA_OK)
      status = jinja_json_value(provider, &entries[i].value, depth + 1u, pretty, indent);
    if (status == JINJA_CMETA_OK && i + 1u < count)
      status = jinja_json_write(provider, pretty ? "," : ", ", pretty ? 1u : 2u);
  }
  if (status == JINJA_CMETA_OK && pretty && count != 0u)
    status = jinja_json_indent(provider, indent, depth);
  if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, "}", 1u);
cleanup:
  jinja_cmeta_memory_drop(entries);
  return status;
}

static JINJA_CMETA_STATUS jinja_json_struct(JINJA_CMETA_PROVIDER *provider,
                                            const JINJA_CMETA_NODE *node, size_t depth,
                                            int pretty, size_t indent) {
  const cmeta_data_struct_shape *shape;
  JINJA_CMETA_STATUS status;

  if (node == NULL || node->object == NULL || !cmeta_data_desc_valid(node->desc) ||
      node->desc->kind != CMETA_DATA_STRUCT)
    return JINJA_CMETA_ERR_METADATA;
  shape = (const cmeta_data_struct_shape *)node->desc->shape;
  if (shape == NULL || (shape->field_count != 0u && shape->fields == NULL))
    return JINJA_CMETA_ERR_METADATA;
  status = jinja_json_write(provider, "{", 1u);
  for (size_t i = 0u; status == JINJA_CMETA_OK && i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    const void *child;
    JINJA_CMETA_VALUE child_value;
    if (field->name == NULL || !cmeta_data_desc_valid(field->value) ||
        !jinja_child_address(node, field, &child))
      return JINJA_CMETA_ERR_METADATA;
    if (pretty) status = jinja_json_indent(provider, indent, depth + 1u);
    if (status == JINJA_CMETA_OK) status = jinja_json_string(provider, vstr_from_cstr(field->name));
    if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, ": ", 2u);
    child_value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NODE,
        .node = {.object = child, .desc = field->value, .parent = node->parent}};
    if (status == JINJA_CMETA_OK)
      status = jinja_json_value(provider, &child_value, depth + 1u, pretty, indent);
    if (status == JINJA_CMETA_OK && i + 1u < shape->field_count)
      status = jinja_json_write(provider, pretty ? "," : ", ", pretty ? 1u : 2u);
  }
  if (status == JINJA_CMETA_OK && pretty && shape->field_count != 0u)
    status = jinja_json_indent(provider, indent, depth);
  if (status == JINJA_CMETA_OK) status = jinja_json_write(provider, "}", 1u);
  return status;
}

static JINJA_CMETA_STATUS jinja_json_value_impl(JINJA_CMETA_PROVIDER *provider,
                                                const JINJA_CMETA_VALUE *value, size_t depth,
                                                int pretty, size_t indent) {
  if (value->kind == JINJA_CMETA_VALUE_LIST || value->kind == JINJA_CMETA_VALUE_TUPLE)
    return jinja_json_array(provider, value->collection_values, value->collection_item_count,
                            depth, pretty, indent);
  if (value->kind == JINJA_CMETA_VALUE_RANGE)
    return jinja_json_range(provider, &value->range, depth, pretty, indent);
  if (value->kind == JINJA_CMETA_VALUE_DICT)
    return jinja_json_dict(provider, value, depth, pretty, indent);
  if (value->kind == JINJA_CMETA_VALUE_NODE) {
    const cmeta_data_desc *desc = value->node.desc;
    if (value->node.object == NULL || !cmeta_data_desc_valid(desc)) return JINJA_CMETA_ERR_METADATA;
    if (desc->kind == CMETA_DATA_STRUCT)
      return jinja_json_struct(provider, &value->node, depth, pretty, indent);
    if (desc->kind == CMETA_DATA_SEQUENCE || desc->kind == CMETA_DATA_SET ||
        (desc->kind == CMETA_DATA_CUSTOM && jinja_is_sequence_desc(desc))) {
      JINJA_CMETA_VALUE source = *value;
      JINJA_CMETA_VALUE list;
      JINJA_CMETA_STATUS status = jinja_materialize_list(provider, &source, &list);
      if (status != JINJA_CMETA_OK) return status;
      return jinja_json_array(provider, list.collection_values, list.collection_item_count,
                              depth, pretty, indent);
    }
  }
  return jinja_json_scalar(provider, value);
}

static JINJA_CMETA_STATUS jinja_json_value(JINJA_CMETA_PROVIDER *provider,
                                           const JINJA_CMETA_VALUE *value, size_t depth,
                                           int pretty, size_t indent) {
  JINJA_CMETA_STATUS status = jinja_value_traversal_enter(provider);
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_json_value_impl(provider, value, depth, pretty, indent);
  --provider->shared.value_depth;
  return status;
}

static JINJA_CMETA_STATUS jinja_tojson_value(JINJA_CMETA_PROVIDER *provider,
                                             JINJA_CMETA_VALUE *operand,
                                             const JINJA_CMETA_VALUE *indent_value,
                                             JINJA_CMETA_VALUE *result) {
  const size_t start = provider->shared.slice_byte_count;
  size_t indent = 0u;
  int pretty = 0;
  JINJA_CMETA_STATUS status;

  if (indent_value != NULL && indent_value->kind != JINJA_CMETA_VALUE_NONE) {
    int valid;
    int64_t requested;
    status = jinja_lookup_integer_key(provider, indent_value, &requested, &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid || requested < 0) return JINJA_CMETA_ERR_RENDER;
    indent = (size_t)requested;
    pretty = 1;
  }
  status = jinja_json_value(provider, operand, 0u, pretty, indent);
  if (status != JINJA_CMETA_OK) return status;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string_safe = 1,
      .string = vstr_from_buf(provider->shared.slice_bytes + start,
          provider->shared.slice_byte_count - start)};
  return JINJA_CMETA_OK;
}

typedef enum JINJA_CMETA_URLIZE_KIND {
  JINJA_CMETA_URLIZE_NONE,
  JINJA_CMETA_URLIZE_HTTP,
  JINJA_CMETA_URLIZE_WWW,
  JINJA_CMETA_URLIZE_MAILTO,
  JINJA_CMETA_URLIZE_EMAIL,
  JINJA_CMETA_URLIZE_EXTRA
} JINJA_CMETA_URLIZE_KIND;

static unsigned char jinja_urlize_lower(unsigned char value) {
  return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static int jinja_urlize_starts(vstr value, const char *prefix) {
  size_t length = strlen(prefix);
  if (value.len < length) return 0;
  for (size_t i = 0u; i < length; ++i)
    if (jinja_urlize_lower((unsigned char)value.data[i]) != (unsigned char)prefix[i]) return 0;
  return 1;
}

static int jinja_urlize_host(vstr host) {
  size_t last_dot = SIZE_MAX;
  if (host.len == 0u) return 0;
  if (host.data[0] == '[' && host.len >= 4u && host.data[host.len - 1u] == ']') {
    for (size_t i = 1u; i + 1u < host.len; ++i) {
      unsigned char c = (unsigned char)host.data[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F') || c == ':')) return 0;
    }
    return 1;
  }
  for (size_t i = 0u; i < host.len; ++i) {
    unsigned char c = (unsigned char)host.data[i];
    if (c == '.') {
      if (i == 0u || i + 1u == host.len || host.data[i - 1u] == '.' || host.data[i + 1u] == '.') return 0;
      last_dot = i;
    } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '%')) return 0;
  }
  return last_dot != SIZE_MAX && host.len - last_dot >= 3u;
}

static int jinja_urlize_email(vstr value) {
  size_t at = SIZE_MAX;
  for (size_t i = 0u; i < value.len; ++i) {
    unsigned char c = (unsigned char)value.data[i];
    if (c == '@') {
      if (at != SIZE_MAX) return 0;
      at = i;
    } else if (c <= ' ' || c == ':' || c == '<' || c == '>' || c == '(' || c == ')') return 0;
  }
  return at != SIZE_MAX && at != 0u && at + 1u < value.len &&
      jinja_urlize_host(vstr_from_buf(value.data + at + 1u, value.len - at - 1u));
}

static int jinja_urlize_extra_scheme(vstr scheme) {
  size_t colon = 0u;
  if (scheme.len < 3u) return 0;
  while (colon < scheme.len && scheme.data[colon] != ':') {
    unsigned char c = (unsigned char)scheme.data[colon];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '.' || c == '+' || c == '-')) return 0;
    ++colon;
  }
  return colon >= 2u && colon < scheme.len &&
      (colon + 1u == scheme.len ||
       (colon + 2u == scheme.len && scheme.data[colon + 1u] == '/') ||
       (colon + 3u == scheme.len && scheme.data[colon + 1u] == '/' && scheme.data[colon + 2u] == '/'));
}

static JINJA_CMETA_URLIZE_KIND jinja_urlize_kind(vstr word,
    const JINJA_CMETA_VALUE *extra_schemes, size_t extra_count) {
  size_t host_start = 0u;
  if (jinja_urlize_starts(word, "http://")) host_start = sizeof("http://") - 1u;
  else if (jinja_urlize_starts(word, "https://")) host_start = sizeof("https://") - 1u;
  if (host_start != 0u) {
    size_t host_end = host_start;
    while (host_end < word.len && word.data[host_end] != ':' && word.data[host_end] != '/' &&
           word.data[host_end] != '?' && word.data[host_end] != '#') ++host_end;
    if (jinja_urlize_host(vstr_from_buf(word.data + host_start, host_end - host_start)))
      return JINJA_CMETA_URLIZE_HTTP;
  }
  if (jinja_urlize_starts(word, "www.")) {
    size_t host_end = 0u;
    while (host_end < word.len && word.data[host_end] != '/' && word.data[host_end] != '?' &&
           word.data[host_end] != '#') ++host_end;
    if (jinja_urlize_host(vstr_from_buf(word.data, host_end))) return JINJA_CMETA_URLIZE_WWW;
  }
  if (jinja_urlize_starts(word, "mailto:") && word.len > sizeof("mailto:") - 1u &&
      jinja_urlize_email(vstr_from_buf(word.data + sizeof("mailto:") - 1u,
          word.len - (sizeof("mailto:") - 1u))))
    return JINJA_CMETA_URLIZE_MAILTO;
  if (word.len != 0u && word.data[0] != '@' && !jinja_urlize_starts(word, "www.") &&
      jinja_urlize_email(word)) return JINJA_CMETA_URLIZE_EMAIL;
  for (size_t i = 0u; i < extra_count; ++i) {
    vstr scheme = extra_schemes[i].string;
    if (word.len > scheme.len && memcmp(word.data, scheme.data, scheme.len) == 0)
      return JINJA_CMETA_URLIZE_EXTRA;
  }
  return JINJA_CMETA_URLIZE_NONE;
}

static JINJA_CMETA_STATUS jinja_urlize_write_text(JINJA_CMETA_PROVIDER *provider,
    vstr text, int safe) {
  if (text.len == 0u) return JINJA_CMETA_OK;
  const int written = safe ? jinja_concat_write(text.data, text.len, provider)
      : jinja_html_write(text.data, text.len, jinja_concat_write, provider);
  return written == 0 ? JINJA_CMETA_OK : provider->shared.status;
}

static JINJA_CMETA_STATUS jinja_urlize_trim(vstr input, int has_limit, int64_t limit,
    vstr *display, int *ellipsis) {
  size_t count = 0u;
  size_t cursor = 0u;
  if (display == NULL || ellipsis == NULL) return JINJA_CMETA_ERR_RENDER;
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    ++count;
  }
  *display = input;
  *ellipsis = 0;
  if (!has_limit || (limit >= 0 && count <= (uint64_t)limit)) return JINJA_CMETA_OK;
  size_t retain;
  if (limit >= 0) retain = (size_t)limit;
  else {
    uint64_t magnitude = UINT64_C(0) - (uint64_t)limit;
    retain = magnitude >= count ? 0u : count - (size_t)magnitude;
  }
  cursor = 0u;
  for (size_t i = 0u; i < retain; ++i) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
  }
  *display = vstr_from_buf(input.data, cursor);
  *ellipsis = 1;
  return JINJA_CMETA_OK;
}

static int jinja_urlize_rel_has_nofollow(vstr rel) {
  size_t cursor = 0u;
  while (cursor < rel.len) {
    while (cursor < rel.len && (unsigned char)rel.data[cursor] <= ' ') ++cursor;
    size_t start = cursor;
    while (cursor < rel.len && (unsigned char)rel.data[cursor] > ' ') ++cursor;
    if (cursor - start == sizeof("nofollow") - 1u &&
        memcmp(rel.data + start, "nofollow", sizeof("nofollow") - 1u) == 0) return 1;
  }
  return 0;
}

static JINJA_CMETA_STATUS jinja_urlize_link(JINJA_CMETA_PROVIDER *provider,
    vstr word, JINJA_CMETA_URLIZE_KIND kind, int input_safe, int has_limit, int64_t limit,
    int nofollow, const JINJA_CMETA_VALUE *target, const JINJA_CMETA_VALUE *rel) {
  const size_t mailto_size = sizeof("mailto:") - 1u;
  vstr display = word;
  int ellipsis = 0;
  JINJA_CMETA_STATUS status;
  if (kind == JINJA_CMETA_URLIZE_MAILTO) display = vstr_from_buf(word.data + mailto_size, word.len - mailto_size);
  status = jinja_urlize_trim(display, has_limit, limit, &display, &ellipsis);
  if (status != JINJA_CMETA_OK) return status;
  if (jinja_concat_write("<a href=\"", 9u, provider) != 0) return provider->shared.status;
  if (kind == JINJA_CMETA_URLIZE_WWW && jinja_concat_write("https://", 8u, provider) != 0)
    return provider->shared.status;
  if (kind == JINJA_CMETA_URLIZE_EMAIL && jinja_concat_write("mailto:", mailto_size, provider) != 0)
    return provider->shared.status;
  if (jinja_html_write(word.data, word.len, jinja_concat_write, provider) != 0) return provider->shared.status;
  if (jinja_concat_write("\"", 1u, provider) != 0) return provider->shared.status;
  if (kind != JINJA_CMETA_URLIZE_MAILTO && kind != JINJA_CMETA_URLIZE_EMAIL &&
      (nofollow || (rel != NULL && rel->string.len != 0u))) {
    const int has_nofollow = rel != NULL && jinja_urlize_rel_has_nofollow(rel->string);
    if (jinja_concat_write(" rel=\"", 6u, provider) != 0) return provider->shared.status;
    if (nofollow && !has_nofollow && jinja_concat_write("nofollow", 8u, provider) != 0)
      return provider->shared.status;
    if (rel != NULL && rel->string.len != 0u) {
      if (nofollow && !has_nofollow && jinja_concat_write(" ", 1u, provider) != 0)
        return provider->shared.status;
      if (jinja_html_write(rel->string.data, rel->string.len, jinja_concat_write, provider) != 0)
        return provider->shared.status;
    }
    if (jinja_concat_write("\"", 1u, provider) != 0) return provider->shared.status;
  }
  if (kind != JINJA_CMETA_URLIZE_MAILTO && kind != JINJA_CMETA_URLIZE_EMAIL &&
      target != NULL && target->string.len != 0u) {
    if (jinja_concat_write(" target=\"", 9u, provider) != 0 ||
        jinja_html_write(target->string.data, target->string.len, jinja_concat_write, provider) != 0 ||
        jinja_concat_write("\"", 1u, provider) != 0)
      return provider->shared.status;
  }
  if (jinja_concat_write(">", 1u, provider) != 0) return provider->shared.status;
  status = jinja_urlize_write_text(provider, display, input_safe);
  if (status != JINJA_CMETA_OK) return status;
  if (ellipsis && jinja_concat_write("...", 3u, provider) != 0) return provider->shared.status;
  if (jinja_concat_write("</a>", 4u, provider) != 0) return provider->shared.status;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_urlize_word(JINJA_CMETA_PROVIDER *provider,
    vstr word, int input_safe, int has_limit, int64_t limit, int nofollow,
    const JINJA_CMETA_VALUE *target, const JINJA_CMETA_VALUE *rel,
    const JINJA_CMETA_VALUE *extra_schemes, size_t extra_count) {
  size_t begin = 0u;
  size_t end = word.len;
  while (begin < end && (word.data[begin] == '(' || word.data[begin] == '<')) ++begin;
  while (end > begin && (word.data[end - 1u] == ')' || word.data[end - 1u] == '>' ||
                         word.data[end - 1u] == '.' || word.data[end - 1u] == ',')) --end;
  for (size_t pair = 0u; pair < 2u; ++pair) {
    const char opening = pair == 0u ? '(' : '<';
    const char closing = pair == 0u ? ')' : '>';
    size_t opening_count = 0u;
    size_t closing_count = 0u;
    for (size_t i = begin; i < end; ++i) {
      opening_count += word.data[i] == opening;
      closing_count += word.data[i] == closing;
    }
    while (opening_count > closing_count && end < word.len && word.data[end] == closing) {
      ++end;
      ++closing_count;
    }
  }
  JINJA_CMETA_STATUS status = jinja_urlize_write_text(provider,
      vstr_from_buf(word.data, begin), input_safe);
  if (status != JINJA_CMETA_OK) return status;
  const vstr middle = vstr_from_buf(word.data + begin, end - begin);
  const JINJA_CMETA_URLIZE_KIND kind = jinja_urlize_kind(middle, extra_schemes, extra_count);
  status = kind == JINJA_CMETA_URLIZE_NONE
      ? jinja_urlize_write_text(provider, middle, input_safe)
      : jinja_urlize_link(provider, middle, kind, input_safe, has_limit, limit, nofollow, target, rel);
  if (status != JINJA_CMETA_OK) return status;
  return jinja_urlize_write_text(provider, vstr_from_buf(word.data + end, word.len - end), input_safe);
}

static JINJA_CMETA_STATUS jinja_urlize_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *trim_value,
    const JINJA_CMETA_VALUE *nofollow_value, const JINJA_CMETA_VALUE *target_value,
    const JINJA_CMETA_VALUE *rel_value, const JINJA_CMETA_VALUE *extra_value,
    JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE string, target = {0}, rel = {0}, extras = {0};
  int has_limit = 0;
  int64_t limit = 0;
  int nofollow = 0;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  if (vstr_utf8_invalid_offset(string.string) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  if (trim_value != NULL && trim_value->kind != JINJA_CMETA_VALUE_NONE) {
    int valid;
    status = jinja_lookup_integer_key(provider, trim_value, &limit, &valid);
    if (status != JINJA_CMETA_OK || !valid) return status != JINJA_CMETA_OK ? status : JINJA_CMETA_ERR_RENDER;
    has_limit = 1;
  }
  if (nofollow_value != NULL) {
    status = jinja_value_truthy(provider, nofollow_value, &nofollow);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (target_value != NULL && target_value->kind != JINJA_CMETA_VALUE_NONE) {
    target = *target_value;
    status = jinja_string_value(provider, &target, &target);
    if (status != JINJA_CMETA_OK || vstr_utf8_invalid_offset(target.string) != SIZE_MAX)
      return status != JINJA_CMETA_OK ? status : JINJA_CMETA_ERR_METADATA;
  }
  if (rel_value != NULL && rel_value->kind != JINJA_CMETA_VALUE_NONE) {
    rel = *rel_value;
    status = jinja_string_value(provider, &rel, &rel);
    if (status != JINJA_CMETA_OK || vstr_utf8_invalid_offset(rel.string) != SIZE_MAX)
      return status != JINJA_CMETA_OK ? status : JINJA_CMETA_ERR_METADATA;
  }
  if (extra_value != NULL && extra_value->kind != JINJA_CMETA_VALUE_NONE) {
    JINJA_CMETA_VALUE source = *extra_value;
    status = jinja_materialize_list(provider, &source, &extras);
    if (status != JINJA_CMETA_OK) return status;
    for (size_t i = 0u; i < extras.collection_item_count; ++i) {
      if (!jinja_value_is_string(&extras.collection_values[i]) ||
          vstr_utf8_invalid_offset(extras.collection_values[i].string) != SIZE_MAX ||
          !jinja_urlize_extra_scheme(extras.collection_values[i].string))
        return JINJA_CMETA_ERR_RENDER;
    }
  }
  const size_t start = provider->shared.slice_byte_count;
  size_t cursor = 0u;
  while (cursor < string.string.len) {
    const size_t begin = cursor;
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
      return JINJA_CMETA_ERR_METADATA;
    const int space = jinja_striptags_space(scalar);
    while (cursor < string.string.len) {
      const size_t next = cursor;
      if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
        return JINJA_CMETA_ERR_METADATA;
      if (jinja_striptags_space(scalar) != space) {
        cursor = next;
        break;
      }
    }
    const vstr part = vstr_from_buf(string.string.data + begin, cursor - begin);
    status = space ? jinja_urlize_write_text(provider, part, string.string_safe)
        : jinja_urlize_word(provider, part, string.string_safe, has_limit, limit, nofollow,
            target_value != NULL && target_value->kind != JINJA_CMETA_VALUE_NONE ? &target : NULL,
            rel_value != NULL && rel_value->kind != JINJA_CMETA_VALUE_NONE ? &rel : NULL,
            extras.collection_values, extras.collection_item_count);
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = provider->autoescape,
      .string = vstr_from_buf(provider->shared.slice_bytes + start,
          provider->shared.slice_byte_count - start)};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_attr_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *name, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_SCALAR scalar = {0};
  JINJA_CMETA_VALUE key;
  JINJA_CMETA_STATUS status;
  if (!jinja_value_is_string(name)) return JINJA_CMETA_ERR_RENDER;
  status = jinja_scalar_from_value(provider, name, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  if (scalar.size > provider->shared.max_string_bytes) return JINJA_CMETA_ERR_CAPACITY;
  key = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string = vstr_from_buf((const char *)scalar.data, scalar.size)};
  if (vstr_utf8_invalid_offset(key.string) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  jinja_normalize_call_argument(operand);
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (operand->kind == JINJA_CMETA_VALUE_UNDEFINED) return JINJA_CMETA_ERR_RENDER;
  if (operand->kind == JINJA_CMETA_VALUE_LOOP ||
      operand->kind == JINJA_CMETA_VALUE_NAMESPACE || operand->kind == JINJA_CMETA_VALUE_RANGE ||
      operand->kind == JINJA_CMETA_VALUE_CYCLER || operand->kind == JINJA_CMETA_VALUE_JOINER ||
      (operand->kind == JINJA_CMETA_VALUE_TUPLE && operand->identity.source == &jinja_groupby_identity) ||
      (operand->kind == JINJA_CMETA_VALUE_CALLABLE && (operand->callable_kind == JINJA_CMETA_EXPRESSION_MACRO ||
          operand->callable_kind == JINJA_CMETA_EXPRESSION_BLOCK)))
    return jinja_lookup_item(provider, operand, &key, 0u, result);
  if (operand->kind == JINJA_CMETA_VALUE_NODE) {
    if (operand->node.object == NULL || !cmeta_data_desc_valid(operand->node.desc))
      return JINJA_CMETA_ERR_METADATA;
    /* Sequence descriptors are host adapters, not objects exposing their C storage fields. */
    if (!jinja_is_sequence_desc(operand->node.desc) && operand->node.desc->kind == CMETA_DATA_STRUCT)
      return jinja_lookup_struct_item(&operand->node, key.string, result);
  }
  return JINJA_CMETA_OK;
}

/* Runtime filter arguments share compiled string constants in the reference
 * backend. Keep that provenance check local: constant-folded sameas expressions
 * are evaluated before that sharing. O(expression_count + string bytes), O(1). */
static int jinja_replace_same_string(const JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *left, const JINJA_CMETA_VALUE *right) {
  if (!vstr_eq(left->string, right->string)) return 0;
  if (left->string.len == 0u) return 1;
  /* Converting Markup arguments to plain str creates independent objects. */
  if (left->string_safe || right->string_safe) return 0;
  if ((left->identity.serial != 0u && left->identity.serial == right->identity.serial) ||
      (left->identity.source != NULL && left->identity.source == right->identity.source &&
       left->identity.type == right->identity.type)) return 1;
  if (left->identity.source == NULL || right->identity.source == NULL ||
      left->identity.type != NULL || right->identity.type != NULL) return 0;
  int left_constant = 0, right_constant = 0;
  for (size_t i = 0u; i < provider->instance->templ->expression_count; ++i) {
    const JINJA_CMETA_EXPRESSION_NODE *expression = &provider->instance->templ->expressions[i];
    if (expression->kind != JINJA_CMETA_EXPRESSION_STRING) continue;
    if (left->identity.source == expression) left_constant = 1;
    if (right->identity.source == expression) right_constant = 1;
    if (left_constant && right_constant) return 1;
  }
  return 0;
}

static JINJA_CMETA_STATUS jinja_replace_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *old, JINJA_CMETA_VALUE *replacement,
    const JINJA_CMETA_VALUE *count, JINJA_CMETA_VALUE *result) {
  enum { INPUT = 0, OLD = 1, NEW = 2, STRING_COUNT = 3 };
  JINJA_CMETA_VALUE *inputs[STRING_COUNT] = {operand, old, replacement};
  JINJA_CMETA_VALUE strings[STRING_COUNT];
  JINJA_CMETA_STATUS status;
  int64_t limit = -1;
  int valid;
  for (size_t i = 0u; i < STRING_COUNT; ++i) {
    status = jinja_string_value(provider, inputs[i], &strings[i]);
    if (status != JINJA_CMETA_OK) return status;
    if (strings[i].string.len > provider->shared.max_string_bytes) return JINJA_CMETA_ERR_CAPACITY;
    if (vstr_utf8_invalid_offset(strings[i].string) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  }
  const int input_was_safe = strings[INPUT].string_safe;
  if (!provider->autoescape) strings[INPUT].string_safe = 0;
  else {
    if (strings[OLD].string_safe || (strings[NEW].string_safe && !strings[INPUT].string_safe)) {
      status = jinja_safety_value(provider, &strings[INPUT], JINJA_CMETA_EXPRESSION_ESCAPE, &strings[INPUT]);
      if (status != JINJA_CMETA_OK) return status;
    }
    /* Markup.replace escapes only the replacement; old remains the literal search key. */
    if (strings[INPUT].string_safe) {
      status = jinja_safety_value(provider, &strings[NEW], JINJA_CMETA_EXPRESSION_ESCAPE, &strings[NEW]);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  if (count != NULL && count->kind != JINJA_CMETA_VALUE_NONE) {
    status = jinja_lookup_integer_key(provider, count, &limit, &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid) return JINJA_CMETA_ERR_RENDER;
  }
  /* Both str(Markup) and Markup.replace create an object, even for a no-op. */
  if (input_was_safe || strings[INPUT].string_safe)
    strings[INPUT].identity = (JINJA_CMETA_IDENTITY){0};
  if (limit == 0 || jinja_replace_same_string(provider, &strings[OLD], &strings[NEW])) {
    *result = strings[INPUT];
    return JINJA_CMETA_OK;
  }
  const vstr source = strings[INPUT].string;
  const vstr needle = strings[OLD].string;
  const vstr insert = strings[NEW].string;
  const size_t start = provider->shared.slice_byte_count;
  size_t cursor = 0u;
  /* Nonempty matches consume their full byte span and never overlap. Empty matches
     advance by UTF-8 scalar, including one final boundary. O(N*M + output bytes)
     worst case for substring search, O(1) scratch; all bytes share the render cap. */
  while (limit != 0) {
    size_t match = cursor;
    if (needle.len != 0u) {
      if (cursor == source.len) break;
      size_t found = vstr_find(vstr_from_buf(source.data + cursor, source.len - cursor), needle);
      if (found == SIZE_MAX) break;
      match += found;
    }
    if (match != cursor && jinja_concat_write(source.data + cursor, match - cursor, provider) != 0)
      return provider->shared.status;
    if (jinja_concat_write(insert.data, insert.len, provider) != 0) return provider->shared.status;
    if (limit > 0) --limit;
    cursor = match + needle.len;
    if (needle.len == 0u) {
      if (cursor == source.len) break;
      salts_unicode_scalar scalar;
      if (salts_unicode_utf8_next(source, &cursor, &scalar) != SALTS_UNICODE_OK)
        return JINJA_CMETA_ERR_METADATA;
      if (jinja_concat_write(source.data + scalar.byte_offset, scalar.byte_length, provider) != 0)
        return provider->shared.status;
    }
  }
  if (cursor == 0u && needle.len != 0u) {
    *result = strings[INPUT];
    return JINJA_CMETA_OK;
  }
  if (cursor < source.len && jinja_concat_write(source.data + cursor, source.len - cursor, provider) != 0)
    return provider->shared.status;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = strings[INPUT].string_safe,
      .string = provider->shared.slice_byte_count == start ? vstr_from_cstr("")
          : vstr_from_buf(provider->shared.slice_bytes + start, provider->shared.slice_byte_count - start)};
  return JINJA_CMETA_OK;
}

/* The temporary ASCII spelling belongs to this conversion, not the render's
 * retained string arena. Unicode decimal digits shrink to one byte each. */
typedef struct JINJA_CMETA_NUMERIC_TEXT {
  char *data;
  size_t len;
} JINJA_CMETA_NUMERIC_TEXT;

static JINJA_CMETA_STATUS jinja_numeric_text(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_SCALAR *scalar, JINJA_CMETA_NUMERIC_TEXT *text) {
  vstr trimmed;
  *text = (JINJA_CMETA_NUMERIC_TEXT){0};
  if (!jinja_cmeta_string_append_fits(0u, scalar->size, provider->shared.max_string_bytes))
    return JINJA_CMETA_ERR_CAPACITY;
  if (salts_unicode_trim_whitespace(vstr_from_buf((const char *)scalar->data, scalar->size),
                                   &trimmed) != SALTS_UNICODE_OK)
    return JINJA_CMETA_ERR_METADATA;
  char *buffer = (char *)jinja_provider_allocate(provider, trimmed.len + 1u, sizeof(char));
  if (buffer == NULL) return provider->shared.status;
  size_t cursor = 0u, length = 0u;
  salts_unicode_scalar character;
  while (salts_unicode_utf8_next(trimmed, &cursor, &character) == SALTS_UNICODE_OK) {
    uint32_t digit;
    if (character.value < 128u) {
      char c = (char)character.value;
      buffer[length++] = c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
    } else if (salts_unicode_decimal_value(character.value, &digit) == SALTS_UNICODE_OK) {
      buffer[length++] = (char)('0' + digit);
    } else {
      jinja_cmeta_memory_drop(buffer);
      return JINJA_CMETA_OK;
    }
  }
  /* trim_whitespace already validated the immutable UTF-8 input. */
  /* The admitted buffer has room for the shortened spelling and its NUL. */
  buffer[length] = '\0';
  *text = (JINJA_CMETA_NUMERIC_TEXT){.data = buffer, .len = length};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_integer_text(const char *text, size_t size, int64_t base,
    int64_t *result, int *valid) {
  enum { BINARY_BASE = 2, OCTAL_BASE = 8, HEX_BASE = 16, MAX_BASE = 36 };
  *valid = 0;
  if (size == 0u || (base != 0 && (base < BINARY_BASE || base > MAX_BASE))) return JINJA_CMETA_OK;
  size_t cursor = text[0] == '-' || text[0] == '+' ? 1u : 0u;
  const int negative = text[0] == '-';
  int leading_zero = base == 0 && cursor < size && text[cursor] == '0';
  if (cursor + 1u < size && text[cursor] == '0') {
    char prefix = text[cursor + 1u];
    int prefix_base = prefix == 'b' ? BINARY_BASE : prefix == 'o' ? OCTAL_BASE :
        prefix == 'x' ? HEX_BASE : 0;
    if (prefix_base != 0 && (base == 0 || base == prefix_base)) {
      base = prefix_base;
      leading_zero = 0;
      cursor += 2u;
      if (cursor < size && text[cursor] == '_') ++cursor;
    }
  }
  if (base == 0) base = JINJA_CMETA_DECIMAL_RADIX;
  uint64_t magnitude = 0u;
  const uint64_t limit = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
  int previous_digit = 0, nonzero = 0, overflow = 0;
  for (; cursor < size; ++cursor) {
    char c = text[cursor];
    if (c == '_' && previous_digit) { previous_digit = 0; continue; }
    int digit = c >= '0' && c <= '9' ? c - '0' :
        c >= 'a' && c <= 'z' ? c - 'a' + JINJA_CMETA_DECIMAL_RADIX : MAX_BASE;
    if (digit >= base) return JINJA_CMETA_OK;
    previous_digit = 1;
    nonzero |= digit != 0;
    if (magnitude > (limit - (unsigned)digit) / (unsigned)base) overflow = 1;
    if (!overflow) magnitude = magnitude * (unsigned)base + (unsigned)digit;
  }
  if (!previous_digit || (leading_zero && nonzero)) return JINJA_CMETA_OK;
  if (overflow) return JINJA_CMETA_ERR_CAPACITY;
  *result = negative ? -(int64_t)(magnitude - (magnitude != 0u)) - (magnitude != 0u)
                     : (int64_t)magnitude;
  *valid = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_float_to_integer(double number, JINJA_CMETA_VALUE *result) {
  if (!isfinite(number)) return JINJA_CMETA_ERR_RENDER;
  if (number < (double)INT64_MIN || number >= -(double)INT64_MIN)
    return JINJA_CMETA_ERR_CAPACITY;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)number};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_convert_number(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_EXPRESSION_KIND kind,
    const JINJA_CMETA_VALUE *default_value, const JINJA_CMETA_VALUE *base_value,
    JINJA_CMETA_VALUE *result) {
  const int integer = kind == JINJA_CMETA_EXPRESSION_TO_INT;
  *result = default_value != NULL ? *default_value : (JINJA_CMETA_VALUE){
      .kind = integer ? JINJA_CMETA_VALUE_INTEGER : JINJA_CMETA_VALUE_FLOAT};
  jinja_normalize_call_argument(operand);
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, operand, &scalar);
  /* Containers and other nonnumeric objects use the filter's default. */
  if (status == JINJA_CMETA_ERR_RENDER) return JINJA_CMETA_OK;
  if (status != JINJA_CMETA_OK) return status;
  if (scalar.kind == JINJA_CMETA_SCALAR_UNDEFINED) return JINJA_CMETA_ERR_RENDER;
  if (scalar.kind == JINJA_CMETA_SCALAR_NONE) return JINJA_CMETA_OK;
  double number;
  if (scalar.kind == JINJA_CMETA_SCALAR_STRING) {
    JINJA_CMETA_NUMERIC_TEXT text;
    status = jinja_numeric_text(provider, &scalar, &text);
    if (status != JINJA_CMETA_OK || text.data == NULL) return status;
    if (integer) {
      int64_t base = JINJA_CMETA_DECIMAL_RADIX, converted = 0;
      int valid = 1;
      if (base_value != NULL)
        status = jinja_lookup_integer_key(provider, base_value, &base, &valid);
      if (status == JINJA_CMETA_OK)
        status = jinja_integer_text(text.data, text.len, valid ? base : -1, &converted, &valid);
      if (status != JINJA_CMETA_OK || valid) {
        jinja_cmeta_memory_drop(text.data);
        if (status == JINJA_CMETA_OK)
          *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = converted};
        return status;
      }
    }
    /* Jinja int("42.23") explicitly converts decimal text through float. */
    int converted = jinja_float_parse_text(text.data, text.len, &number);
    jinja_cmeta_memory_drop(text.data);
    if (!converted || (integer && !isfinite(number))) return JINJA_CMETA_OK;
  } else if (scalar.kind == JINJA_CMETA_SCALAR_FLOAT) {
    number = scalar.floating;
    if (integer && isnan(number)) return JINJA_CMETA_OK;
  } else {
    int64_t integral = scalar.kind == JINJA_CMETA_SCALAR_BOOL ? scalar.boolean != 0 :
        scalar.kind == JINJA_CMETA_SCALAR_SINT ? scalar.sint : 0;
    if (integer) {
      if (scalar.kind == JINJA_CMETA_SCALAR_UINT) {
        if (scalar.uint > (uint64_t)INT64_MAX) return JINJA_CMETA_ERR_CAPACITY;
        integral = (int64_t)scalar.uint;
      }
      *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = integral};
      return JINJA_CMETA_OK;
    }
    number = scalar.kind == JINJA_CMETA_SCALAR_UINT ? (double)scalar.uint : (double)integral;
  }
  if (integer) return jinja_float_to_integer(number, result);
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_FLOAT, .floating = number};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_round_integer(int64_t number, int64_t precision,
    JINJA_CMETA_VALUE *result) {
  enum { INT64_DECIMAL_DIGITS = 19 };
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = number};
  if (precision >= 0) return JINJA_CMETA_OK;
  if (precision < -INT64_DECIMAL_DIGITS) { result->integer = 0; return JINJA_CMETA_OK; }
  uint64_t scale = 1u;
  for (int64_t i = 0; i > precision; --i) scale *= JINJA_CMETA_DECIMAL_RADIX;
  const uint64_t magnitude = jinja_integer_magnitude(number);
  uint64_t quotient = magnitude / scale, remainder = magnitude % scale;
  if (remainder > scale / 2u || (remainder == scale / 2u && quotient % 2u != 0u)) ++quotient;
  const uint64_t limit = (uint64_t)INT64_MAX + (number < 0 ? 1u : 0u);
  if (quotient > limit / scale) return JINJA_CMETA_ERR_CAPACITY;
  uint64_t rounded = quotient * scale;
  result->integer = number < 0 ? -(int64_t)(rounded - (rounded != 0u)) - (rounded != 0u)
                               : (int64_t)rounded;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_round_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *precision_value,
    const JINJA_CMETA_VALUE *method_value, JINJA_CMETA_VALUE *result) {
  enum { COMMON, CEILING, FLOOR } method = COMMON;
  if (method_value != NULL) {
    JINJA_CMETA_SCALAR scalar;
    JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, method_value, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    if (scalar.kind != JINJA_CMETA_SCALAR_STRING) return JINJA_CMETA_ERR_RENDER;
    const char *name = (const char *)scalar.data;
    if (jinja_name_equal(name, scalar.size, "ceil")) method = CEILING;
    else if (jinja_name_equal(name, scalar.size, "floor")) method = FLOOR;
    else if (!jinja_name_equal(name, scalar.size, "common")) return JINJA_CMETA_ERR_RENDER;
  }
  JINJA_CMETA_NUMBER number;
  JINJA_CMETA_STATUS status = jinja_number_from_value(provider, operand, &number);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_VALUE precision = precision_value != NULL ? *precision_value :
      (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER};
  jinja_normalize_call_argument(&precision);
  const double input = number.kind == JINJA_CMETA_NUMBER_INTEGER ? (double)number.integer : number.floating;
  double rounded;
  if (method == COMMON) {
    int64_t places = 0;
    if (precision.kind != JINJA_CMETA_VALUE_NONE) {
      int valid;
      status = jinja_lookup_integer_key(provider, &precision, &places, &valid);
      if (status != JINJA_CMETA_OK) return status;
      if (!valid) return JINJA_CMETA_ERR_RENDER;
    }
    if (number.kind == JINJA_CMETA_NUMBER_INTEGER)
      return jinja_round_integer(number.integer, places, result);
    if (!jinja_float_round(input, places, &rounded)) return JINJA_CMETA_ERR_RENDER;
    if (precision.kind == JINJA_CMETA_VALUE_NONE) return jinja_float_to_integer(rounded, result);
  } else {
    JINJA_CMETA_NUMBER places;
    status = jinja_number_from_value(provider, &precision, &places);
    if (status != JINJA_CMETA_OK) return status;
    if (number.kind == JINJA_CMETA_NUMBER_INTEGER && places.kind == JINJA_CMETA_NUMBER_INTEGER &&
        places.integer >= 0) rounded = input;
    else {
      double scale = pow((double)JINJA_CMETA_DECIMAL_RADIX,
          places.kind == JINJA_CMETA_NUMBER_INTEGER ? (double)places.integer : places.floating);
      double scaled = input * scale;
      if (scale == 0.0 || !isfinite(scale) || !isfinite(scaled)) return JINJA_CMETA_ERR_RENDER;
      double integral = method == CEILING ? ceil(scaled) : floor(scaled);
      rounded = integral == 0.0 ? 0.0 : integral / scale;
    }
  }
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_FLOAT, .floating = rounded};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_filesize_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *binary_value, JINJA_CMETA_VALUE *result) {
  enum { DECIMAL_BASE = 1000, BINARY_BASE = 1024, FRACTION_DIGITS = 1 };
  static const struct { const char *decimal, *binary; double limit; int rounded_down; } prefixes[] = {
      {"kB", "KiB", 1e6, 0}, {"MB", "MiB", 1e9, 0}, {"GB", "GiB", 1e12, 0}, {"TB", "TiB", 1e15, 0},
      {"PB", "PiB", 1e18, 0}, {"EB", "EiB", 1e21, 0}, {"ZB", "ZiB", 1e24, 1}, {"YB", "YiB", 1e27, 0}};
  const JINJA_CMETA_VALUE invalid = {.kind = JINJA_CMETA_VALUE_NONE};
  JINJA_CMETA_VALUE converted;
  JINJA_CMETA_STATUS status = jinja_convert_number(provider, operand,
      JINJA_CMETA_EXPRESSION_TO_FLOAT, &invalid, NULL, &converted);
  if (status != JINJA_CMETA_OK) return status;
  if (converted.kind != JINJA_CMETA_VALUE_FLOAT) return JINJA_CMETA_ERR_RENDER;
  int binary = 0;
  if (binary_value != NULL) {
    status = jinja_value_truthy(provider, binary_value, &binary);
    if (status != JINJA_CMETA_OK) return status;
  }
  const double bytes = converted.floating;
  const double base = binary ? BINARY_BASE : DECIMAL_BASE;
  const char *suffix = "Bytes";
  double number = bytes;
  int precision = 0;
  if (bytes == 1.0) suffix = "Byte";
  else if (bytes < base) {
    if (!isfinite(bytes)) return JINJA_CMETA_ERR_RENDER;
    number = trunc(bytes);
    if (number == 0.0) number = 0.0;
  } else {
    double unit = base * base;
    for (size_t i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
      if (!binary) unit = prefixes[i].limit;
      suffix = binary ? prefixes[i].binary : prefixes[i].decimal;
      /* Compare against the exact integer cutoff; division still uses its nearest double. */
      const double cutoff = !binary && prefixes[i].rounded_down ? nextafter(unit, INFINITY) : unit;
      if (bytes < cutoff || i + 1u == sizeof(prefixes) / sizeof(prefixes[0])) break;
      if (binary) unit *= base;
    }
    number = base * bytes / unit;
    precision = FRACTION_DIGITS;
  }
  char text[JINJA_FLOAT_TOKEN_CAPACITY];
  size_t size;
  if (!jinja_float_format_fixed(number, precision, text, sizeof(text), &size)) return JINJA_CMETA_ERR_RENDER;
  const size_t offset = provider->shared.slice_byte_count;
  if (jinja_concat_write(text, size, provider) != 0 || jinja_concat_write(" ", 1u, provider) != 0 ||
      jinja_concat_write(suffix, strlen(suffix), provider) != 0) return provider->shared.status;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string = vstr_from_buf(provider->shared.slice_bytes + offset, provider->shared.slice_byte_count - offset)};
  return JINJA_CMETA_OK;
}

enum { JINJA_FILTER_FIRST, JINJA_FILTER_SECOND, JINJA_FILTER_THIRD, JINJA_FILTER_FOURTH,
       JINJA_FILTER_FIFTH,
       JINJA_FILTER_MAX_PARAMETERS };

static JINJA_CMETA_STATUS jinja_bind_filter_arguments(JINJA_CMETA_EXPRESSION_KIND kind,
    const JINJA_CMETA_CALL_INPUT *input, size_t slots[JINJA_FILTER_MAX_PARAMETERS]) {
  static const struct {
    JINJA_CMETA_EXPRESSION_KIND kind;
    size_t count;
    const char *names[JINJA_FILTER_MAX_PARAMETERS];
  } signatures[] = {
    {JINJA_CMETA_EXPRESSION_DEFAULT, 2u, {"default_value", "boolean"}},
    {JINJA_CMETA_EXPRESSION_TO_INT, 2u, {"default", "base"}},
    {JINJA_CMETA_EXPRESSION_TO_FLOAT, 1u, {"default"}},
    {JINJA_CMETA_EXPRESSION_ROUND, 2u, {"precision", "method"}},
    {JINJA_CMETA_EXPRESSION_FILESIZEFORMAT, 1u, {"binary"}},
    {JINJA_CMETA_EXPRESSION_XMLATTR, 1u, {"autospace"}},
    {JINJA_CMETA_EXPRESSION_WORDWRAP, 4u, {"width", "break_long_words", "wrapstring", "break_on_hyphens"}},
    {JINJA_CMETA_EXPRESSION_TRIM, 1u, {"chars"}},
    {JINJA_CMETA_EXPRESSION_CENTER, 1u, {"width"}},
    {JINJA_CMETA_EXPRESSION_INDENT, 3u, {"width", "first", "blank"}},
    {JINJA_CMETA_EXPRESSION_TRUNCATE, 4u, {"length", "killwords", "end", "leeway"}},
    {JINJA_CMETA_EXPRESSION_BATCH, 2u, {"linecount", "fill_with"}},
    {JINJA_CMETA_EXPRESSION_SLICE_FILTER, 2u, {"slices", "fill_with"}},
    {JINJA_CMETA_EXPRESSION_SUM, 2u, {"attribute", "start"}},
    {JINJA_CMETA_EXPRESSION_MIN, 2u, {"case_sensitive", "attribute"}},
    {JINJA_CMETA_EXPRESSION_MAX, 2u, {"case_sensitive", "attribute"}},
    {JINJA_CMETA_EXPRESSION_SORT, 3u, {"reverse", "case_sensitive", "attribute"}},
    {JINJA_CMETA_EXPRESSION_DICTSORT, 3u, {"case_sensitive", "by", "reverse"}},
    {JINJA_CMETA_EXPRESSION_UNIQUE, 2u, {"case_sensitive", "attribute"}},
    {JINJA_CMETA_EXPRESSION_GROUPBY, 3u, {"attribute", "default", "case_sensitive"}},
    {JINJA_CMETA_EXPRESSION_URLIZE, 5u,
        {"trim_url_limit", "nofollow", "target", "rel", "extra_schemes"}},
    {JINJA_CMETA_EXPRESSION_TOJSON, 1u, {"indent"}},
    {JINJA_CMETA_EXPRESSION_JOIN, 2u, {"d", "attribute"}},
    {JINJA_CMETA_EXPRESSION_ATTR, 1u, {"name"}},
    {JINJA_CMETA_EXPRESSION_REPLACE, 3u, {"old", "new", "count"}}
  };
  size_t signature = SIZE_MAX, count = 0u;
  for (size_t i = 0u; i < JINJA_FILTER_MAX_PARAMETERS; ++i) slots[i] = SIZE_MAX;
  for (size_t i = 0u; i < sizeof(signatures) / sizeof(signatures[0]); ++i)
    if (signatures[i].kind == kind) { signature = i; count = signatures[i].count; break; }
  if (input->count > count) return JINJA_CMETA_ERR_RENDER;
  for (size_t i = 0u; i < input->count; ++i) {
    size_t slot = i < input->positional ? i : SIZE_MAX;
    if (slot == SIZE_MAX) {
      for (size_t j = 0u; j < count; ++j)
        if (jinja_name_equal(input->keywords[i].data, input->keywords[i].len, signatures[signature].names[j])) {
          slot = j;
          break;
        }
    }
    if (slot == SIZE_MAX || slots[slot] != SIZE_MAX) return JINJA_CMETA_ERR_RENDER;
    slots[slot] = i;
  }
  return JINJA_CMETA_OK;
}

/* Arguments leave the LIFO call workspace at construction; aliases share the
 * render-owned transform and its input cursor until the render is cleaned up. */
static JINJA_CMETA_STATUS jinja_transform_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, JINJA_CMETA_EXPRESSION_KIND kind, JINJA_CMETA_VALUE *operand,
    const JINJA_CMETA_CALL_INPUT *input, JINJA_CMETA_VALUE *result) {
  const size_t offset = provider->shared.collection_value_count;
  if (provider->shared.transform_count >= provider->shared.node_capacity ||
      input->count >= provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_STATUS status = jinja_ensure_collection_storage(provider, input->count + 1u);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_TRANSFORM *transform = (JINJA_CMETA_TRANSFORM *)jinja_provider_zero(provider, 1u, sizeof(*transform));
  if (transform == NULL) return provider->shared.status;
  transform->source.input = *operand;
  jinja_normalize_call_argument(&transform->source.input);
  transform->source.max_items = provider->shared.node_capacity;
  transform->context = context;
  transform->kind = kind;
  transform->call = *input;
  transform->call.values = jinja_cmeta_values_at(&provider->shared.values, offset);
  if (input->count != 0u)
    memcpy(transform->call.values, input->values, input->count * sizeof(*input->values));
  provider->shared.collection_value_count += input->count;
  transform->next = provider->shared.transforms;
  provider->shared.transforms = transform;
  ++provider->shared.transform_count;
  status = jinja_create_iterator(provider, operand, JINJA_CMETA_ITERATOR_TRANSFORM, result);
  if (status == JINJA_CMETA_OK) result->iterator->transform = transform;
  return status;
}

static JINJA_CMETA_STATUS jinja_apply_filter(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, JINJA_CMETA_EXPRESSION_KIND kind, vstr name,
    JINJA_CMETA_VALUE *operand,
    const JINJA_CMETA_CALL_INPUT *input, JINJA_CMETA_VALUE *value) {
  if (kind == JINJA_CMETA_EXPRESSION_HOST_FILTER) {
    const JINJA_CMETA_CALLABLE *callable = jinja_cmeta_env_find_filter(
        provider->instance->templ->env, name);
    if (callable == NULL) return JINJA_CMETA_ERR_RENDER;
    return jinja_invoke_host_function(provider, callable, input, operand, value);
  }
  if (kind == JINJA_CMETA_EXPRESSION_FORMAT) {
    if (input->positional != 0u && input->positional != input->count) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_VALUE arguments = {.kind = JINJA_CMETA_VALUE_TUPLE,
        .collection_values = input->values, .collection_item_count = input->positional};
    if (input->count != input->positional) {
      JINJA_CMETA_STATUS status = jinja_build_mapping(provider, input->keywords, input->count,
          JINJA_CMETA_EXPRESSION_DICT_CALL, 0u, &arguments, input->values);
      if (status != JINJA_CMETA_OK) return status;
    }
    return jinja_format_value(provider, operand, &arguments, value);
  }
  if (kind == JINJA_CMETA_EXPRESSION_MAP || kind == JINJA_CMETA_EXPRESSION_SELECT ||
      kind == JINJA_CMETA_EXPRESSION_REJECT || kind == JINJA_CMETA_EXPRESSION_SELECTATTR ||
      kind == JINJA_CMETA_EXPRESSION_REJECTATTR)
    return jinja_transform_value(provider, context, kind, operand, input, value);
  JINJA_CMETA_VALUE *arguments = input->values;
  size_t slots[JINJA_FILTER_MAX_PARAMETERS];
  JINJA_CMETA_STATUS status = jinja_bind_filter_arguments(kind, input, slots);
  if (status != JINJA_CMETA_OK) return status;
  if (kind == JINJA_CMETA_EXPRESSION_FILESIZEFORMAT) {
    status = jinja_filesize_value(provider, operand,
        slots[JINJA_FILTER_FIRST] == SIZE_MAX ? NULL : &arguments[slots[JINJA_FILTER_FIRST]], value);
  } else if (kind == JINJA_CMETA_EXPRESSION_WORDCOUNT) {
    status = jinja_wordcount_value(provider, operand, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_URLENCODE) {
    status = jinja_urlencode_value(provider, operand, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_XMLATTR) {
    status = jinja_xmlattr_value(provider, operand,
        slots[JINJA_FILTER_FIRST] == SIZE_MAX ? NULL : &arguments[slots[JINJA_FILTER_FIRST]], value);
  } else if (kind == JINJA_CMETA_EXPRESSION_TO_INT || kind == JINJA_CMETA_EXPRESSION_TO_FLOAT ||
      kind == JINJA_CMETA_EXPRESSION_ROUND) {
    const JINJA_CMETA_VALUE *first = slots[JINJA_FILTER_FIRST] == SIZE_MAX ? NULL :
        &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *second = slots[JINJA_FILTER_SECOND] == SIZE_MAX ? NULL :
        &arguments[slots[JINJA_FILTER_SECOND]];
    status = kind == JINJA_CMETA_EXPRESSION_ROUND
        ? jinja_round_value(provider, operand, first, second, value)
        : jinja_convert_number(provider, operand, kind, first, second, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_INDENT || kind == JINJA_CMETA_EXPRESSION_TRUNCATE ||
      kind == JINJA_CMETA_EXPRESSION_WORDWRAP) {
    const JINJA_CMETA_VALUE *parameters[JINJA_FILTER_MAX_PARAMETERS];
    for (size_t i = 0u; i < JINJA_FILTER_MAX_PARAMETERS; ++i)
      parameters[i] = slots[i] == SIZE_MAX ? NULL : &arguments[slots[i]];
    status = kind == JINJA_CMETA_EXPRESSION_INDENT
        ? jinja_indent_value(provider, operand, parameters, value)
        : kind == JINJA_CMETA_EXPRESSION_TRUNCATE
            ? jinja_truncate_value(provider, operand, parameters, value)
            : jinja_wordwrap_value(provider, operand, parameters, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_SLICE_FILTER) {
    if (slots[JINJA_FILTER_FIRST] == SIZE_MAX) return JINJA_CMETA_ERR_RENDER;
    status = jinja_slicer_value(provider, operand, &arguments[slots[JINJA_FILTER_FIRST]],
        slots[JINJA_FILTER_SECOND] == SIZE_MAX ? NULL : &arguments[slots[JINJA_FILTER_SECOND]], value);
  } else if (kind == JINJA_CMETA_EXPRESSION_BATCH) {
    if (slots[JINJA_FILTER_FIRST] == SIZE_MAX) return JINJA_CMETA_ERR_RENDER;
    status = jinja_batch_value(provider, operand, &arguments[slots[JINJA_FILTER_FIRST]],
        slots[JINJA_FILTER_SECOND] == SIZE_MAX ? NULL : &arguments[slots[JINJA_FILTER_SECOND]], value);
  } else if (kind == JINJA_CMETA_EXPRESSION_REPLACE) {
    if (slots[JINJA_FILTER_FIRST] == SIZE_MAX || slots[JINJA_FILTER_SECOND] == SIZE_MAX)
      return JINJA_CMETA_ERR_RENDER;
    const JINJA_CMETA_VALUE *count = slots[JINJA_FILTER_THIRD] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_THIRD]];
    status = jinja_replace_value(provider, operand, &arguments[slots[JINJA_FILTER_FIRST]],
                                &arguments[slots[JINJA_FILTER_SECOND]], count, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_ATTR) {
    if (input->count != 1u)
      return JINJA_CMETA_ERR_RENDER;
    status = jinja_attr_value(provider, operand, &arguments[0], value);
  } else if (kind == JINJA_CMETA_EXPRESSION_JOIN) {
    const JINJA_CMETA_VALUE *delimiter = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *attribute = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    status = jinja_join_value(provider, operand, delimiter, attribute, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_SUM) {
    const JINJA_CMETA_VALUE *attribute = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *start = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    status = jinja_sum_value(provider, operand, attribute, start, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_MIN || kind == JINJA_CMETA_EXPRESSION_MAX) {
    const JINJA_CMETA_VALUE *case_sensitive = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *attribute = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    status = jinja_minmax_value(provider, operand, case_sensitive, attribute,
        kind == JINJA_CMETA_EXPRESSION_MAX, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_SORT) {
    const JINJA_CMETA_VALUE *reverse = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *case_sensitive = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    const JINJA_CMETA_VALUE *attribute = slots[JINJA_FILTER_THIRD] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_THIRD]];
    status = jinja_sort_value(provider, operand, reverse, case_sensitive, attribute, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_DICTSORT) {
    const JINJA_CMETA_VALUE *case_sensitive = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *by = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    const JINJA_CMETA_VALUE *reverse = slots[JINJA_FILTER_THIRD] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_THIRD]];
    status = jinja_dictsort_value(provider, operand, case_sensitive, by, reverse, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_UNIQUE) {
    const JINJA_CMETA_VALUE *case_sensitive = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *attribute = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    status = jinja_unique_value(provider, operand, case_sensitive, attribute, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_GROUPBY) {
    if (slots[JINJA_FILTER_FIRST] == SIZE_MAX) return JINJA_CMETA_ERR_RENDER;
    const JINJA_CMETA_VALUE *attribute = &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *default_value = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    const JINJA_CMETA_VALUE *case_sensitive = slots[JINJA_FILTER_THIRD] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_THIRD]];
    status = jinja_groupby_value(provider, operand, attribute, default_value, case_sensitive, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_PPRINT) {
    status = jinja_pprint_value(provider, operand, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_RANDOM) {
    status = jinja_random_value(provider, operand, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_STRIPTAGS) {
    status = jinja_striptags_value(provider, operand, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_URLIZE) {
    const JINJA_CMETA_VALUE *trim = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    const JINJA_CMETA_VALUE *nofollow = slots[JINJA_FILTER_SECOND] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_SECOND]];
    const JINJA_CMETA_VALUE *target = slots[JINJA_FILTER_THIRD] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_THIRD]];
    const JINJA_CMETA_VALUE *rel = slots[JINJA_FILTER_FOURTH] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FOURTH]];
    const JINJA_CMETA_VALUE *extra = slots[JINJA_FILTER_FIFTH] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIFTH]];
    status = jinja_urlize_value(provider, operand, trim, nofollow, target, rel, extra, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_TOJSON) {
    const JINJA_CMETA_VALUE *indent = slots[JINJA_FILTER_FIRST] == SIZE_MAX
        ? NULL : &arguments[slots[JINJA_FILTER_FIRST]];
    status = jinja_tojson_value(provider, operand, indent, value);
  } else if (kind == JINJA_CMETA_EXPRESSION_TRIM ||
      kind == JINJA_CMETA_EXPRESSION_CENTER) {
    const JINJA_CMETA_VALUE *parameter =
        input->count == 0u ? NULL : &arguments[0];
    status = kind == JINJA_CMETA_EXPRESSION_TRIM
        ? jinja_trim_value(provider, operand, parameter, value)
        : jinja_center_value(provider, operand, parameter, value);
  } else if (kind != JINJA_CMETA_EXPRESSION_DEFAULT) {
    if (kind == JINJA_CMETA_EXPRESSION_CAPITALIZE)
      status = jinja_capitalize_value(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_TITLE)
      status = jinja_title_value(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_LENGTH)
      status = jinja_value_length(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_TO_LIST)
      status = jinja_materialize_list(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_ITEMS)
      status = jinja_create_iterator(provider, operand, JINJA_CMETA_ITERATOR_ITEMS, value);
    else if (kind == JINJA_CMETA_EXPRESSION_REVERSE)
      status = jinja_reverse_value(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_ABS)
      status = jinja_absolute_value(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_TO_STRING)
      status = jinja_string_value(provider, operand, value);
    else if (kind == JINJA_CMETA_EXPRESSION_UPPER || kind == JINJA_CMETA_EXPRESSION_LOWER)
      status = jinja_case_value(provider, operand,
          kind == JINJA_CMETA_EXPRESSION_UPPER ? SALTS_UNICODE_CASE_UPPER : SALTS_UNICODE_CASE_LOWER,
          value);
    else if (kind == JINJA_CMETA_EXPRESSION_SAFE ||
             kind == JINJA_CMETA_EXPRESSION_ESCAPE ||
             kind == JINJA_CMETA_EXPRESSION_FORCEESCAPE)
      status = jinja_safety_value(provider, operand, kind, value);
    else
      status = jinja_sequence_edge(provider, operand,
                                   kind == JINJA_CMETA_EXPRESSION_LAST, value);
  } else {
    int boolean = 0;
    int truthy = 1;
    jinja_normalize_call_argument(operand);
    if (slots[JINJA_FILTER_SECOND] != SIZE_MAX) {
      status = jinja_value_truthy(provider, &arguments[slots[JINJA_FILTER_SECOND]], &boolean);
    }
    if (status == JINJA_CMETA_OK && boolean)
      status = jinja_value_truthy(provider, operand, &truthy);
    if (status != JINJA_CMETA_OK) return status;
    if (operand->kind == JINJA_CMETA_VALUE_UNDEFINED || !truthy)
      *value = slots[JINJA_FILTER_FIRST] != SIZE_MAX
                   ? arguments[slots[JINJA_FILTER_FIRST]]
                   : (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
                                         .string = vstr_from_cstr("")};
    else *value = *operand;
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_evaluate_filter(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t index, size_t depth, JINJA_CMETA_VALUE *value, JINJA_CMETA_VALUE *workspace) {
  JINJA_CMETA_VALUE *operand = workspace;
  JINJA_CMETA_VALUE *arguments = workspace + 1u;
  JINJA_CMETA_STATUS status;
  JINJA_CMETA_CALL_INPUT input = {0};
  if (expression->left_node >= index ||
      expression->first_collection_item > provider->instance->templ->collection_item_count ||
      expression->collection_item_count >
          provider->instance->templ->collection_item_count - expression->first_collection_item)
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, operand);
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_collect_call_input(provider, context, expression, index, depth, arguments, &input);
  if (status != JINJA_CMETA_OK) return status;
  return jinja_apply_filter(provider, context, expression->kind, expression->path,
      operand, &input, value);
}

/* Identity never traverses payloads or advances shared iterators. */
static JINJA_CMETA_STATUS jinja_same_value(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *left, const JINJA_CMETA_VALUE *right, int *result) {
  const int left_bool = left->kind == JINJA_CMETA_VALUE_BOOL ||
      (left->kind == JINJA_CMETA_VALUE_NODE && left->node.desc != NULL && left->node.desc->kind == CMETA_DATA_BOOL);
  const int right_bool = right->kind == JINJA_CMETA_VALUE_BOOL ||
      (right->kind == JINJA_CMETA_VALUE_NODE && right->node.desc != NULL && right->node.desc->kind == CMETA_DATA_BOOL);
  *result = 0;
  if ((left->identity.serial != 0u && left->identity.serial == right->identity.serial) ||
      (left->identity.source != NULL && left->identity.source == right->identity.source &&
       left->identity.type == right->identity.type)) {
    *result = 1;
    return JINJA_CMETA_OK;
  }
  if (left_bool || right_bool) {
    if (left_bool && right_bool) {
      JINJA_CMETA_SCALAR a = {0}, b = {0};
      JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, left, &a);
      if (status == JINJA_CMETA_OK) status = jinja_scalar_from_value(provider, right, &b);
      if (status != JINJA_CMETA_OK) return status;
      *result = a.boolean == b.boolean;
    }
    return JINJA_CMETA_OK;
  }
  /* The pinned CPython oracle shares these integer singleton objects. */
  enum { SMALL_INTEGER_MIN = -5, SMALL_INTEGER_MAX = 256 };
  int64_t left_integer, right_integer;
  int left_valid, right_valid;
  JINJA_CMETA_STATUS status = jinja_lookup_integer_key(provider, left, &left_integer, &left_valid);
  if (status != JINJA_CMETA_OK) return status;
  if (left_valid && left_integer >= SMALL_INTEGER_MIN && left_integer <= SMALL_INTEGER_MAX) {
    status = jinja_lookup_integer_key(provider, right, &right_integer, &right_valid);
    if (status != JINJA_CMETA_OK) return status;
    *result = right_valid && left_integer == right_integer;
    return JINJA_CMETA_OK;
  }
  if (left->kind != right->kind) return JINJA_CMETA_OK;
  switch (left->kind) {
  case JINJA_CMETA_VALUE_NONE:
  case JINJA_CMETA_VALUE_MISSING:
    *result = 1;
    break;
  case JINJA_CMETA_VALUE_NODE:
    *result = left->node.object == right->node.object && left->node.desc == right->node.desc;
    break;
  case JINJA_CMETA_VALUE_RANGE:
    *result = left->range.identity == right->range.identity;
    break;
  case JINJA_CMETA_VALUE_NAMESPACE:
    *result = left->namespace_dict == right->namespace_dict;
    break;
  case JINJA_CMETA_VALUE_CYCLER:
  case JINJA_CMETA_VALUE_JOINER:
    *result = left->helper == right->helper;
    break;
  case JINJA_CMETA_VALUE_ITERATOR:
    *result = left->iterator == right->iterator;
    break;
  case JINJA_CMETA_VALUE_LOOP:
    *result = left->loop == right->loop;
    break;
  case JINJA_CMETA_VALUE_CALLABLE:
    if (left->callable_kind != right->callable_kind) break;
    if (left->host_callable != right->host_callable) break;
    if (left->callable_kind == JINJA_CMETA_EXPRESSION_MACRO || left->callable_kind == JINJA_CMETA_EXPRESSION_BLOCK)
      *result = left->closure == right->closure;
    else if (left->callable_kind == JINJA_CMETA_EXPRESSION_RANGE ||
             left->callable_kind == JINJA_CMETA_EXPRESSION_DICT_CALL ||
             left->callable_kind == JINJA_CMETA_EXPRESSION_NAMESPACE ||
             left->callable_kind == JINJA_CMETA_EXPRESSION_CYCLER ||
             left->callable_kind == JINJA_CMETA_EXPRESSION_JOINER)
      *result = 1;
    break;
  case JINJA_CMETA_VALUE_TUPLE:
    if (left->collection_item_count == 0u && right->collection_item_count == 0u) {
      *result = 1;
      break;
    }
    break;
  default:
    break;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_apply_test(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TEST_KIND kind, vstr name, JINJA_CMETA_VALUE *operand,
    const JINJA_CMETA_CALL_INPUT *input, size_t depth, JINJA_CMETA_VALUE *value) {
  int result = 0;
  JINJA_CMETA_STATUS status;
  if (kind == JINJA_CMETA_TEST_HOST) {
    const JINJA_CMETA_CALLABLE *callable = jinja_cmeta_env_find_test(
        provider->instance->templ->env, name);
    JINJA_CMETA_VALUE host_result;
    if (callable == NULL) return JINJA_CMETA_ERR_RENDER;
    status = jinja_invoke_host_function(provider, callable, input, operand, &host_result);
    if (status != JINJA_CMETA_OK) return status;
    if (host_result.kind != JINJA_CMETA_VALUE_BOOL) return JINJA_CMETA_ERR_RENDER;
    *value = host_result;
    return JINJA_CMETA_OK;
  }
  jinja_normalize_call_argument(operand);
  for (size_t i = input->positional; i < input->count; ++i) {
    vstr keyword = input->keywords[i];
    if (!((kind == JINJA_CMETA_TEST_DIVISIBLEBY &&
           jinja_name_equal(keyword.data, keyword.len, "num")) ||
          (kind == JINJA_CMETA_TEST_IN &&
           jinja_name_equal(keyword.data, keyword.len, "seq")) ||
          (kind == JINJA_CMETA_TEST_SAMEAS &&
           jinja_name_equal(keyword.data, keyword.len, "other"))))
      return JINJA_CMETA_ERR_RENDER;
  }
  if (kind == JINJA_CMETA_TEST_SAMEAS) {
    if (input->count != 1u) return JINJA_CMETA_ERR_RENDER;
    status = jinja_same_value(provider, operand, &input->values[0], &result);
  } else if (kind == JINJA_CMETA_TEST_DIVISIBLEBY) {
    if (input->count != 1u) return JINJA_CMETA_ERR_RENDER;
    status = jinja_value_divisibleby(provider, operand, &input->values[0], &result);
  } else if (kind == JINJA_CMETA_TEST_IN) {
    if (input->count != 1u) return JINJA_CMETA_ERR_RENDER;
    status = jinja_compare_values(provider, operand, &input->values[0],
        JINJA_CMETA_COMPARISON_IN, depth + 1u, &result);
  } else if (kind >= JINJA_CMETA_TEST_EQUAL && kind <= JINJA_CMETA_TEST_GREATER_EQUAL) {
    static const JINJA_CMETA_COMPARISON_KIND comparisons[] = {
      JINJA_CMETA_COMPARISON_EQUAL, JINJA_CMETA_COMPARISON_NOT_EQUAL,
      JINJA_CMETA_COMPARISON_LESS, JINJA_CMETA_COMPARISON_LESS_EQUAL,
      JINJA_CMETA_COMPARISON_GREATER, JINJA_CMETA_COMPARISON_GREATER_EQUAL
    };
    if (input->count != 1u) return JINJA_CMETA_ERR_RENDER;
    status = jinja_compare_values(provider, operand, &input->values[0],
        comparisons[kind - JINJA_CMETA_TEST_EQUAL], depth + 1u, &result);
  } else {
    if (input->count != 0u) return JINJA_CMETA_ERR_RENDER;
    status = jinja_value_test(provider, operand, kind, &result);
  }
  if (status != JINJA_CMETA_OK) return status;
  value->kind = JINJA_CMETA_VALUE_BOOL;
  value->boolean = result != 0;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_evaluate_test(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t index, size_t depth, JINJA_CMETA_VALUE *value, JINJA_CMETA_VALUE *arguments) {
  JINJA_CMETA_VALUE *operand = arguments++;
  JINJA_CMETA_CALL_INPUT input = {0};
  if (expression->left_node >= index ||
      expression->first_collection_item > provider->instance->templ->collection_item_count ||
      expression->collection_item_count > provider->instance->templ->collection_item_count - expression->first_collection_item)
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STATUS status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, operand);
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_collect_call_input(provider, context, expression, index, depth, arguments, &input);
  if (status != JINJA_CMETA_OK) return status;
  return jinja_apply_test(provider, expression->test, expression->path,
      operand, &input, depth, value);
}

static JINJA_CMETA_STATUS jinja_transform_prepare(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TRANSFORM *transform) {
  if (transform->prepared) return JINJA_CMETA_OK;
  int truthy;
  JINJA_CMETA_STATUS status = jinja_value_truthy(provider, &transform->source.input, &truthy);
  if (status != JINJA_CMETA_OK) return status;
  if (!truthy) {
    transform->prepared = transform->done = 1;
    return JINJA_CMETA_OK;
  }
  JINJA_CMETA_CALL_INPUT *call = &transform->call;
  const JINJA_CMETA_VALUE *attribute = NULL;
  size_t skip = 0u;
  if (transform->kind == JINJA_CMETA_EXPRESSION_MAP) {
    if (call->positional == 0u) {
      for (size_t i = 0u; i < call->count; ++i) {
        if (jinja_name_equal(call->keywords[i].data, call->keywords[i].len, "attribute"))
          attribute = &call->values[i];
        else if (jinja_name_equal(call->keywords[i].data, call->keywords[i].len, "default")) {
          if (call->values[i].kind != JINJA_CMETA_VALUE_NONE)
            transform->default_value = &call->values[i];
        } else return JINJA_CMETA_ERR_RENDER;
      }
      if (attribute == NULL) return JINJA_CMETA_ERR_RENDER;
    }
  } else if (transform->kind == JINJA_CMETA_EXPRESSION_SELECTATTR ||
             transform->kind == JINJA_CMETA_EXPRESSION_REJECTATTR) {
    if (call->positional == 0u) return JINJA_CMETA_ERR_RENDER;
    attribute = &call->values[skip++];
  }
  if (attribute != NULL) {
    status = jinja_attribute_parts(provider, attribute, &transform->parts);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (call->positional > skip) transform->name = &call->values[skip++];
  call->values += skip;
  call->count -= skip;
  call->positional -= skip;
  memmove(call->keywords, call->keywords + skip, call->count * sizeof(*call->keywords));
  transform->prepared = 1;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_transform_remaining_bound(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TRANSFORM *transform, size_t *count) {
  JINJA_CMETA_STATUS status = jinja_transform_prepare(provider, transform);
  if (status != JINJA_CMETA_OK) return status;
  if (transform->done) { *count = 0u; return JINJA_CMETA_OK; }
  return jinja_iteration_bound(provider, &transform->source, count);
}

/* Selection scans only through the next match and yields the original item.
 * Mapping applies the same evaluated filter path used by direct template calls. */
static JINJA_CMETA_STATUS jinja_transform_next(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TRANSFORM *transform, JINJA_CMETA_VALUE *result, int *found) {
  *found = 0;
  *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  JINJA_CMETA_STATUS status = jinja_transform_prepare(provider, transform);
  if (status != JINJA_CMETA_OK || transform->done) return status;
  if (transform->kind == JINJA_CMETA_EXPRESSION_UNIQUE) {
    for (;;) {
      JINJA_CMETA_VALUE item;
      JINJA_CMETA_VALUE key;
      int present;
      int hashable = 0;
      int seen = 0;

      status = jinja_iteration_next(provider, &transform->source, &item, &present);
      if (status != JINJA_CMETA_OK) return status;
      if (!present) {
        transform->done = 1;
        return JINJA_CMETA_OK;
      }
      key = item;
      status = jinja_project_attribute(provider, &transform->parts, NULL, &key);
      if (status == JINJA_CMETA_OK && !transform->case_sensitive && jinja_value_is_string(&key))
        status = jinja_case_value(provider, &key, SALTS_UNICODE_CASE_LOWER, &key);
      if (status == JINJA_CMETA_OK) status = jinja_dict_key_hashable(provider, &key, &hashable);
      if (status != JINJA_CMETA_OK) return status;
      if (!hashable) return JINJA_CMETA_ERR_RENDER;
      for (size_t i = 0u; i < transform->unique_count; ++i) {
        int equal = 0;
        status = jinja_container_equal(provider, &transform->unique_seen[i], &key, 0u, &equal);
        if (status != JINJA_CMETA_OK) return status;
        if (equal) {
          seen = 1;
          break;
        }
      }
      if (seen) continue;
      if (transform->unique_count == transform->unique_capacity) return JINJA_CMETA_ERR_CAPACITY;
      transform->unique_seen[transform->unique_count++] = key;
      *result = item;
      *found = 1;
      return JINJA_CMETA_OK;
    }
  }
  for (;;) {
    JINJA_CMETA_VALUE item, projected;
    int present, selected;
    status = jinja_iteration_next(provider, &transform->source, &item, &present);
    if (status != JINJA_CMETA_OK) return status;
    if (!present) { transform->done = 1; return JINJA_CMETA_OK; }
    projected = item;
    status = jinja_project_attribute(provider, &transform->parts, transform->default_value, &projected);
    if (status != JINJA_CMETA_OK) return status;
    if (transform->name != NULL) {
      if (!jinja_value_is_string(transform->name)) return JINJA_CMETA_ERR_RENDER;
      JINJA_CMETA_SCALAR scalar;
      status = jinja_scalar_from_value(provider, transform->name, &scalar);
      if (status != JINJA_CMETA_OK) return status;
      const vstr name = vstr_from_buf((const char *)scalar.data, scalar.size);
      if (transform->kind == JINJA_CMETA_EXPRESSION_MAP) {
        JINJA_CMETA_EXPRESSION_KIND kind;
        if (!jinja_builtin_filter_kind(name, &kind)) kind = JINJA_CMETA_EXPRESSION_HOST_FILTER;
        status = jinja_apply_filter(provider, transform->context, kind, name,
            &projected, &transform->call, result);
        if (status == JINJA_CMETA_OK) *found = 1;
        return status;
      }
      JINJA_CMETA_TEST_KIND test;
      if (!jinja_builtin_test_kind(name, &test)) test = JINJA_CMETA_TEST_HOST;
      status = jinja_apply_test(provider, test, name, &projected, &transform->call, 0u, &projected);
      if (status != JINJA_CMETA_OK) return status;
    }
    if (transform->kind == JINJA_CMETA_EXPRESSION_MAP) {
      *result = projected;
      *found = 1;
      return JINJA_CMETA_OK;
    }
    status = jinja_value_truthy(provider, &projected, &selected);
    if (status != JINJA_CMETA_OK) return status;
    if (transform->kind == JINJA_CMETA_EXPRESSION_REJECT ||
        transform->kind == JINJA_CMETA_EXPRESSION_REJECTATTR) selected = !selected;
    if (selected) {
      *result = item;
      *found = 1;
      return JINJA_CMETA_OK;
    }
  }
}

static JINJA_CMETA_STATUS jinja_evaluate_buffered_call(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t index, size_t depth, JINJA_CMETA_VALUE *value, int filter) {
  const size_t saved_count = provider->shared.call_argument_count;
  const size_t operand_count = filter || expression->kind == JINJA_CMETA_EXPRESSION_CALL ||
      expression->kind == JINJA_CMETA_EXPRESSION_TEST ? 1u : 0u;
  if (saved_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      operand_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS - saved_count ||
      expression->collection_item_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS - saved_count - operand_count)
    return JINJA_CMETA_ERR_CAPACITY;
  /* Argument snapshots are render-owned, fixed-capacity and reclaimed LIFO.
   * Re-entry must not reserve a maximum-size C stack array per call. */
  if (provider->shared.call_arguments == NULL) {
    provider->shared.call_arguments = (JINJA_CMETA_VALUE *)jinja_provider_zero(provider, 
        JINJA_CMETA_MAX_COMPILED_EXPRESSIONS, sizeof(*provider->shared.call_arguments));
    if (provider->shared.call_arguments == NULL) return provider->shared.status;
  }
  JINJA_CMETA_VALUE *arguments = provider->shared.call_arguments + saved_count;
  provider->shared.call_argument_count += expression->collection_item_count + operand_count;
  JINJA_CMETA_STATUS status = filter
      ? jinja_evaluate_filter(provider, context, expression, index, depth, value, arguments)
      : expression->kind == JINJA_CMETA_EXPRESSION_CALL
      ? jinja_invoke_callable(provider, context, expression, index, depth, value, arguments)
      : expression->kind == JINJA_CMETA_EXPRESSION_LOOP_RECURSE
      ? jinja_evaluate_recursive_loop(provider, context, expression, index, depth, value, arguments)
      : expression->kind == JINJA_CMETA_EXPRESSION_TEST
      ? jinja_evaluate_test(provider, context, expression, index, depth, value, arguments)
      : jinja_evaluate_loop_call(provider, context, expression, index, depth, value, arguments);
  provider->shared.call_argument_count = saved_count;
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_value_impl(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t index, size_t depth, JINJA_CMETA_VALUE *value);

static JINJA_CMETA_STATUS jinja_expression_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t index, size_t depth, JINJA_CMETA_VALUE *value) {
  if (provider == NULL || context == NULL || value == NULL || provider->instance->templ == NULL ||
      provider->instance->templ->expressions == NULL || index >= provider->instance->templ->expression_count ||
      depth >= provider->instance->templ->expression_count)
    return JINJA_CMETA_ERR_RENDER;
  /* A recursive loop re-enters expression evaluation with a new local depth.
   * Keep one budget for all live expressions in this render instead. */
  if (provider->shared.active_expression_depth == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS)
    return JINJA_CMETA_ERR_CAPACITY;
  ++provider->shared.active_expression_depth;
  const JINJA_CMETA_EXPRESSION_NODE *expression = &provider->instance->templ->expressions[index];
  JINJA_CMETA_STATUS status;
  memset(value, 0, sizeof(*value));
  switch (expression->kind) {
  case JINJA_CMETA_EXPRESSION_CALL:
  case JINJA_CMETA_EXPRESSION_TEST:
  case JINJA_CMETA_EXPRESSION_LOOP_CYCLE:
  case JINJA_CMETA_EXPRESSION_LOOP_CHANGED:
  case JINJA_CMETA_EXPRESSION_LOOP_RECURSE:
    status = jinja_evaluate_buffered_call(provider, context, expression, index, depth, value, 0);
    break;
  case JINJA_CMETA_EXPRESSION_DEFAULT:
  case JINJA_CMETA_EXPRESSION_ABS:
  case JINJA_CMETA_EXPRESSION_TO_STRING:
  case JINJA_CMETA_EXPRESSION_UPPER:
  case JINJA_CMETA_EXPRESSION_LOWER:
  case JINJA_CMETA_EXPRESSION_CAPITALIZE:
  case JINJA_CMETA_EXPRESSION_TITLE:
  case JINJA_CMETA_EXPRESSION_SAFE:
  case JINJA_CMETA_EXPRESSION_ESCAPE:
  case JINJA_CMETA_EXPRESSION_FORCEESCAPE:
  case JINJA_CMETA_EXPRESSION_TRIM:
  case JINJA_CMETA_EXPRESSION_CENTER:
  case JINJA_CMETA_EXPRESSION_INDENT:
  case JINJA_CMETA_EXPRESSION_TRUNCATE:
  case JINJA_CMETA_EXPRESSION_TO_INT:
  case JINJA_CMETA_EXPRESSION_TO_FLOAT:
  case JINJA_CMETA_EXPRESSION_ROUND:
  case JINJA_CMETA_EXPRESSION_FILESIZEFORMAT:
  case JINJA_CMETA_EXPRESSION_FORMAT:
  case JINJA_CMETA_EXPRESSION_WORDCOUNT:
  case JINJA_CMETA_EXPRESSION_WORDWRAP:
  case JINJA_CMETA_EXPRESSION_URLENCODE:
  case JINJA_CMETA_EXPRESSION_XMLATTR:
  case JINJA_CMETA_EXPRESSION_REVERSE:
  case JINJA_CMETA_EXPRESSION_BATCH:
  case JINJA_CMETA_EXPRESSION_SLICE_FILTER:
  case JINJA_CMETA_EXPRESSION_SUM:
  case JINJA_CMETA_EXPRESSION_MIN:
  case JINJA_CMETA_EXPRESSION_MAX:
  case JINJA_CMETA_EXPRESSION_SORT:
  case JINJA_CMETA_EXPRESSION_DICTSORT:
  case JINJA_CMETA_EXPRESSION_UNIQUE:
  case JINJA_CMETA_EXPRESSION_GROUPBY:
  case JINJA_CMETA_EXPRESSION_PPRINT:
  case JINJA_CMETA_EXPRESSION_RANDOM:
  case JINJA_CMETA_EXPRESSION_STRIPTAGS:
  case JINJA_CMETA_EXPRESSION_URLIZE:
  case JINJA_CMETA_EXPRESSION_TOJSON:
  case JINJA_CMETA_EXPRESSION_JOIN:
  case JINJA_CMETA_EXPRESSION_ATTR:
  case JINJA_CMETA_EXPRESSION_REPLACE:
  case JINJA_CMETA_EXPRESSION_MAP:
  case JINJA_CMETA_EXPRESSION_SELECT:
  case JINJA_CMETA_EXPRESSION_REJECT:
  case JINJA_CMETA_EXPRESSION_SELECTATTR:
  case JINJA_CMETA_EXPRESSION_REJECTATTR:
  case JINJA_CMETA_EXPRESSION_TO_LIST:
  case JINJA_CMETA_EXPRESSION_ITEMS:
  case JINJA_CMETA_EXPRESSION_FIRST:
  case JINJA_CMETA_EXPRESSION_LAST:
  case JINJA_CMETA_EXPRESSION_LENGTH:
    status = jinja_evaluate_buffered_call(provider, context, expression, index, depth, value, 1);
    break;
  default:
    status = jinja_expression_value_impl(provider, context, index, depth, value);
    break;
  }
  if (status == JINJA_CMETA_OK && (expression->unary_not_count != 0u || expression->force_boolean)) {
    int truthy;
    status = jinja_value_truthy(provider, value, &truthy);
    if (status == JINJA_CMETA_OK) {
      value->kind = JINJA_CMETA_VALUE_BOOL;
      value->boolean = (expression->unary_not_count & 1u) != 0u ? !truthy : truthy != 0;
      value->identity = (JINJA_CMETA_IDENTITY){0};
    }
  }
  --provider->shared.active_expression_depth;
  if (status == JINJA_CMETA_OK) status = jinja_value_identify(provider, value);
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_value_impl(JINJA_CMETA_PROVIDER *provider,
                                                 JINJA_CMETA_NODE *context, size_t index,
                                                 size_t depth, JINJA_CMETA_VALUE *value) {
  /* Only one expression kind is active in a frame. Sharing its scratch values
   * keeps recursive template calls from multiplying disjoint ASan stack slots. */
  union {
    struct { JINJA_CMETA_VALUE base, key; } lookup;
    struct { JINJA_CMETA_VALUE operand; } unary;
    struct { JINJA_CMETA_VALUE left_value, right_value; } binary;
    struct { JINJA_CMETA_VALUE left, right; } comparison;
    struct { JINJA_CMETA_VALUE previous, current; } chain;
    struct { JINJA_CMETA_VALUE test; } conditional;
    struct { JINJA_CMETA_VALUE left; } logical;
  } scratch;
  const JINJA_CMETA_EXPRESSION_NODE *expression;
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;

  if (provider == NULL || context == NULL || value == NULL || provider->instance->templ == NULL ||
      provider->instance->templ->expressions == NULL || index >= provider->instance->templ->expression_count ||
      depth >= provider->instance->templ->expression_count)
    return JINJA_CMETA_ERR_RENDER;
  *value = (JINJA_CMETA_VALUE){0};
  expression = &provider->instance->templ->expressions[index];

  switch (expression->kind) {
  case JINJA_CMETA_EXPRESSION_CAPTURE:
    if (!provider->capture_value_active) return JINJA_CMETA_ERR_RENDER;
    *value = provider->capture_value;
    break;
  case JINJA_CMETA_EXPRESSION_BOOL:
    value->kind = JINJA_CMETA_VALUE_BOOL;
    value->boolean = expression->boolean != 0;
    break;
  case JINJA_CMETA_EXPRESSION_INTEGER:
    value->kind = JINJA_CMETA_VALUE_INTEGER;
    value->integer = expression->integer;
    value->identity.source = expression;
    break;
  case JINJA_CMETA_EXPRESSION_FLOAT:
    value->kind = JINJA_CMETA_VALUE_FLOAT;
    value->floating = expression->floating;
    value->identity.source = expression;
    break;
  case JINJA_CMETA_EXPRESSION_STRING:
    value->kind = JINJA_CMETA_VALUE_STRING;
    value->string = expression->string;
    value->identity.source = expression;
    break;
  case JINJA_CMETA_EXPRESSION_NONE:
    value->kind = JINJA_CMETA_VALUE_NONE;
    break;
  case JINJA_CMETA_EXPRESSION_LIST:
  case JINJA_CMETA_EXPRESSION_TUPLE:
  case JINJA_CMETA_EXPRESSION_DICT:
    status = jinja_evaluate_collection(provider, context, expression, depth, value);
    if (status != JINJA_CMETA_OK) return status;
    break;
  case JINJA_CMETA_EXPRESSION_PATH: {
    status = jinja_resolve_value_path(provider, context, expression->path, value);
    if (status != JINJA_CMETA_OK) return status;
    break;
  }
  case JINJA_CMETA_EXPRESSION_SLICE_LOOKUP:
  case JINJA_CMETA_EXPRESSION_CONCAT:
  case JINJA_CMETA_EXPRESSION_ITEM_LOOKUP: {

    if (expression->left_node >= index || expression->right_node >= index)
      return JINJA_CMETA_ERR_RENDER;
    status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.lookup.base);
    if (status == JINJA_CMETA_OK)
      status = jinja_expression_value(provider, context, expression->right_node, depth + 1u, &scratch.lookup.key);
    if (status == JINJA_CMETA_OK) {
      if (expression->kind == JINJA_CMETA_EXPRESSION_SLICE_LOOKUP)
        status = jinja_slice_value(provider, &scratch.lookup.base, &scratch.lookup.key, depth + 1u, value);
      else if (expression->kind == JINJA_CMETA_EXPRESSION_CONCAT)
        status = provider->autoescape ? jinja_markup_concat(provider, &scratch.lookup.base, &scratch.lookup.key, value)
                                      : jinja_concat_values(provider, &scratch.lookup.base, &scratch.lookup.key, value);
      else status = jinja_lookup_item(provider, &scratch.lookup.base, &scratch.lookup.key, depth + 1u, value);
    }
    if (status != JINJA_CMETA_OK) return status;
    break;
  }
  case JINJA_CMETA_EXPRESSION_ATTRIBUTE_LOOKUP: {

    if (expression->left_node >= index || !vstr_is_valid(expression->path) ||
        expression->path.len == 0u)
      return JINJA_CMETA_ERR_RENDER;
    status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.lookup.base);
    scratch.lookup.key = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string = expression->path};
    if (status == JINJA_CMETA_OK)
      status = jinja_lookup_item(provider, &scratch.lookup.base, &scratch.lookup.key, depth + 1u, value);
    if (status != JINJA_CMETA_OK) return status;
    break;
  }
  case JINJA_CMETA_EXPRESSION_UNARY_ARITHMETIC: {
    JINJA_CMETA_NUMBER number;
    JINJA_CMETA_NUMBER unused = {0};
    JINJA_CMETA_NUMBER result = {0};

    if (expression->left_node >= index) return JINJA_CMETA_ERR_RENDER;
    status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.unary.operand);
    if (status == JINJA_CMETA_OK) status = jinja_number_from_value(provider, &scratch.unary.operand, &number);
    if (status == JINJA_CMETA_OK)
      status = jinja_evaluate_number_arithmetic(expression->arithmetic, &number, &unused, &result);
    if (status != JINJA_CMETA_OK) return status;
    if (expression->arithmetic == JINJA_CMETA_ARITHMETIC_POSITIVE &&
        scratch.unary.operand.kind != JINJA_CMETA_VALUE_BOOL &&
        !(scratch.unary.operand.kind == JINJA_CMETA_VALUE_NODE &&
          scratch.unary.operand.node.desc->kind == CMETA_DATA_BOOL)) {
      *value = scratch.unary.operand;
      break;
    }
    if (result.kind == JINJA_CMETA_NUMBER_FLOAT) {
      value->kind = JINJA_CMETA_VALUE_FLOAT;
      value->floating = result.floating;
    } else {
      value->kind = JINJA_CMETA_VALUE_INTEGER;
      value->integer = result.integer;
    }
    break;
  }
  case JINJA_CMETA_EXPRESSION_BINARY_ARITHMETIC: {
    JINJA_CMETA_NUMBER left;
    JINJA_CMETA_NUMBER right;
    JINJA_CMETA_NUMBER result = {0};

    if (expression->left_node >= index || expression->right_node >= index)
      return JINJA_CMETA_ERR_RENDER;
    status =
        jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.binary.left_value);
    if (status == JINJA_CMETA_OK)
      status = jinja_expression_value(provider, context, expression->right_node, depth + 1u,
                                      &scratch.binary.right_value);
    if (status != JINJA_CMETA_OK) return status;
    jinja_normalize_call_argument(&scratch.binary.left_value);
    jinja_normalize_call_argument(&scratch.binary.right_value);
    if (expression->arithmetic == JINJA_CMETA_ARITHMETIC_MODULO &&
        jinja_value_is_string(&scratch.binary.left_value)) {
      status = jinja_format_value(provider, &scratch.binary.left_value, &scratch.binary.right_value, value);
      if (status != JINJA_CMETA_OK) return status;
      break;
    }
    if (expression->arithmetic == JINJA_CMETA_ARITHMETIC_MULTIPLY &&
        (jinja_value_is_string(&scratch.binary.left_value) || jinja_value_is_string(&scratch.binary.right_value))) {
      const int string_on_left = jinja_value_is_string(&scratch.binary.left_value);
      status = jinja_repeat_string(provider, string_on_left ? &scratch.binary.left_value : &scratch.binary.right_value,
                                   string_on_left ? &scratch.binary.right_value : &scratch.binary.left_value, value);
      if (status != JINJA_CMETA_OK) return status;
      break;
    }
    if (expression->arithmetic == JINJA_CMETA_ARITHMETIC_MULTIPLY &&
        (jinja_value_is_collection(scratch.binary.left_value.kind) ||
         jinja_value_is_collection(scratch.binary.right_value.kind))) {
      const int sequence_on_left = jinja_value_is_collection(scratch.binary.left_value.kind);
      status = jinja_repeat_collection(provider, sequence_on_left ? &scratch.binary.left_value : &scratch.binary.right_value,
                                       sequence_on_left ? &scratch.binary.right_value : &scratch.binary.left_value, value);
      if (status != JINJA_CMETA_OK) return status;
      break;
    }
    if (expression->arithmetic == JINJA_CMETA_ARITHMETIC_ADD &&
        jinja_value_is_collection(scratch.binary.left_value.kind)) {
      status = jinja_add_collections(provider, &scratch.binary.left_value, &scratch.binary.right_value, value);
      if (status != JINJA_CMETA_OK) return status;
      break;
    }
    if (expression->arithmetic == JINJA_CMETA_ARITHMETIC_ADD &&
        (scratch.binary.left_value.kind == JINJA_CMETA_VALUE_STRING ||
         (scratch.binary.left_value.kind == JINJA_CMETA_VALUE_NODE && scratch.binary.left_value.node.desc != NULL &&
          scratch.binary.left_value.node.desc->kind == CMETA_DATA_STRING))) {
      if (scratch.binary.right_value.kind != JINJA_CMETA_VALUE_STRING &&
          !(scratch.binary.right_value.kind == JINJA_CMETA_VALUE_NODE && scratch.binary.right_value.node.desc != NULL &&
            scratch.binary.right_value.node.desc->kind == CMETA_DATA_STRING))
        return JINJA_CMETA_ERR_RENDER;
      status = jinja_markup_concat(provider, &scratch.binary.left_value, &scratch.binary.right_value, value);
      if (status != JINJA_CMETA_OK) return status;
      if (!jinja_value_is_safe(&scratch.binary.left_value) && !jinja_value_is_safe(&scratch.binary.right_value)) {
        JINJA_CMETA_SCALAR left_text = {0}, right_text = {0};
        status = jinja_scalar_from_value(provider, &scratch.binary.left_value, &left_text);
        if (status == JINJA_CMETA_OK)
          status = jinja_scalar_from_value(provider, &scratch.binary.right_value, &right_text);
        if (status != JINJA_CMETA_OK) return status;
        if (left_text.size == 0u) value->identity = scratch.binary.right_value.identity;
        else if (right_text.size == 0u) value->identity = scratch.binary.left_value.identity;
      }
      break;
    }
    status = jinja_number_from_value(provider, &scratch.binary.left_value, &left);
    if (status == JINJA_CMETA_OK) status = jinja_number_from_value(provider, &scratch.binary.right_value, &right);
    if (status == JINJA_CMETA_OK)
      status = jinja_evaluate_number_arithmetic(expression->arithmetic, &left, &right, &result);
    if (status != JINJA_CMETA_OK) return status;
    if (result.kind == JINJA_CMETA_NUMBER_FLOAT) {
      value->kind = JINJA_CMETA_VALUE_FLOAT;
      value->floating = result.floating;
    } else {
      value->kind = JINJA_CMETA_VALUE_INTEGER;
      value->integer = result.integer;
    }
    break;
  }
  case JINJA_CMETA_EXPRESSION_COMPARISON: {
    int result = 0;
    status = jinja_value_from_operand(provider, context, &expression->left, &scratch.comparison.left);
    if (status == JINJA_CMETA_OK)
      status = jinja_value_from_operand(provider, context, &expression->right, &scratch.comparison.right);
    if (status == JINJA_CMETA_OK)
      status = jinja_compare_values(provider, &scratch.comparison.left, &scratch.comparison.right, expression->comparison,
                                    depth + 1u, &result);
    if (status != JINJA_CMETA_OK) return status;
    value->kind = JINJA_CMETA_VALUE_BOOL;
    value->boolean = result != 0;
    break;
  }
  case JINJA_CMETA_EXPRESSION_NESTED_COMPARISON: {
    int result = 0;

    if (expression->left_node >= index || expression->right_node >= index)
      return JINJA_CMETA_ERR_RENDER;
    status =
        jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.binary.left_value);
    if (status == JINJA_CMETA_OK)
      status = jinja_expression_value(provider, context, expression->right_node, depth + 1u,
                                      &scratch.binary.right_value);
    if (status == JINJA_CMETA_OK)
      status = jinja_compare_values(provider, &scratch.binary.left_value, &scratch.binary.right_value, expression->comparison,
                                    depth + 1u, &result);
    if (status != JINJA_CMETA_OK) return status;
    value->kind = JINJA_CMETA_VALUE_BOOL;
    value->boolean = result != 0;
    break;
  }
  case JINJA_CMETA_EXPRESSION_COMPARISON_CHAIN: {
    size_t i;

    if (expression->left_node >= index || provider->instance->templ->comparison_steps == NULL ||
        expression->first_comparison_step > provider->instance->templ->comparison_step_count ||
        expression->comparison_step_count >
            provider->instance->templ->comparison_step_count - expression->first_comparison_step)
      return JINJA_CMETA_ERR_RENDER;
    status =
        jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.chain.previous);
    if (status != JINJA_CMETA_OK) return status;
    for (i = 0u; i < expression->comparison_step_count; ++i) {
      const JINJA_CMETA_COMPARISON_STEP *step =
          &provider->instance->templ->comparison_steps[expression->first_comparison_step + i];
      int result = 0;

      if (step->operand_node >= index) return JINJA_CMETA_ERR_RENDER;
      status = jinja_expression_value(provider, context, step->operand_node, depth + 1u, &scratch.chain.current);
      if (status == JINJA_CMETA_OK)
        status = jinja_compare_values(provider, &scratch.chain.previous, &scratch.chain.current, step->comparison, depth + 1u,
                                      &result);
      if (status != JINJA_CMETA_OK) return status;
      if (!result) {
        value->kind = JINJA_CMETA_VALUE_BOOL;
        value->boolean = false;
        break;
      }
      scratch.chain.previous = scratch.chain.current;
    }
    if (i == expression->comparison_step_count) {
      value->kind = JINJA_CMETA_VALUE_BOOL;
      value->boolean = true;
    }
    break;
  }
  case JINJA_CMETA_EXPRESSION_CONDITIONAL: {
    int test_truthy;

    if (expression->left_node >= index || expression->test_node >= index ||
        (expression->has_else && expression->right_node >= index))
      return JINJA_CMETA_ERR_RENDER;
    status = jinja_expression_value(provider, context, expression->test_node, depth + 1u, &scratch.conditional.test);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_value_truthy(provider, &scratch.conditional.test, &test_truthy);
    if (status != JINJA_CMETA_OK) return status;
    if (test_truthy) {
      status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, value);
    } else if (expression->has_else) {
      status = jinja_expression_value(provider, context, expression->right_node, depth + 1u, value);
    } else {
      value->kind = JINJA_CMETA_VALUE_UNDEFINED;
    }
    if (status != JINJA_CMETA_OK) return status;
    break;
  }
  case JINJA_CMETA_EXPRESSION_LOGICAL_AND:
  case JINJA_CMETA_EXPRESSION_LOGICAL_OR: {
    int left_truthy;
    int select_left;

    if (expression->left_node >= index || expression->right_node >= index)
      return JINJA_CMETA_ERR_RENDER;
    status = jinja_expression_value(provider, context, expression->left_node, depth + 1u, &scratch.logical.left);
    if (status != JINJA_CMETA_OK) return status;
    status = jinja_value_truthy(provider, &scratch.logical.left, &left_truthy);
    if (status != JINJA_CMETA_OK) return status;
    select_left =
        expression->kind == JINJA_CMETA_EXPRESSION_LOGICAL_AND ? !left_truthy : left_truthy;
    if (select_left) {
      *value = scratch.logical.left;
    } else {
      status = jinja_expression_value(provider, context, expression->right_node, depth + 1u, value);
      if (status != JINJA_CMETA_OK) return status;
    }
    break;
  }
  default:
    return JINJA_CMETA_ERR_RENDER;
  }

  return JINJA_CMETA_OK;
}

static void *jinja_provider_value_node_impl(JINJA_CMETA_PROVIDER *provider,
                                       const JINJA_CMETA_VALUE *value, JINJA_CMETA_NODE *context) {
  static const char empty[] = "";
  if (provider == NULL || value == NULL) return NULL;
  switch (value->kind) {
  case JINJA_CMETA_VALUE_MODULE:
  case JINJA_CMETA_VALUE_TEMPLATE: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->expression_kind = value->kind == JINJA_CMETA_VALUE_MODULE
        ? JINJA_CMETA_EXPRESSION_MODULE : JINJA_CMETA_EXPRESSION_SELF;
    node->template_context = value->template_context;
    node->instance = value->instance;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_MISSING: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->is_missing = 1;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_LOOP: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->is_loop_object = 1;
    node->loop_receiver = value->loop;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_CALLABLE: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->expression_kind = JINJA_CMETA_EXPRESSION_CALL;
    node->callable_kind = value->callable_kind;
    node->closure = value->closure;
    node->helper = value->helper;
    node->host_callable = value->host_callable;
    node->loop_receiver = value->loop;
    node->range = value->range;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_ITERATOR: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->iterator = value->iterator;
    node->expression_kind = JINJA_CMETA_EXPRESSION_ITEMS;
    node->owned_sequence = (JINJA_CMETA_SEQUENCE_VIEW){0};
    node->object = &node->owned_sequence;
    node->desc = &jinja_cmeta_sequence_desc;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_UNDEFINED: {
    JINJA_CMETA_NODE *node = jinja_provider_string_node(provider, vstr_from_buf(empty, 0u), context);
    if (node != NULL) node->is_undefined = 1;
    return node;
  }
  case JINJA_CMETA_VALUE_RANGE: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->range = value->range;
    node->expression_kind = JINJA_CMETA_EXPRESSION_RANGE;
    node->owned_sequence = (JINJA_CMETA_SEQUENCE_VIEW){0};
    node->object = &node->owned_sequence;
    node->desc = &jinja_cmeta_sequence_desc;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_NAMESPACE: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->namespace_dict = value->namespace_dict;
    node->expression_kind = JINJA_CMETA_EXPRESSION_NAMESPACE;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_CYCLER:
  case JINJA_CMETA_VALUE_JOINER: {
    JINJA_CMETA_NODE *node = jinja_provider_reserve(provider);
    if (node == NULL) return NULL;
    node->helper = value->helper;
    node->expression_kind = value->kind == JINJA_CMETA_VALUE_CYCLER
        ? JINJA_CMETA_EXPRESSION_CYCLER : JINJA_CMETA_EXPRESSION_JOINER;
    node->parent = context;
    return node;
  }
  case JINJA_CMETA_VALUE_NONE: {
    JINJA_CMETA_NODE *node = jinja_provider_string_node(provider, vstr_from_buf("None", 4u), context);
    if (node != NULL) node->is_none = 1;
    return node;
  }
  case JINJA_CMETA_VALUE_BOOL:
    return jinja_provider_bool_node(provider, value->boolean, context);
  case JINJA_CMETA_VALUE_INTEGER:
    return jinja_provider_integer_node(provider, value->integer, context);
  case JINJA_CMETA_VALUE_FLOAT:
    return jinja_provider_float_node(provider, value->floating, context);
  case JINJA_CMETA_VALUE_STRING: {
    JINJA_CMETA_NODE *node = jinja_provider_string_node(provider, value->string, context);
    if (node != NULL) node->string_safe = value->string_safe;
    return node;
  }
  case JINJA_CMETA_VALUE_NODE:
    if (value->node.desc->kind == CMETA_DATA_BOOL) {
      bool boolean;
      memcpy(&boolean, value->node.object, sizeof(boolean));
      return jinja_provider_bool_node(provider, boolean, context);
    }
    return jinja_provider_node(provider, value->node.object, value->node.desc, context);
  case JINJA_CMETA_VALUE_LIST:
  case JINJA_CMETA_VALUE_TUPLE:
  case JINJA_CMETA_VALUE_DICT:
    return jinja_provider_collection_node(provider, value, context);
  }
  jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
  return NULL;
}



static void *jinja_provider_value_node(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *value, JINJA_CMETA_NODE *context) {
  JINJA_CMETA_NODE *node = jinja_provider_value_node_impl(provider, value, context);
  if (node != NULL) node->value_identity = value->identity;
  return node;
}

static JINJA_CMETA_STATUS jinja_repr_write(JINJA_CMETA_PROVIDER *provider,
                                           int (*out_fn)(const char *, size_t, void *),
                                           void *renderer_data, const char *text, size_t size) {
  if (provider == NULL || out_fn == NULL || (size != 0u && text == NULL))
    return JINJA_CMETA_ERR_RENDER;
  if (size != 0u && out_fn(text, size, renderer_data) != 0) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return JINJA_CMETA_ERR_RENDER;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_dump_string_repr(JINJA_CMETA_PROVIDER *provider, vstr value,
                                                 int (*out_fn)(const char *, size_t, void *),
                                                 void *renderer_data) {
  char quote = '\'';
  size_t chunk_start = 0u;
  size_t i;
  int has_single = 0;
  int has_double = 0;

  if (!vstr_is_valid(value)) return JINJA_CMETA_ERR_RENDER;
  for (i = 0u; i < value.len; ++i) {
    has_single |= value.data[i] == '\'';
    has_double |= value.data[i] == '"';
  }
  if (has_single && !has_double) quote = '"';
  if (jinja_repr_write(provider, out_fn, renderer_data, &quote, 1u) != JINJA_CMETA_OK)
    return JINJA_CMETA_ERR_RENDER;

  for (i = 0u; i < value.len; ++i) {
    const unsigned char byte = (unsigned char)value.data[i];
    const char *escape = NULL;
    size_t escape_size = 0u;
    char escaped_byte[4];

    if (value.data[i] == quote) {
      escaped_byte[0] = '\\';
      escaped_byte[1] = quote;
      escape = escaped_byte;
      escape_size = 2u;
    } else if (value.data[i] == '\\') {
      escape = "\\\\";
      escape_size = 2u;
    } else if (value.data[i] == '\n') {
      escape = "\\n";
      escape_size = 2u;
    } else if (value.data[i] == '\r') {
      escape = "\\r";
      escape_size = 2u;
    } else if (value.data[i] == '\t') {
      escape = "\\t";
      escape_size = 2u;
    } else if (byte < 0x20u || byte == 0x7fu) {
      static const char hexadecimal[] = "0123456789abcdef";
      escaped_byte[0] = '\\';
      escaped_byte[1] = 'x';
      escaped_byte[2] = hexadecimal[byte >> 4u];
      escaped_byte[3] = hexadecimal[byte & 0x0fu];
      escape = escaped_byte;
      escape_size = sizeof(escaped_byte);
    }
    if (escape == NULL) continue;
    if (i != chunk_start &&
        jinja_repr_write(provider, out_fn, renderer_data, value.data + chunk_start,
                         i - chunk_start) != JINJA_CMETA_OK)
      return JINJA_CMETA_ERR_RENDER;
    if (jinja_repr_write(provider, out_fn, renderer_data, escape, escape_size) != JINJA_CMETA_OK)
      return JINJA_CMETA_ERR_RENDER;
    chunk_start = i + 1u;
  }
  if (chunk_start != value.len &&
      jinja_repr_write(provider, out_fn, renderer_data, value.data + chunk_start,
                       value.len - chunk_start) != JINJA_CMETA_OK)
    return JINJA_CMETA_ERR_RENDER;
  return jinja_repr_write(provider, out_fn, renderer_data, &quote, 1u);
}

static JINJA_CMETA_STATUS jinja_dump_value_repr_impl(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *value, size_t depth,
    int (*out_fn)(const char *, size_t, void *), void *renderer_data);

static JINJA_CMETA_STATUS jinja_dump_value_repr(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_VALUE *value, size_t depth,
    int (*out_fn)(const char *, size_t, void *), void *renderer_data) {
  if (provider == NULL || value == NULL || provider->instance->templ == NULL)
    return JINJA_CMETA_ERR_RENDER;
  if (depth >= JINJA_CMETA_MAX_BLOCK_DEPTH) return JINJA_CMETA_ERR_CAPACITY;
  if (!jinja_value_is_container(value->kind) && value->kind != JINJA_CMETA_VALUE_NAMESPACE)
    return jinja_dump_value_repr_impl(provider, value, depth, out_fn, renderer_data);

  /* Only ancestors are recursive: sibling aliases and shallow copies retain
   * their complete repr. Frames borrow live caller values, with O(depth<=64)
   * identity lookup and no state surviving the synchronous output call. */
  size_t active_frames = 0u;
  for (const JINJA_CMETA_REPR_FRAME *ancestor = provider->repr_frame;
       ancestor != NULL; ancestor = ancestor->parent) {
    ++active_frames;
    if (ancestor->value->kind != value->kind) continue;
    int same = 0;
    JINJA_CMETA_STATUS status = jinja_same_value(provider, ancestor->value, value, &same);
    if (status != JINJA_CMETA_OK) return status;
    if (same) {
      const char *marker = value->kind == JINJA_CMETA_VALUE_LIST ? "[...]"
          : value->kind == JINJA_CMETA_VALUE_TUPLE ? "(...)"
          : value->kind == JINJA_CMETA_VALUE_DICT ? "{...}" : "<Namespace {...}>";
      return jinja_repr_write(provider, out_fn, renderer_data, marker, strlen(marker));
    }
  }
  /* Lazy loop length may reenter repr with local depth zero. Bound the actual
   * active path before pushing; a recursion marker above needs no new frame. */
  if (active_frames >= JINJA_CMETA_MAX_BLOCK_DEPTH) return JINJA_CMETA_ERR_CAPACITY;
  const JINJA_CMETA_REPR_FRAME frame = {provider->repr_frame, value};
  provider->repr_frame = &frame;
  JINJA_CMETA_STATUS status = jinja_dump_value_repr_impl(provider, value, depth, out_fn, renderer_data);
  provider->repr_frame = frame.parent;
  return status;
}

static JINJA_CMETA_STATUS jinja_dump_value_repr_impl(JINJA_CMETA_PROVIDER *provider,
                                                const JINJA_CMETA_VALUE *value, size_t depth,
                                                int (*out_fn)(const char *, size_t, void *),
                                                void *renderer_data) {
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status;
  enum { JINJA_VALUE_REPR_CAPACITY = 96 };
  char buffer[JINJA_VALUE_REPR_CAPACITY];
  size_t size = 0u;

  if (value->kind == JINJA_CMETA_VALUE_TEMPLATE)
    return jinja_repr_write(provider, out_fn, renderer_data, jinja_template_reference_repr,
        sizeof(jinja_template_reference_repr) - 1u);
  if (value->kind == JINJA_CMETA_VALUE_MODULE) {
    if (value->instance == NULL || value->instance->module_state != JINJA_CMETA_MODULE_READY)
      return JINJA_CMETA_ERR_METADATA;
    static const char prefix[] = "<TemplateModule ";
    status = jinja_repr_write(provider, out_fn, renderer_data, prefix, sizeof(prefix) - 1u);
    if (status == JINJA_CMETA_OK)
      status = jinja_dump_string_repr(provider, value->instance->templ->name, out_fn, renderer_data);
    if (status == JINJA_CMETA_OK)
      status = jinja_repr_write(provider, out_fn, renderer_data, ">", 1u);
    return status;
  }
  if (value->kind == JINJA_CMETA_VALUE_MISSING) {
    static const char text[] = "missing";
    return jinja_repr_write(provider, out_fn, renderer_data, text, sizeof(text) - 1u);
  }
  if (value->kind == JINJA_CMETA_VALUE_LOOP) {
    JINJA_CMETA_VALUE length;
    status = jinja_loop_attribute(provider, value->loop, vstr_from_cstr("length"), &length);
    if (status != JINJA_CMETA_OK) return status;
    int written = snprintf(buffer, sizeof(buffer), "<LoopContext %zu/%lld>",
        value->loop->loop_current->loop_index + 1u, (long long)length.integer);
    if (written < 0 || (size_t)written >= sizeof(buffer)) return JINJA_CMETA_ERR_RENDER;
    return jinja_repr_write(provider, out_fn, renderer_data, buffer, (size_t)written);
  }
  if (value->kind == JINJA_CMETA_VALUE_CALLABLE) {
    if (value->host_callable != NULL) {
      static const char prefix[] = "<function ";
      status = jinja_repr_write(provider, out_fn, renderer_data, prefix, sizeof(prefix) - 1u);
      if (status == JINJA_CMETA_OK)
        status = jinja_repr_write(provider, out_fn, renderer_data,
            value->host_callable->name.data, value->host_callable->name.len);
      if (status == JINJA_CMETA_OK) status = jinja_repr_write(provider, out_fn, renderer_data, ">", 1u);
      return status;
    }
    const char *text;
    switch (value->callable_kind) {
    case JINJA_CMETA_EXPRESSION_MACRO: {
      if (value->closure == NULL || value->closure->instance == NULL ||
          value->closure->function >= value->closure->instance->templ->function_count)
        return JINJA_CMETA_ERR_METADATA;
      const JINJA_CMETA_TEMPLATE *templ = value->closure->instance->templ;
      const JINJA_CMETA_FUNCTION *function = &templ->functions[value->closure->function];
      static const char prefix[] = "<Macro ";
      static const char anonymous[] = "anonymous";
      status = jinja_repr_write(provider, out_fn, renderer_data, prefix, sizeof(prefix) - 1u);
      if (status == JINJA_CMETA_OK) {
        if (function->name_length == 0u)
          status = jinja_repr_write(provider, out_fn, renderer_data, anonymous, sizeof(anonymous) - 1u);
        else
          status = jinja_dump_string_repr(provider,
              vstr_from_buf(templ->program_strings + function->name_offset, function->name_length),
              out_fn, renderer_data);
      }
      if (status == JINJA_CMETA_OK) status = jinja_repr_write(provider, out_fn, renderer_data, ">", 1u);
      return status;
    }
    case JINJA_CMETA_EXPRESSION_RANGE: text = "<class 'range'>"; break;
    case JINJA_CMETA_EXPRESSION_DICT_CALL: text = "<class 'dict'>"; break;
    case JINJA_CMETA_EXPRESSION_NAMESPACE: text = "<class 'jinja2.utils.Namespace'>"; break;
    case JINJA_CMETA_EXPRESSION_CYCLER: text = "<class 'jinja2.utils.Cycler'>"; break;
    case JINJA_CMETA_EXPRESSION_JOINER: text = "<class 'jinja2.utils.Joiner'>"; break;
    /* Bound-method repr embeds a Python object address; no fabricated host identity. */
    default: return JINJA_CMETA_ERR_RENDER;
    }
    return jinja_repr_write(provider, out_fn, renderer_data, text, strlen(text));
  }
  if (value->kind == JINJA_CMETA_VALUE_NAMESPACE) {
    if (value->namespace_dict == NULL) return JINJA_CMETA_ERR_RENDER;
    static const char prefix[] = "<Namespace ";
    status = jinja_repr_write(provider, out_fn, renderer_data, prefix, sizeof(prefix) - 1u);
    if (status == JINJA_CMETA_OK)
      status = jinja_dump_value_repr(provider, value->namespace_dict, depth + 1u, out_fn, renderer_data);
    if (status == JINJA_CMETA_OK) status = jinja_repr_write(provider, out_fn, renderer_data, ">", 1u);
    return status;
  }
  if (value->kind == JINJA_CMETA_VALUE_RANGE) {
    int written;
    if (value->range.step == 1)
      written = snprintf(buffer, sizeof(buffer), "range(%lld, %lld)", (long long)value->range.start,
                         (long long)value->range.stop);
    else
      written =
          snprintf(buffer, sizeof(buffer), "range(%lld, %lld, %lld)", (long long)value->range.start,
                   (long long)value->range.stop, (long long)value->range.step);
    if (written < 0 || (size_t)written >= sizeof(buffer)) return JINJA_CMETA_ERR_RENDER;
    return jinja_repr_write(provider, out_fn, renderer_data, buffer, (size_t)written);
  }
  if (value->kind == JINJA_CMETA_VALUE_DICT) {
    size_t position;
    size_t written = 0u;
    status = jinja_repr_write(provider, out_fn, renderer_data, "{", 1u);
    for (position = 0u; status == JINJA_CMETA_OK && position < value->collection_item_count;
         ++position) {
      JINJA_CMETA_VALUE key;
      JINJA_CMETA_VALUE item;
      JINJA_CMETA_VALUE ignored;
      int first;
      int found = 0;

      status = jinja_dict_entry_is_first(provider, value, position, 0u, &first);
      if (status != JINJA_CMETA_OK || !first) continue;
      status = jinja_dict_entry_value(provider, value, position, 0u, &key, &ignored);
      if (status == JINJA_CMETA_OK)
        status = jinja_dict_lookup(provider, value, &key, 0u, &item, &found);
      if (status == JINJA_CMETA_OK && !found) status = JINJA_CMETA_ERR_RENDER;
      if (status == JINJA_CMETA_OK && written != 0u)
        status = jinja_repr_write(provider, out_fn, renderer_data, ", ", 2u);
      if (status == JINJA_CMETA_OK)
        status = jinja_dump_value_repr(provider, &key, depth + 1u, out_fn, renderer_data);
      if (status == JINJA_CMETA_OK)
        status = jinja_repr_write(provider, out_fn, renderer_data, ": ", 2u);
      if (status == JINJA_CMETA_OK)
        status = jinja_dump_value_repr(provider, &item, depth + 1u, out_fn, renderer_data);
      if (status == JINJA_CMETA_OK) ++written;
    }
    if (status == JINJA_CMETA_OK)
      status = jinja_repr_write(provider, out_fn, renderer_data, "}", 1u);
    return status;
  }
  if (jinja_value_is_collection(value->kind)) {
    const int tuple = value->kind == JINJA_CMETA_VALUE_TUPLE;
    size_t i;
    status = jinja_repr_write(provider, out_fn, renderer_data, tuple ? "(" : "[", 1u);
    for (i = 0u; status == JINJA_CMETA_OK && i < value->collection_item_count; ++i) {
      JINJA_CMETA_VALUE item;
      if (i != 0u) status = jinja_repr_write(provider, out_fn, renderer_data, ", ", 2u);
      if (status == JINJA_CMETA_OK)
        status = jinja_collection_item_value(provider, value, i, depth, &item);
      if (status == JINJA_CMETA_OK)
        status = jinja_dump_value_repr(provider, &item, depth + 1u, out_fn, renderer_data);
    }
    if (status == JINJA_CMETA_OK && tuple && value->collection_item_count == 1u)
      status = jinja_repr_write(provider, out_fn, renderer_data, ",", 1u);
    if (status == JINJA_CMETA_OK)
      status = jinja_repr_write(provider, out_fn, renderer_data, tuple ? ")" : "]", 1u);
    return status;
  }
  if (value->kind == JINJA_CMETA_VALUE_UNDEFINED)
    return jinja_repr_write(provider, out_fn, renderer_data, "Undefined", 9u);
  if (value->kind == JINJA_CMETA_VALUE_NONE)
    return jinja_repr_write(provider, out_fn, renderer_data, "None", 4u);

  status = jinja_scalar_from_value(provider, value, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  switch (scalar.kind) {
  case JINJA_CMETA_SCALAR_BOOL:
    return jinja_repr_write(provider, out_fn, renderer_data, scalar.boolean ? "True" : "False",
                            scalar.boolean ? 4u : 5u);
  case JINJA_CMETA_SCALAR_SINT: {
    int written = snprintf(buffer, sizeof(buffer), "%lld", (long long)scalar.sint);
    if (written < 0 || (size_t)written >= sizeof(buffer)) return JINJA_CMETA_ERR_RENDER;
    size = (size_t)written;
    break;
  }
  case JINJA_CMETA_SCALAR_UINT: {
    int written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)scalar.uint);
    if (written < 0 || (size_t)written >= sizeof(buffer)) return JINJA_CMETA_ERR_RENDER;
    size = (size_t)written;
    break;
  }
  case JINJA_CMETA_SCALAR_FLOAT:
    if (!jinja_float_format(scalar.floating, buffer, sizeof(buffer), &size))
      return JINJA_CMETA_ERR_RENDER;
    break;
  case JINJA_CMETA_SCALAR_STRING: {
    const int markup = jinja_value_is_safe(value);
    if (markup) {
      static const char prefix[] = "Markup(";
      status = jinja_repr_write(provider, out_fn, renderer_data, prefix, sizeof(prefix) - 1u);
    }
    if (status == JINJA_CMETA_OK)
      status = jinja_dump_string_repr(provider, vstr_from_buf((const char *)scalar.data, scalar.size),
                                      out_fn, renderer_data);
    if (status == JINJA_CMETA_OK && markup)
      status = jinja_repr_write(provider, out_fn, renderer_data, ")", 1u);
    return status;
  }
  case JINJA_CMETA_SCALAR_NONE:
    return jinja_repr_write(provider, out_fn, renderer_data, "None", 4u);
  case JINJA_CMETA_SCALAR_UNDEFINED:
    return jinja_repr_write(provider, out_fn, renderer_data, "Undefined", 9u);
  }
  return jinja_repr_write(provider, out_fn, renderer_data, buffer, size);
}

static int jinja_concat_write(const char *text, size_t size, void *opaque) {
  JINJA_CMETA_PROVIDER *provider = (JINJA_CMETA_PROVIDER *)opaque;
  if (!jinja_string_bytes_fit(provider, size)) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
    return -1;
  }
  if (size == 0u) return 0;
  if (provider->shared.slice_bytes == NULL) {
    provider->shared.slice_bytes = (char *)jinja_provider_allocate(provider, provider->shared.max_string_bytes, sizeof(char));
    if (provider->shared.slice_bytes == NULL) {
      jinja_provider_fail(provider, provider->shared.status);
      return -1;
    }
  }
  memcpy(provider->shared.slice_bytes + provider->shared.slice_byte_count, text, size);
  provider->shared.slice_byte_count += size;
  return 0;
}

static JINJA_CMETA_STATUS jinja_repeat_string(JINJA_CMETA_PROVIDER *provider,
                                              const JINJA_CMETA_VALUE *sequence,
                                              const JINJA_CMETA_VALUE *multiplier,
                                              JINJA_CMETA_VALUE *result) {
  int64_t repetitions;
  int valid;
  size_t offset = provider->shared.slice_byte_count;
  size_t bytes = 0u;
  size_t i;
  JINJA_CMETA_SCALAR scalar;
  JINJA_CMETA_STATUS status = jinja_lookup_integer_key(provider, multiplier, &repetitions, &valid);
  if (status != JINJA_CMETA_OK) return status;
  if (!valid) return JINJA_CMETA_ERR_RENDER;
  status = jinja_scalar_from_value(provider, sequence, &scalar);
  if (status != JINJA_CMETA_OK) return status;
  if (repetitions == 1 && !jinja_value_is_safe(sequence)) {
    *result = *sequence;
    return JINJA_CMETA_OK;
  }
  if (offset > provider->shared.max_string_bytes) return JINJA_CMETA_ERR_CAPACITY;
  if (repetitions > 0 && scalar.size != 0u) {
    if ((uint64_t)repetitions > (provider->shared.max_string_bytes - offset) / scalar.size)
      return JINJA_CMETA_ERR_CAPACITY;
    bytes = (size_t)repetitions * scalar.size;
    for (i = 0u; i < bytes; i += scalar.size)
      if (jinja_concat_write((const char *)scalar.data, scalar.size, provider) != 0)
        return provider->shared.status;
  }
  *result = (JINJA_CMETA_VALUE){
      .kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = jinja_value_is_safe(sequence),
      .string =
          bytes == 0u ? vstr_from_cstr("") : vstr_from_buf(provider->shared.slice_bytes + offset, bytes)};
  return JINJA_CMETA_OK;
}

static int jinja_string_write(const char *text, size_t size, void *opaque) {
  JINJA_CMETA_STRING_OUTPUT *output = (JINJA_CMETA_STRING_OUTPUT *)opaque;
  JINJA_CMETA_PROVIDER *provider = output->provider;
  if (!jinja_string_bytes_fit(provider, size) ||
      !jinja_cmeta_string_append_fits(jinja_cmeta_text_length(&output->bytes), size, SIZE_MAX)) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
    return -1;
  }
  if (size == 0u) return 0;
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  if (output->bytes.bytes == NULL)
    status = jinja_cmeta_text_init(&output->bytes, provider->shared.memory);
  if (status == JINJA_CMETA_OK)
    status = jinja_cmeta_text_append(&output->bytes, text, size);
  if (status != JINJA_CMETA_OK) {
    jinja_provider_fail(provider, status);
    return -1;
  }
  provider->shared.pending_string_bytes += size;
  return 0;
}

static JINJA_CMETA_STATUS jinja_concat_operand(JINJA_CMETA_STRING_OUTPUT *output,
                                               JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_PROVIDER *provider = output->provider;
  jinja_normalize_call_argument(value);
  if (value->kind == JINJA_CMETA_VALUE_UNDEFINED) return JINJA_CMETA_OK;
  if (value->kind == JINJA_CMETA_VALUE_MODULE) {
    if (value->instance == NULL || value->instance->module_state != JINJA_CMETA_MODULE_READY)
      return JINJA_CMETA_ERR_METADATA;
    const vstr body = value->instance->module_body;
    return jinja_string_write(body.data, body.len, output) == 0
        ? JINJA_CMETA_OK : provider->shared.status;
  }
  if (value->kind == JINJA_CMETA_VALUE_STRING ||
      (value->kind == JINJA_CMETA_VALUE_NODE && value->node.desc != NULL &&
       value->node.desc->kind == CMETA_DATA_STRING)) {
    JINJA_CMETA_SCALAR scalar;
    JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, value, &scalar);
    if (status != JINJA_CMETA_OK) return status;
    return jinja_string_write((const char *)scalar.data, scalar.size, output) == 0
               ? JINJA_CMETA_OK
               : provider->shared.status;
  }
  return jinja_dump_value_repr(provider, value, 0u, jinja_string_write, output);
}

static JINJA_CMETA_STATUS jinja_string_finish(JINJA_CMETA_STRING_OUTPUT *output,
    JINJA_CMETA_STATUS status, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_PROVIDER *provider = output->provider;
  if (provider->shared.status != JINJA_CMETA_OK) status = provider->shared.status;
  const size_t size = jinja_cmeta_text_length(&output->bytes);
  provider->shared.pending_string_bytes -= size;
  if (status == JINJA_CMETA_OK) {
    const size_t offset = provider->shared.slice_byte_count;
    if (jinja_concat_write(jinja_cmeta_text_data(&output->bytes), size, provider) != 0) status = provider->shared.status;
    else *result = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
        .string = size == 0u ? vstr_from_cstr("") : vstr_from_buf(provider->shared.slice_bytes + offset, size)};
  }
  jinja_cmeta_text_destroy(&output->bytes);
  return status;
}

static JINJA_CMETA_STATUS jinja_case_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, salts_unicode_case_mode mode, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  const salts_unicode_status case_status = salts_unicode_case_transform(
      string.string, mode, jinja_string_write, &output);
  if (case_status == SALTS_UNICODE_ERR_CALLBACK) status = provider->shared.status;
  else if (case_status != SALTS_UNICODE_OK) status = JINJA_CMETA_ERR_METADATA;
  status = jinja_string_finish(&output, status, result);
  if (status == JINJA_CMETA_OK) result->string_safe = string.string_safe;
  return status;
}

typedef struct JINJA_CMETA_CASE_SKIP_OUTPUT {
  JINJA_CMETA_STRING_OUTPUT *output;
  size_t skip;
} JINJA_CMETA_CASE_SKIP_OUTPUT;

static int jinja_case_count_write(const char *bytes, size_t size, void *opaque) {
  size_t *count = (size_t *)opaque;
  if (bytes == NULL || count == NULL || size > SIZE_MAX - *count) return -1;
  *count += size;
  return 0;
}

static int jinja_case_skip_write(const char *bytes, size_t size, void *opaque) {
  JINJA_CMETA_CASE_SKIP_OUTPUT *skip = (JINJA_CMETA_CASE_SKIP_OUTPUT *)opaque;
  if (bytes == NULL || skip == NULL || skip->output == NULL) return -1;
  if (size <= skip->skip) {
    skip->skip -= size;
    return 0;
  }
  bytes += skip->skip;
  size -= skip->skip;
  skip->skip = 0u;
  return jinja_string_write(bytes, size, skip->output);
}

static JINJA_CMETA_STATUS jinja_capitalize_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  JINJA_CMETA_VALUE string;
  salts_unicode_scalar first = {0};
  size_t cursor = 0u;
  size_t lowered_first_size = 0u;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  while (cursor < string.string.len) {
    if (salts_unicode_utf8_next(string.string, &cursor, &first) != SALTS_UNICODE_OK) {
      status = JINJA_CMETA_ERR_METADATA;
      break;
    }
  }
  cursor = 0u;
  if (status == JINJA_CMETA_OK && string.string.len != 0u &&
      salts_unicode_utf8_next(string.string, &cursor, &first) != SALTS_UNICODE_OK)
    status = JINJA_CMETA_ERR_METADATA;
  if (status == JINJA_CMETA_OK && string.string.len != 0u) {
    const vstr first_input = vstr_from_buf(string.string.data, first.byte_length);
    salts_unicode_status case_status = salts_unicode_case_transform(
        first_input, SALTS_UNICODE_CASE_TITLE, jinja_string_write, &output);
    if (case_status == SALTS_UNICODE_ERR_CALLBACK) status = provider->shared.status;
    else if (case_status != SALTS_UNICODE_OK) status = JINJA_CMETA_ERR_METADATA;
    if (status == JINJA_CMETA_OK) {
      case_status = salts_unicode_case_transform(first_input, SALTS_UNICODE_CASE_LOWER,
          jinja_case_count_write, &lowered_first_size);
      if (case_status != SALTS_UNICODE_OK) status = JINJA_CMETA_ERR_METADATA;
    }
    if (status == JINJA_CMETA_OK) {
      JINJA_CMETA_CASE_SKIP_OUTPUT skip = {.output = &output, .skip = lowered_first_size};
      case_status = salts_unicode_case_transform(
          string.string, SALTS_UNICODE_CASE_LOWER, jinja_case_skip_write, &skip);
      if (case_status == SALTS_UNICODE_ERR_CALLBACK) status = provider->shared.status;
      else if (case_status != SALTS_UNICODE_OK || skip.skip != 0u) status = JINJA_CMETA_ERR_METADATA;
    }
  }
  status = jinja_string_finish(&output, status, result);
  if (status == JINJA_CMETA_OK) result->string_safe = string.string_safe;
  return status;
}

static JINJA_CMETA_STATUS jinja_title_segment(JINJA_CMETA_PROVIDER *provider,
    vstr segment, JINJA_CMETA_STRING_OUTPUT *output) {
  salts_unicode_scalar first = {0};
  size_t cursor = 0u;
  size_t lowered_first_size = 0u;
  if (segment.len == 0u) return JINJA_CMETA_OK;
  if (salts_unicode_utf8_next(segment, &cursor, &first) != SALTS_UNICODE_OK)
    return JINJA_CMETA_ERR_METADATA;
  const vstr first_input = vstr_from_buf(segment.data, first.byte_length);
  salts_unicode_status case_status = salts_unicode_case_transform(
      first_input, SALTS_UNICODE_CASE_UPPER, jinja_string_write, output);
  if (case_status == SALTS_UNICODE_ERR_CALLBACK) return provider->shared.status;
  if (case_status != SALTS_UNICODE_OK) return JINJA_CMETA_ERR_METADATA;
  case_status = salts_unicode_case_transform(first_input, SALTS_UNICODE_CASE_LOWER,
      jinja_case_count_write, &lowered_first_size);
  if (case_status != SALTS_UNICODE_OK) return JINJA_CMETA_ERR_METADATA;
  JINJA_CMETA_CASE_SKIP_OUTPUT skip = {.output = output, .skip = lowered_first_size};
  case_status = salts_unicode_case_transform(segment, SALTS_UNICODE_CASE_LOWER,
      jinja_case_skip_write, &skip);
  if (case_status == SALTS_UNICODE_ERR_CALLBACK) return provider->shared.status;
  return case_status == SALTS_UNICODE_OK && skip.skip == 0u
      ? JINJA_CMETA_OK : JINJA_CMETA_ERR_METADATA;
}

static int jinja_title_separator(salts_unicode_scalar scalar) {
  return scalar.value == '-' || scalar.value == '(' || scalar.value == '{' ||
      scalar.value == '[' || scalar.value == '<' ||
      (scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) != 0u ||
      (scalar.value >= 0x1cu && scalar.value <= 0x1fu);
}

static JINJA_CMETA_STATUS jinja_title_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  JINJA_CMETA_VALUE string;
  size_t cursor = 0u;
  size_t segment_start = 0u;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  while (status == JINJA_CMETA_OK && cursor < string.string.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
      status = JINJA_CMETA_ERR_METADATA;
  }
  cursor = 0u;
  while (status == JINJA_CMETA_OK && cursor < string.string.len) {
    salts_unicode_scalar scalar;
    const size_t scalar_start = cursor;
    if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK)
      status = JINJA_CMETA_ERR_METADATA;
    else if (jinja_title_separator(scalar)) {
      status = jinja_title_segment(provider,
          vstr_from_buf(string.string.data + segment_start, scalar_start - segment_start), &output);
      if (status == JINJA_CMETA_OK &&
          jinja_string_write(string.string.data + scalar_start, cursor - scalar_start, &output) != 0)
        status = provider->shared.status;
      segment_start = cursor;
    }
  }
  if (status == JINJA_CMETA_OK)
    status = jinja_title_segment(provider,
        vstr_from_buf(string.string.data + segment_start, string.string.len - segment_start), &output);
  status = jinja_string_finish(&output, status, result);
  if (status == JINJA_CMETA_OK) result->string_safe = string.string_safe;
  return status;
}

/* Repr may run filtered-loop macros. Each conversion owns its unpublished bytes;
 * a completed child can therefore publish stable views without joining its parent.
 * O(output bytes) copying/temporary storage; all live payload shares one byte budget. */
static JINJA_CMETA_STATUS jinja_concat_values(JINJA_CMETA_PROVIDER *provider,
                                              JINJA_CMETA_VALUE *left, JINJA_CMETA_VALUE *right,
                                              JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  JINJA_CMETA_STATUS status = jinja_concat_operand(&output, left);
  if (status == JINJA_CMETA_OK) status = jinja_concat_operand(&output, right);
  return jinja_string_finish(&output, status, result);
}

typedef struct JINJA_CMETA_FORMAT_SPEC {
  size_t width;
  int precision, left, zero, alternate, safe;
  char conversion, sign;
} JINJA_CMETA_FORMAT_SPEC;

static JINJA_CMETA_STATUS jinja_format_next(const JINJA_CMETA_VALUE *arguments,
    size_t *cursor, JINJA_CMETA_VALUE *value) {
  const size_t count = arguments->kind == JINJA_CMETA_VALUE_TUPLE ? arguments->collection_item_count : 1u;
  if (*cursor == count) return JINJA_CMETA_ERR_RENDER;
  *value = arguments->kind == JINJA_CMETA_VALUE_TUPLE ? arguments->collection_values[*cursor] : *arguments;
  ++*cursor;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_format_dimension(JINJA_CMETA_PROVIDER *provider,
    vstr text, const JINJA_CMETA_VALUE *arguments, size_t *cursor, int64_t *dimension) {
  *dimension = 0;
  if (text.len != 0u && text.data[0] == '*') {
    JINJA_CMETA_VALUE value;
    int valid = 0;
    JINJA_CMETA_STATUS status = jinja_format_next(arguments, cursor, &value);
    if (status == JINJA_CMETA_OK) status = jinja_lookup_integer_key(provider, &value, dimension, &valid);
    return status != JINJA_CMETA_OK ? status : valid ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
  }
  for (size_t i = 0u; i < text.len; ++i) {
    const int digit = text.data[i] - '0';
    if (*dimension > (INT_MAX - digit) / JINJA_CMETA_DECIMAL_RADIX) return JINJA_CMETA_ERR_CAPACITY;
    *dimension = *dimension * JINJA_CMETA_DECIMAL_RADIX + digit;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_format_padding(JINJA_CMETA_STRING_OUTPUT *output,
    char character, size_t count) {
  enum { FORMAT_PADDING_CHUNK = 64 };
  char bytes[FORMAT_PADDING_CHUNK];
  if (!jinja_string_bytes_fit(output->provider, count)) return JINJA_CMETA_ERR_CAPACITY;
  memset(bytes, character, sizeof(bytes));
  while (count != 0u) {
    const size_t chunk = count < sizeof(bytes) ? count : sizeof(bytes);
    if (jinja_string_write(bytes, chunk, output) != 0) return output->provider->shared.status;
    count -= chunk;
  }
  return JINJA_CMETA_OK;
}

typedef struct JINJA_CMETA_FORMAT_TEXT {
  JINJA_CMETA_STRING_OUTPUT *output;
  int escape, ascii;
} JINJA_CMETA_FORMAT_TEXT;

static int jinja_format_text_write(const char *bytes, size_t size, void *opaque) {
  JINJA_CMETA_FORMAT_TEXT *text = (JINJA_CMETA_FORMAT_TEXT *)opaque;
  if (!text->ascii)
    return text->escape ? jinja_html_write(bytes, size, jinja_string_write, text->output)
        : jinja_string_write(bytes, size, text->output);
  vstr rest = vstr_from_buf(bytes, size);
  uint32_t cp;
  while (vstr_utf8_next(&rest, &cp)) {
    enum { ASCII_LIMIT = 128, LATIN1_LIMIT = 256, BMP_LIMIT = 65536, ASCII_ESCAPE_CAPACITY = 11 };
    char escaped[ASCII_ESCAPE_CAPACITY];
    size_t length;
    if (cp < ASCII_LIMIT) { escaped[0] = (char)cp; length = 1u; }
    else length = (size_t)snprintf(escaped, sizeof(escaped),
        cp < LATIN1_LIMIT ? "\\x%02x" : cp < BMP_LIMIT ? "\\u%04x" : "\\U%08x", (unsigned)cp);
    int written = text->escape ? jinja_html_write(escaped, length, jinja_string_write, text->output)
        : jinja_string_write(escaped, length, text->output);
    if (written != 0) return -1;
  }
  return 0;
}

/* Markup's decimal/float conversions use int()/float(); ordinary % conversions
 * require numeric values. No int-filter default or float fallback applies here. */
static JINJA_CMETA_STATUS jinja_format_number(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_FORMAT_SPEC *spec, const JINJA_CMETA_VALUE *value, JINJA_CMETA_SCALAR *number) {
  const int decimal = strchr("diu", spec->conversion) != NULL;
  const int integral = decimal || strchr("oxX", spec->conversion) != NULL;
  JINJA_CMETA_STATUS status = jinja_scalar_from_value(provider, value, number);
  if (status != JINJA_CMETA_OK) return status;
  if (spec->safe && integral && !decimal) return JINJA_CMETA_ERR_RENDER;
  if (number->kind == JINJA_CMETA_SCALAR_STRING && spec->safe) {
    JINJA_CMETA_NUMERIC_TEXT text;
    status = jinja_numeric_text(provider, number, &text);
    if (status != JINJA_CMETA_OK) return status;
    if (text.data == NULL) return JINJA_CMETA_ERR_RENDER;
    int valid = 0;
    if (decimal) {
      status = jinja_integer_text(text.data, text.len, JINJA_CMETA_DECIMAL_RADIX, &number->sint, &valid);
      number->kind = JINJA_CMETA_SCALAR_SINT;
    } else {
      valid = jinja_float_parse_text(text.data, text.len, &number->floating);
      number->kind = JINJA_CMETA_SCALAR_FLOAT;
    }
    jinja_cmeta_memory_drop(text.data);
    if (status != JINJA_CMETA_OK || !valid) return status != JINJA_CMETA_OK ? status : JINJA_CMETA_ERR_RENDER;
  }
  if (!jinja_scalar_is_numeric(number->kind)) return JINJA_CMETA_ERR_RENDER;
  if (integral && number->kind == JINJA_CMETA_SCALAR_FLOAT) {
    if (!decimal) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_VALUE converted;
    status = jinja_float_to_integer(number->floating, &converted);
    if (status != JINJA_CMETA_OK) return status;
    number->kind = JINJA_CMETA_SCALAR_SINT;
    number->sint = converted.integer;
  }
  return JINJA_CMETA_OK;
}

/* Field scratch is call-local; the assembled format output is published once.
 * Scanning/padding is linear in output bytes, plus existing dictionary lookup. */
static JINJA_CMETA_STATUS jinja_format_field(JINJA_CMETA_STRING_OUTPUT *output,
    const JINJA_CMETA_FORMAT_SPEC *spec, JINJA_CMETA_VALUE *value) {
  enum { FORMAT_DIGITS_CAPACITY = 3 * sizeof(uint64_t) + 1, FORMAT_PREFIX_CAPACITY = 3,
         FORMAT_DEFAULT_PRECISION = 6 };
  JINJA_CMETA_PROVIDER *provider = output->provider;
  JINJA_CMETA_STRING_OUTPUT scratch = {.provider = provider};
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  char digits[FORMAT_DIGITS_CAPACITY], prefix[FORMAT_PREFIX_CAPACITY];
  size_t prefix_length = 0u, zeros = 0u;
  vstr content = {0};
  const int numeric = strchr("diuoxXeEfFgG", spec->conversion) != NULL;
  if (!numeric) {
    if (spec->conversion == 'c') {
      if (spec->safe) { status = JINJA_CMETA_ERR_RENDER; goto cleanup; }
      JINJA_CMETA_SCALAR scalar;
      status = jinja_scalar_from_value(provider, value, &scalar);
      if (status != JINJA_CMETA_OK) goto cleanup;
      if (scalar.kind == JINJA_CMETA_SCALAR_STRING) {
        content = vstr_from_buf((const char *)scalar.data, scalar.size);
        if (vstr_utf8_len(content) != 1u) { status = JINJA_CMETA_ERR_RENDER; goto cleanup; }
      } else {
        int valid;
        int64_t cp;
        status = jinja_lookup_integer_key(provider, value, &cp, &valid);
        if (status != JINJA_CMETA_OK) goto cleanup;
        if (!valid || cp < 0 || cp > UINT32_MAX || tstr_utf8_codepoint_size((uint32_t)cp) == 0u) {
          status = JINJA_CMETA_ERR_RENDER; goto cleanup;
        }
        if (!jinja_string_bytes_fit(provider, tstr_utf8_codepoint_size((uint32_t)cp))) {
          status = JINJA_CMETA_ERR_CAPACITY; goto cleanup;
        }
        /* The legacy scalar encoder still owns an unmetered temporary here. */
        tstr encoded = tstr_utf8_from_cp((uint32_t)cp);
        if (encoded == NULL) { status = JINJA_CMETA_ERR_OUT_OF_MEMORY; goto cleanup; }
        const int written = jinja_string_write(encoded, tstr_len(encoded), &scratch);
        tstr_free(encoded);
        if (written != 0) { status = provider->shared.status; goto cleanup; }
      }
    } else {
      JINJA_CMETA_FORMAT_TEXT text = {.output = &scratch,
          .escape = spec->safe && (spec->conversion != 's' || !jinja_value_is_safe(value)),
          .ascii = spec->conversion == 'a'};
      if (spec->conversion == 's') {
        JINJA_CMETA_VALUE string;
        status = jinja_string_value(provider, value, &string);
        if (status != JINJA_CMETA_OK) goto cleanup;
        if (!text.escape) content = string.string;
        else if (jinja_format_text_write(string.string.data, string.string.len, &text) != 0) status = provider->shared.status;
      } else status = jinja_dump_value_repr(provider, value, 0u, jinja_format_text_write, &text);
      if (status != JINJA_CMETA_OK) goto cleanup;
    }
    if (scratch.bytes.bytes != NULL) content = vstr_from_buf(jinja_cmeta_text_data(&scratch.bytes), jinja_cmeta_text_length(&scratch.bytes));
    if (spec->precision >= 0 && spec->conversion != 'c') content = vstr_utf8_sub(content, 0u, (size_t)spec->precision);
  } else {
    JINJA_CMETA_SCALAR number;
    status = jinja_format_number(provider, spec, value, &number);
    if (status != JINJA_CMETA_OK) goto cleanup;
    int negative;
    if (strchr("diuoxX", spec->conversion) != NULL) {
      negative = number.kind == JINJA_CMETA_SCALAR_SINT && number.sint < 0;
      uint64_t magnitude = number.kind == JINJA_CMETA_SCALAR_UINT ? number.uint :
          number.kind == JINJA_CMETA_SCALAR_BOOL ? number.boolean != 0 : (uint64_t)number.sint;
      if (negative) magnitude = UINT64_C(0) - magnitude;
      const unsigned long long printable = (unsigned long long)magnitude;
      const int length = spec->conversion == 'o' ? snprintf(digits, sizeof(digits), "%llo", printable) :
          spec->conversion == 'x' ? snprintf(digits, sizeof(digits), "%llx", printable) :
          spec->conversion == 'X' ? snprintf(digits, sizeof(digits), "%llX", printable) :
          snprintf(digits, sizeof(digits), "%llu", printable);
      content = vstr_from_buf(digits, (size_t)length);
      if (spec->precision > length) zeros = (size_t)(spec->precision - length);
    } else {
      double floating = number.kind == JINJA_CMETA_SCALAR_FLOAT ? number.floating :
          number.kind == JINJA_CMETA_SCALAR_SINT ? (double)number.sint :
          number.kind == JINJA_CMETA_SCALAR_UINT ? (double)number.uint : (double)(number.boolean != 0);
      const int precision = spec->precision < 0 ? FORMAT_DEFAULT_PRECISION : spec->precision;
      if ((size_t)precision > provider->shared.max_string_bytes) { status = JINJA_CMETA_ERR_CAPACITY; goto cleanup; }
      int length = jinja_float_format_spec(floating, spec->conversion, spec->alternate, precision, NULL, 0u);
      if (length < 0) { status = JINJA_CMETA_ERR_RENDER; goto cleanup; }
      if (!jinja_string_bytes_fit(provider, (size_t)length) ||
          !jinja_cmeta_string_append_fits(0u, (size_t)length, SIZE_MAX)) {
        status = JINJA_CMETA_ERR_CAPACITY; goto cleanup;
      }
      status = jinja_cmeta_text_init(&scratch.bytes, provider->shared.memory);
      if (status == JINJA_CMETA_OK)
        status = jinja_cmeta_text_resize(&scratch.bytes, (size_t)length);
      if (status != JINJA_CMETA_OK) goto cleanup;
      provider->shared.pending_string_bytes += (size_t)length;
      if (jinja_float_format_spec(floating, spec->conversion, spec->alternate, precision,
          jinja_cmeta_text_mutable(&scratch.bytes), (size_t)length + 1u) < 0) { status = JINJA_CMETA_ERR_RENDER; goto cleanup; }
      negative = jinja_cmeta_text_data(&scratch.bytes)[0] == '-';
      content = vstr_from_buf(jinja_cmeta_text_data(&scratch.bytes) + negative, (size_t)length - (size_t)negative);
    }
    if (negative || spec->sign != 0) prefix[prefix_length++] = negative ? '-' : spec->sign;
    if (spec->alternate && strchr("oxX", spec->conversion) != NULL) {
      prefix[prefix_length++] = '0';
      prefix[prefix_length++] = spec->conversion;
    }
  }
  size_t characters = numeric ? content.len : vstr_utf8_len(content);
  if (characters == SIZE_MAX) { status = JINJA_CMETA_ERR_METADATA; goto cleanup; }
  const size_t length = prefix_length + zeros + characters;
  const size_t padding = spec->width > length ? spec->width - length : 0u;
  if (!spec->left && (!numeric || !spec->zero)) status = jinja_format_padding(output, ' ', padding);
  if (status == JINJA_CMETA_OK && jinja_string_write(prefix, prefix_length, output) != 0) status = provider->shared.status;
  if (status == JINJA_CMETA_OK)
    status = jinja_format_padding(output, '0', zeros + (numeric && spec->zero && !spec->left ? padding : 0u));
  if (status == JINJA_CMETA_OK && jinja_string_write(content.data, content.len, output) != 0) status = provider->shared.status;
  if (status == JINJA_CMETA_OK && spec->left) status = jinja_format_padding(output, ' ', padding);
cleanup:
  provider->shared.pending_string_bytes -= jinja_cmeta_text_length(&scratch.bytes);
  jinja_cmeta_text_destroy(&scratch.bytes);
  return status;
}

static JINJA_CMETA_STATUS jinja_format_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *format, const JINJA_CMETA_VALUE *arguments, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, format, &string);
  if (status != JINJA_CMETA_OK) return status;
  const vstr text = string.string;
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  size_t start = 0u, argument = 0u;
  for (size_t cursor = 0u; cursor < text.len && status == JINJA_CMETA_OK;) {
    if (text.data[cursor++] != '%') continue;
    if (jinja_string_write(text.data + start, cursor - start - 1u, &output) != 0) { status = provider->shared.status; break; }
    if (cursor < text.len && text.data[cursor] == '%') {
      if (jinja_string_write("%", 1u, &output) != 0) { status = provider->shared.status; break; }
      start = ++cursor;
      continue;
    }
    JINJA_TEXT_FORMAT field;
    if (!jinja_text_format(text, &cursor, &field)) { status = JINJA_CMETA_ERR_RENDER; break; }
    JINJA_CMETA_FORMAT_SPEC spec = {.precision = -1, .conversion = field.conversion, .safe = string.string_safe};
    spec.left = vstr_find_char(field.flags, '-') != SIZE_MAX;
    spec.zero = vstr_find_char(field.flags, '0') != SIZE_MAX;
    spec.alternate = vstr_find_char(field.flags, '#') != SIZE_MAX;
    spec.sign = vstr_find_char(field.flags, '+') != SIZE_MAX ? '+' :
        vstr_find_char(field.flags, ' ') != SIZE_MAX ? ' ' : 0;
    const int star = (field.width.len != 0u && field.width.data[0] == '*') ||
        (field.precision.len != 0u && field.precision.data[0] == '*');
    if (star && (spec.safe || field.key.data != NULL)) { status = JINJA_CMETA_ERR_RENDER; break; }
    int64_t width = 0, precision = 0;
    status = jinja_format_dimension(provider, field.width, arguments, &argument, &width);
    if (status == JINJA_CMETA_OK) status = jinja_format_dimension(provider, field.precision, arguments, &argument, &precision);
    if (status != JINJA_CMETA_OK) break;
    if (width < -INT_MAX || width > INT_MAX || precision > INT_MAX) { status = JINJA_CMETA_ERR_CAPACITY; break; }
    spec.left |= width < 0;
    spec.width = (size_t)(width < 0 ? -width : width);
    if (field.precision.data != NULL) spec.precision = precision < 0 ? 0 : (int)precision;
    JINJA_CMETA_VALUE value;
    if (field.key.data != NULL) {
      JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_STRING, .string = field.key};
      int found;
      status = jinja_dict_lookup(provider, arguments, &key, 0u, &value, &found);
      if (status == JINJA_CMETA_OK && !found) status = JINJA_CMETA_ERR_RENDER;
      argument = 1u;
    } else status = jinja_format_next(arguments, &argument, &value);
    if (status == JINJA_CMETA_OK) status = jinja_format_field(&output, &spec, &value);
    start = cursor;
  }
  /* Non-tuple subscriptable arguments can remain unused, e.g. "%%" % []. */
  if (status == JINJA_CMETA_OK && arguments->kind != JINJA_CMETA_VALUE_DICT &&
      arguments->kind != JINJA_CMETA_VALUE_LIST && arguments->kind != JINJA_CMETA_VALUE_RANGE &&
      argument != (arguments->kind == JINJA_CMETA_VALUE_TUPLE ? arguments->collection_item_count : 1u))
    status = JINJA_CMETA_ERR_RENDER;
  if (status == JINJA_CMETA_OK && start < text.len &&
      jinja_string_write(text.data + start, text.len - start, &output) != 0) status = provider->shared.status;
  status = jinja_string_finish(&output, status, result);
  if (status == JINJA_CMETA_OK) result->string_safe = string.string_safe;
  return status;
}

static JINJA_CMETA_STATUS jinja_url_quote(JINJA_CMETA_STRING_OUTPUT *output,
    JINJA_CMETA_VALUE *value, int query) {
  static const char hex[] = "0123456789ABCDEF";
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_STATUS status = jinja_string_value(output->provider, value, &string);
  if (status != JINJA_CMETA_OK) return status;
  const vstr text = string.string;
  if (text.len > output->provider->shared.max_string_bytes) return JINJA_CMETA_ERR_CAPACITY;
  if (vstr_utf8_invalid_offset(text) != SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
  size_t start = 0u;
  for (size_t i = 0u; i < text.len; ++i) {
    unsigned char c = (unsigned char)text.data[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~' || (!query && c == '/')) continue;
    if (i != start && jinja_string_write(text.data + start, i - start, output) != 0)
      return output->provider->shared.status;
    char encoded[] = {'%', hex[c >> 4u], hex[c & 0x0fu]};
    const int space = query && c == ' ';
    if (jinja_string_write(space ? "+" : encoded, space ? 1u : sizeof(encoded), output) != 0)
      return output->provider->shared.status;
    start = i + 1u;
  }
  if (start != text.len && jinja_string_write(text.data + start, text.len - start, output) != 0)
    return output->provider->shared.status;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_urlencode_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, JINJA_CMETA_VALUE *result) {
  enum { PAIR_SIZE = 2, PAIR_LOOKAHEAD = PAIR_SIZE + 1 };
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  jinja_normalize_call_argument(operand);
  int iterable;
  JINJA_CMETA_STATUS status = jinja_value_test(provider, operand, JINJA_CMETA_TEST_ITERABLE, &iterable);
  if (status != JINJA_CMETA_OK) return status;
  if (jinja_value_is_string(operand) || !iterable) {
    status = jinja_url_quote(&output, operand, 0);
  } else {
    JINJA_CMETA_ITERATION source = {.input = *operand, .max_items = provider->shared.node_capacity};
    if (operand->kind == JINJA_CMETA_VALUE_DICT)
      status = jinja_create_iterator(provider, operand, JINJA_CMETA_ITERATOR_ITEMS, &source.input);
    while (status == JINJA_CMETA_OK) {
      JINJA_CMETA_VALUE item, parts[PAIR_LOOKAHEAD];
      int present;
      status = jinja_iteration_next(provider, &source, &item, &present);
      if (status != JINJA_CMETA_OK || !present) break;
      JINJA_CMETA_ITERATION pair = {.input = item, .max_items = PAIR_LOOKAHEAD};
      for (size_t i = 0u; i < PAIR_LOOKAHEAD; ++i) {
        status = jinja_iteration_next(provider, &pair, &parts[i], &present);
        if (status != JINJA_CMETA_OK) break;
        if (present != (i < PAIR_SIZE)) { status = JINJA_CMETA_ERR_RENDER; break; }
      }
      if (status != JINJA_CMETA_OK) break;
      if (source.position > 1u && jinja_string_write("&", 1u, &output) != 0) {
        status = provider->shared.status;
        break;
      }
      status = jinja_url_quote(&output, &parts[0], 1);
      if (status == JINJA_CMETA_OK && jinja_string_write("=", 1u, &output) != 0) status = provider->shared.status;
      if (status == JINJA_CMETA_OK) status = jinja_url_quote(&output, &parts[1], 1);
    }
  }
  return jinja_string_finish(&output, status, result);
}

static JINJA_CMETA_STATUS jinja_xmlattr_string(JINJA_CMETA_STRING_OUTPUT *output,
    JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_VALUE string;
  JINJA_CMETA_STATUS status = jinja_string_value(output->provider, value, &string);
  if (status != JINJA_CMETA_OK) return status;
  int written = string.string_safe
      ? jinja_string_write(string.string.data, string.string.len, output)
      : jinja_html_write(string.string.data, string.string.len, jinja_string_write, output);
  return written == 0 ? JINJA_CMETA_OK : output->provider->shared.status;
}

static JINJA_CMETA_STATUS jinja_xmlattr_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *autospace, JINJA_CMETA_VALUE *result) {
  jinja_normalize_call_argument(operand);
  if (operand->kind != JINJA_CMETA_VALUE_DICT) return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  JINJA_CMETA_ITERATION source = {.max_items = provider->shared.node_capacity};
  JINJA_CMETA_STATUS status = jinja_create_iterator(provider, operand,
      JINJA_CMETA_ITERATOR_ITEMS, &source.input);
  int space = 0;
  while (status == JINJA_CMETA_OK) {
    JINJA_CMETA_VALUE pair, key, value, name;
    int present;
    status = jinja_iteration_next(provider, &source, &pair, &present);
    if (status != JINJA_CMETA_OK || !present) break;
    key = pair.collection_values[0];
    value = pair.collection_values[1];
    jinja_normalize_call_argument(&value);
    if (value.kind == JINJA_CMETA_VALUE_NONE || value.kind == JINJA_CMETA_VALUE_UNDEFINED) continue;
    if (!jinja_value_is_string(&key)) { status = JINJA_CMETA_ERR_RENDER; break; }
    status = jinja_string_value(provider, &key, &name);
    if (status != JINJA_CMETA_OK) break;
    /* The upstream attribute-name check is ASCII, not Unicode whitespace. */
    for (size_t i = 0u; i < name.string.len; ++i) {
      const unsigned char c = (unsigned char)name.string.data[i];
      if (c == ' ' || (c >= '\t' && c <= '\r') || c == '/' || c == '>' || c == '=') {
        status = JINJA_CMETA_ERR_RENDER;
        break;
      }
    }
    if (status != JINJA_CMETA_OK) break;
    if (space && jinja_string_write(" ", 1u, &output) != 0) { status = provider->shared.status; break; }
    status = jinja_xmlattr_string(&output, &name);
    if (status == JINJA_CMETA_OK && jinja_string_write("=\"", 2u, &output) != 0) status = provider->shared.status;
    if (status == JINJA_CMETA_OK) status = jinja_xmlattr_string(&output, &value);
    if (status == JINJA_CMETA_OK && jinja_string_write("\"", 1u, &output) != 0) status = provider->shared.status;
    space = 1;
  }
  /* Truth testing a loop may execute filter macros, so it follows all value conversions. */
  int prefix = 1;
  if (status == JINJA_CMETA_OK && autospace != NULL)
    status = jinja_value_truthy(provider, autospace, &prefix);
  const size_t size = jinja_cmeta_text_length(&output.bytes);
  if (status == JINJA_CMETA_OK && prefix && size != 0u) {
    if (jinja_string_write(" ", 1u, &output) != 0) status = provider->shared.status;
    else {
      memmove(jinja_cmeta_text_mutable(&output.bytes) + 1u, jinja_cmeta_text_data(&output.bytes), size);
      jinja_cmeta_text_mutable(&output.bytes)[0] = ' ';
    }
  }
  status = jinja_string_finish(&output, status, result);
  if (status == JINJA_CMETA_OK) result->string_safe = provider->autoescape;
  return status;
}

typedef struct JINJA_CMETA_WORDWRAP {
  size_t width;
  int integral_width;
  int chunk_hyphens;
  JINJA_CMETA_VALUE separator;
  const JINJA_CMETA_VALUE *break_long_words;
  const JINJA_CMETA_VALUE *break_on_hyphens;
} JINJA_CMETA_WORDWRAP;

static JINJA_CMETA_STATUS jinja_wordwrap_paragraph(JINJA_CMETA_STRING_OUTPUT *output,
    vstr paragraph, const JINJA_CMETA_WORDWRAP *wrap) {
  JINJA_CMETA_PROVIDER *provider = output->provider;
  JINJA_TEXT_WRAP_SCAN scan = {.input = paragraph, .hyphens = wrap->chunk_hyphens};
  JINJA_TEXT_CHUNK chunk;
  int present = jinja_text_wrap_next(&scan, &chunk), emitted = 0;
  while (present) {
    if (emitted && chunk.content_end == 0u) present = jinja_text_wrap_next(&scan, &chunk);
    if (!present) break;
    const char *begin = chunk.text.data, *end = begin, *last = begin;
    size_t used = 0u, chunks = 0u;
    int blank = 0;
    while (present && chunk.characters <= wrap->width - used) {
      last = chunk.text.data;
      end = last + chunk.text.len;
      blank = chunk.content_end == 0u;
      used += chunk.characters;
      ++chunks;
      present = jinja_text_wrap_next(&scan, &chunk);
    }
    if (present && chunk.characters > wrap->width) {
      int split = 1, hyphens = 1;
      JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
      if (wrap->break_long_words != NULL)
        status = jinja_value_truthy(provider, wrap->break_long_words, &split);
      if (status != JINJA_CMETA_OK) return status;
      if (split) {
        if (!wrap->integral_width && wrap->width != 0u) return JINJA_CMETA_ERR_RENDER;
        if (wrap->break_on_hyphens != NULL)
          status = jinja_value_truthy(provider, wrap->break_on_hyphens, &hyphens);
        if (status != JINJA_CMETA_OK) return status;
        /* A positive fractional width below one uses textwrap's one-character step. */
        const size_t available = wrap->width == 0u ? 1u : wrap->width - used;
        JINJA_TEXT_CHUNK prefix = jinja_text_wrap_split(&chunk, available, hyphens);
        last = prefix.text.data;
        end = last + prefix.text.len;
        blank = prefix.content_end == 0u;
        ++chunks;
      } else if (chunks == 0u) {
        last = chunk.text.data;
        end = last + chunk.text.len;
        blank = chunk.content_end == 0u;
        ++chunks;
        present = jinja_text_wrap_next(&scan, &chunk);
      }
    }
    /* textwrap removes one final blank chunk, not all trailing whitespace. */
    if (chunks != 0u && blank) { end = last; --chunks; }
    if (chunks == 0u) continue;
    if (emitted && jinja_string_write(wrap->separator.string.data,
        wrap->separator.string.len, output) != 0) return provider->shared.status;
    size_t bytes = (size_t)(end - begin);
    if ((wrap->separator.string_safe
          ? jinja_html_write(begin, bytes, jinja_string_write, output)
          : jinja_string_write(begin, bytes, output)) != 0) return provider->shared.status;
    emitted = 1;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_wordwrap_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_VALUE *operand, const JINJA_CMETA_VALUE *const parameters[], JINJA_CMETA_VALUE *result) {
  enum { WIDTH, BREAK_LONG_WORDS, WRAPSTRING, BREAK_ON_HYPHENS, DEFAULT_WIDTH = 79 };
  JINJA_CMETA_VALUE string;
  if (!jinja_value_is_string(operand)) return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_STATUS status = jinja_string_value(provider, operand, &string);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_WORDWRAP wrap = {.width = DEFAULT_WIDTH, .integral_width = 1, .chunk_hyphens = 1,
      .separator = {.kind = JINJA_CMETA_VALUE_STRING, .string = vstr_from_cstr(provider->instance->templ->newline_sequence)},
      .break_long_words = parameters[BREAK_LONG_WORDS], .break_on_hyphens = parameters[BREAK_ON_HYPHENS]};
  if (parameters[WRAPSTRING] != NULL && parameters[WRAPSTRING]->kind != JINJA_CMETA_VALUE_NONE) {
    JINJA_CMETA_VALUE separator = *parameters[WRAPSTRING];
    if (!jinja_value_is_string(&separator)) return JINJA_CMETA_ERR_RENDER;
    status = jinja_string_value(provider, &separator, &wrap.separator);
    if (status != JINJA_CMETA_OK) return status;
  }
  /* Empty input has no paragraphs, so width and break policies are never used. */
  if (string.string.len != 0u && parameters[WIDTH] != NULL) {
    JINJA_CMETA_NUMBER width;
    status = jinja_number_from_value(provider, parameters[WIDTH], &width);
    if (status != JINJA_CMETA_OK) return status;
    wrap.integral_width = width.kind == JINJA_CMETA_NUMBER_INTEGER;
    if (wrap.integral_width) {
      if (width.integer <= 0) return JINJA_CMETA_ERR_RENDER;
      wrap.width = (uint64_t)width.integer > SIZE_MAX ? SIZE_MAX : (size_t)width.integer;
    } else {
      if (!(width.floating > 0.0)) return JINJA_CMETA_ERR_RENDER;
      wrap.width = width.floating >= (double)SIZE_MAX ? SIZE_MAX : (size_t)width.floating;
    }
  }
  if (string.string.len != 0u && wrap.break_on_hyphens != NULL) {
    status = jinja_value_test(provider, wrap.break_on_hyphens, JINJA_CMETA_TEST_TRUE, &wrap.chunk_hyphens);
    if (status != JINJA_CMETA_OK) return status;
  }
  JINJA_CMETA_STRING_OUTPUT output = {.provider = provider};
  size_t cursor = 0u;
  while (cursor < string.string.len && status == JINJA_CMETA_OK) {
    const size_t begin = cursor;
    size_t end = cursor;
    while (cursor < string.string.len) {
      salts_unicode_scalar scalar;
      if (salts_unicode_utf8_next(string.string, &cursor, &scalar) != SALTS_UNICODE_OK) {
        status = JINJA_CMETA_ERR_METADATA;
        break;
      }
      if (jinja_text_linebreak(scalar.value)) {
        if (scalar.value == '\r' && cursor < string.string.len && string.string.data[cursor] == '\n') ++cursor;
        break;
      }
      end = cursor;
    }
    if (status != JINJA_CMETA_OK) break;
    if (begin != 0u && jinja_string_write(wrap.separator.string.data, wrap.separator.string.len, &output) != 0) {
      status = provider->shared.status;
      break;
    }
    status = jinja_wordwrap_paragraph(&output, vstr_from_buf(string.string.data + begin, end - begin), &wrap);
  }
  status = jinja_string_finish(&output, status, result);
  if (status == JINJA_CMETA_OK) result->string_safe = wrap.separator.string_safe;
  return status;
}

static JINJA_CMETA_STATUS jinja_read_float(const JINJA_CMETA_NODE *node, double *out) {
  const cmeta_data_float_shape *shape = (const cmeta_data_float_shape *)node->desc->shape;
  if (shape == NULL || node->desc->storage_type == NULL ||
      shape->bits != node->desc->storage_type->size * CHAR_BIT)
    return JINJA_CMETA_ERR_METADATA;
  if (shape->bits == 32u) {
    float value;
    memcpy(&value, node->object, sizeof(value));
    *out = value;
    return JINJA_CMETA_OK;
  }
  if (shape->bits == 64u) {
    memcpy(out, node->object, sizeof(*out));
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_METADATA;
}

static int jinja_dump(void *opaque_node, int (*out_fn)(const char *, size_t, void *),
                      void *renderer_data, void *provider_data) {
  JINJA_CMETA_NODE *node = (JINJA_CMETA_NODE *)opaque_node;
  JINJA_CMETA_PROVIDER *provider = (JINJA_CMETA_PROVIDER *)provider_data;
  char buffer[64];
  int written = 0;

  if (node == NULL || out_fn == NULL || provider->shared.status != JINJA_CMETA_OK) return -1;
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_CYCLER ||
      node->expression_kind == JINJA_CMETA_EXPRESSION_JOINER) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return -1;
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_SELF) {
    return out_fn(jinja_template_reference_repr, sizeof(jinja_template_reference_repr) - 1u, renderer_data);
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_MODULE) {
    if (node->instance == NULL || node->instance->module_state != JINJA_CMETA_MODULE_READY) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
      return -1;
    }
    return out_fn(node->instance->module_body.data, node->instance->module_body.len, renderer_data);
  }
  if (node->is_loop_object || node->is_missing) {
    JINJA_CMETA_VALUE value = {.kind = node->is_missing ? JINJA_CMETA_VALUE_MISSING : JINJA_CMETA_VALUE_LOOP,
        .loop = node->loop_receiver};
    JINJA_CMETA_STATUS status = jinja_dump_value_repr(provider, &value, 0u, out_fn, renderer_data);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK ? 0 : -1;
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_NAMESPACE ||
      node->expression_kind == JINJA_CMETA_EXPRESSION_CALL) {
    JINJA_CMETA_VALUE value = {.kind = node->expression_kind == JINJA_CMETA_EXPRESSION_CALL
        ? JINJA_CMETA_VALUE_CALLABLE : JINJA_CMETA_VALUE_NAMESPACE,
        .callable_kind = node->callable_kind, .namespace_dict = node->namespace_dict,
        .closure = node->closure, .host_callable = node->host_callable};
    JINJA_CMETA_STATUS status = jinja_dump_value_repr(provider, &value, 0u, out_fn, renderer_data);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK ? 0 : -1;
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_ITEMS) {
    /* Object repr policy is tracked by issue #26; never silently emit nothing. */
    jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return -1;
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_RANGE) {
    JINJA_CMETA_VALUE value = {.kind = JINJA_CMETA_VALUE_RANGE, .range = node->range};
    JINJA_CMETA_STATUS status = jinja_dump_value_repr(provider, &value, 0u, out_fn, renderer_data);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK ? 0 : -1;
  }
  if (jinja_expression_is_container(node->expression_kind)) {
    JINJA_CMETA_VALUE value = {
        .identity = node->value_identity,
        .kind = node->expression_kind == JINJA_CMETA_EXPRESSION_LIST    ? JINJA_CMETA_VALUE_LIST
                : node->expression_kind == JINJA_CMETA_EXPRESSION_TUPLE ? JINJA_CMETA_VALUE_TUPLE
                                                                        : JINJA_CMETA_VALUE_DICT,
        .first_collection_item = node->first_collection_item,
        .collection_values = node->collection_values,
        .collection_item_count = node->collection_item_count};
    JINJA_CMETA_STATUS status = jinja_dump_value_repr(provider, &value, 0u, out_fn, renderer_data);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK ? 0 : -1;
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_BOOL && node->desc->kind == CMETA_DATA_BOOL) {
    bool value;
    memcpy(&value, node->object, sizeof(value));
    return out_fn(value ? "True" : "False", value ? 4u : 5u, renderer_data);
  }
  switch (node->desc->kind) {
  case CMETA_DATA_BOOL: {
    bool value;
    memcpy(&value, node->object, sizeof(value));
    return out_fn(value ? "true" : "false", value ? 4u : 5u, renderer_data);
  }
  case CMETA_DATA_SINT: {
    int64_t value;
    JINJA_CMETA_STATUS status = jinja_read_signed(node, &value);
    if (status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, status);
      return -1;
    }
    written = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    break;
  }
  case CMETA_DATA_UINT: {
    uint64_t value;
    JINJA_CMETA_STATUS status = jinja_read_unsigned(node, &value);
    if (status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, status);
      return -1;
    }
    written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    break;
  }
  case CMETA_DATA_FLOAT: {
    double value;
    size_t size;
    JINJA_CMETA_STATUS status = jinja_read_float(node, &value);
    if (status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, status);
      return -1;
    }
    if (!jinja_float_format(value, buffer, sizeof(buffer), &size)) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
      return -1;
    }
    written = (int)size;
    break;
  }
  case CMETA_DATA_STRING:
  case CMETA_DATA_BYTES: {
    const unsigned char *data = NULL;
    size_t size = 0u;
    cmeta_status status =
        cmeta_data_buffer_read(node->desc, node->object, provider->shared.max_string_bytes, &data, &size);
    if (status != CMETA_OK) {
      jinja_provider_fail(provider, status == CMETA_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY
                                                                      : JINJA_CMETA_ERR_METADATA);
      return -1;
    }
    if (size == 0u) return 0;
    if (out_fn((const char *)data, size, renderer_data) != 0) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
      return -1;
    }
    return 0;
  }
  case CMETA_DATA_ENUM: {
    const cmeta_data_enum_shape *shape = (const cmeta_data_enum_shape *)node->desc->shape;
    int64_t value;
    const char *text;
    if (cmeta_data_enum_read(node->desc, node->object, &value) != CMETA_OK || shape == NULL ||
        (text = cmeta_enum_to_string(shape->meta, value)) == NULL) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
      return -1;
    }
    return out_fn(text, strlen(text), renderer_data);
  }
  case CMETA_DATA_STRUCT:
  case CMETA_DATA_CUSTOM:
    return 0;
  default:
    jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
    return -1;
  }

  if (written < 0 || (size_t)written >= sizeof(buffer)) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return -1;
  }
  if (out_fn(buffer, (size_t)written, renderer_data) != 0) {
    jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
    return -1;
  }
  return 0;
}

static int jinja_truthy(JINJA_CMETA_PROVIDER *provider, const JINJA_CMETA_NODE *node) {
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_SELF ||
      node->expression_kind == JINJA_CMETA_EXPRESSION_MODULE ||
      node->expression_kind == JINJA_CMETA_EXPRESSION_CYCLER ||
      node->expression_kind == JINJA_CMETA_EXPRESSION_JOINER) return 1;
  if (node->is_loop_object) {
    JINJA_CMETA_VALUE length;
    JINJA_CMETA_STATUS status = jinja_loop_attribute(provider, node->loop_receiver, vstr_from_cstr("length"), &length);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK && length.integer != 0;
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_ITEMS) return 1;
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_RANGE) return node->range.count != 0u;
  if (jinja_expression_is_collection(node->expression_kind))
    return node->collection_item_count != 0u;
  switch (node->desc->kind) {
  case CMETA_DATA_BOOL: {
    bool value;
    memcpy(&value, node->object, sizeof(value));
    return value != false;
  }
  case CMETA_DATA_SINT: {
    int64_t value;
    JINJA_CMETA_STATUS status = jinja_read_signed(node, &value);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK && value != 0;
  }
  case CMETA_DATA_UINT: {
    uint64_t value;
    JINJA_CMETA_STATUS status = jinja_read_unsigned(node, &value);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK && value != 0u;
  }
  case CMETA_DATA_FLOAT: {
    double value;
    JINJA_CMETA_STATUS status = jinja_read_float(node, &value);
    if (status != JINJA_CMETA_OK) jinja_provider_fail(provider, status);
    return status == JINJA_CMETA_OK && value != 0.0;
  }
  case CMETA_DATA_STRING:
  case CMETA_DATA_BYTES: {
    const unsigned char *data = NULL;
    size_t size = 0u;
    cmeta_status status =
        cmeta_data_buffer_read(node->desc, node->object, provider->shared.max_string_bytes, &data, &size);
    if (status != CMETA_OK)
      jinja_provider_fail(provider, status == CMETA_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY
                                                                      : JINJA_CMETA_ERR_METADATA);
    return status == CMETA_OK && size != 0u;
  }
  case CMETA_DATA_ENUM: {
    int64_t value;
    cmeta_status status = cmeta_data_enum_read(node->desc, node->object, &value);
    if (status != CMETA_OK) jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
    return status == CMETA_OK && value != 0;
  }
  case CMETA_DATA_STRUCT:
    return 1;
  case CMETA_DATA_CUSTOM:
    if (jinja_is_sequence_desc(node->desc))
      return ((const JINJA_CMETA_SEQUENCE_VIEW *)node->object)->count != 0u;
    jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
    return 0;
  default:
    jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
    return 0;
  }
}

static JINJA_CMETA_NODE *jinja_iteration_child_at(JINJA_CMETA_NODE *node, unsigned index,
                                                 JINJA_CMETA_PROVIDER *provider) {
  if (node == NULL || provider->shared.status != JINJA_CMETA_OK) return NULL;
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_ITEMS ||
      (node->loop_state != NULL && node->loop_state->source != NULL)) {
    JINJA_CMETA_STATUS status = jinja_iterator_cache_until(provider, node, (size_t)index);
    if (status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, status);
      return NULL;
    }
    if ((size_t)index >= node->iterator_cached_count) return NULL;
    return jinja_provider_iteration_child(
        jinja_provider_value_node(provider, &node->iterator_cache[index], node->parent),
        (size_t)index, SIZE_MAX, node);
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_RANGE) {
    if (node->range.count > UINT_MAX || node->range.count > provider->shared.node_capacity) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
      return NULL;
    }
    if ((uint64_t)index >= node->range.count) return NULL;
    return jinja_provider_iteration_child(
        jinja_provider_integer_node(provider, jinja_range_item(&node->range, index), node), index,
        (size_t)node->range.count, node);
  }
  if (node->expression_kind == JINJA_CMETA_EXPRESSION_DICT) {
    JINJA_CMETA_VALUE dict = {.kind = JINJA_CMETA_VALUE_DICT,
                              .first_collection_item = node->first_collection_item,
                              .collection_values = node->collection_values,
                              .collection_item_count = node->collection_item_count};
    JINJA_CMETA_VALUE key;
    JINJA_CMETA_STATUS status;
    if ((size_t)index >= node->owned_sequence.count) return NULL;
    status = jinja_dict_key_by_unique_index(provider, &dict, (size_t)index, 0u, &key);
    if (status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, status);
      return NULL;
    }
    return jinja_provider_iteration_child(
        jinja_provider_value_node(provider, &key, node->parent), (size_t)index,
        node->owned_sequence.count, node);
  }
  if (jinja_expression_is_collection(node->expression_kind)) {
    if ((size_t)index >= node->collection_item_count) return NULL;
    if (node->collection_values == NULL) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
      return NULL;
    }
    return jinja_provider_iteration_child(jinja_provider_value_node(provider,
                                                                    &node->collection_values[index],
                                                                    node->parent),
                                          (size_t)index, node->collection_item_count, node);
  }
  if (jinja_is_sequence_desc(node->desc)) {
    const JINJA_CMETA_SEQUENCE_VIEW *view = (const JINJA_CMETA_SEQUENCE_VIEW *)node->object;
    size_t offset;
    if (view->count > (size_t)UINT_MAX) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
      return NULL;
    }
    if ((size_t)index >= view->count) return NULL;
    if (view->data == NULL || view->stride == 0u || !cmeta_data_desc_valid(view->element) ||
        view->element->storage_type == NULL || view->element->storage_type->size > view->stride ||
        (size_t)index > SIZE_MAX / view->stride) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_METADATA);
      return NULL;
    }
    offset = (size_t)index * view->stride;
    return jinja_provider_iteration_child(
        jinja_provider_node(provider, (const unsigned char *)view->data + offset, view->element,
                            node),
        (size_t)index, view->count, node);
  }
  jinja_provider_fail(provider, JINJA_CMETA_ERR_RENDER);
  return NULL;
}



static int jinja_output_verbatim(const char *text, size_t size, void *opaque) {
  JINJA_CMETA_PROVIDER *provider = (JINJA_CMETA_PROVIDER *)opaque;
  if (jinja_output_suppressed(provider)) return 0;
  if (provider->shared.status != JINJA_CMETA_OK) return -1;
  if (provider->capture_depth != 0u) {
    size_t slot = provider->capture_stack[provider->capture_depth - 1u];
    JINJA_CMETA_STATUS append_status;
    if (provider->shared.capture_bytes > provider->shared.max_string_bytes ||
        size > provider->shared.max_string_bytes - provider->shared.capture_bytes ||
        !jinja_cmeta_string_append_fits(jinja_cmeta_text_length(&provider->shared.capture_buffers[slot]), size, SIZE_MAX)) {
      jinja_provider_fail(provider, JINJA_CMETA_ERR_CAPACITY);
      return -1;
    }
    append_status = jinja_cmeta_text_append(&provider->shared.capture_buffers[slot], text, size);
    if (append_status != JINJA_CMETA_OK) {
      jinja_provider_fail(provider, append_status);
      return -1;
    }
    provider->shared.capture_bytes += size;
    return 0;
  }
  return provider->shared.renderer->write(text, size, provider->shared.renderer_data);
}

static int jinja_html_write(const char *text, size_t size,
                            int (*write)(const char *, size_t, void *), void *opaque) {
  size_t start = 0u, i;
  for (i = 0u; i < size; ++i) {
    const char *entity;
    switch (text[i]) {
    case '&': entity = "&amp;"; break;
    case '<': entity = "&lt;"; break;
    case '>': entity = "&gt;"; break;
    case '\"': entity = "&#34;"; break;
    case '\'': entity = "&#39;"; break;
    default: continue;
    }
    if (i > start && write(text + start, i - start, opaque) != 0) return -1;
    if (write(entity, strlen(entity), opaque) != 0) return -1;
    start = i + 1u;
  }
  return start < size ? write(text + start, size - start, opaque) : 0;
}

static int jinja_output_escaped(const char *text, size_t size, void *opaque) {
  return jinja_html_write(text, size, jinja_output_verbatim, opaque);
}

typedef struct JINJA_LOOP_FRAME {
  JINJA_CMETA_ACTIVATION *activation;
  size_t lexical_scope;
  JINJA_CMETA_NODE *sequence;
  JINJA_CMETA_NODE *parent;
  size_t opener;
  uint8_t scope_depth;
  uint8_t capture_depth;
  uint8_t escaping_depth;
  uint8_t autoescape;
  uint8_t completed_body;
  uint8_t continuing;
} JINJA_LOOP_FRAME;

static JINJA_CMETA_STATUS jinja_program_value(JINJA_CMETA_PROVIDER *provider,
                                               JINJA_CMETA_NODE *context,
                                               const JINJA_CMETA_INSTRUCTION *instruction,
                                               JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_STATUS status;
  if (instruction->expression != SIZE_MAX) {
    status = jinja_expression_value(provider, context, instruction->expression, 0u, value);
    if (status == JINJA_CMETA_OK) jinja_normalize_call_argument(value);
    return status;
  }
  {
    JINJA_CMETA_OPERAND path = {
        .kind = JINJA_CMETA_OPERAND_PATH,
        .text = vstr_from_buf(provider->instance->templ->program_strings + instruction->offset,
                              instruction->length)};
    return jinja_value_from_operand(provider, context, &path, value);
  }
}

static JINJA_CMETA_STATUS jinja_unpack_values(JINJA_CMETA_PROVIDER *provider,
                                               JINJA_CMETA_VALUE *value, size_t count,
                                               JINJA_CMETA_VALUE *items) {
  JINJA_CMETA_STATUS status;
  JINJA_CMETA_VALUE length;
  jinja_normalize_call_argument(value);
  if (value->kind != JINJA_CMETA_VALUE_ITERATOR && value->kind != JINJA_CMETA_VALUE_LOOP) {
    status = jinja_value_length(provider, value, &length);
    if (status != JINJA_CMETA_OK) return status;
    if ((uint64_t)length.integer != count) return JINJA_CMETA_ERR_RENDER;
    return jinja_materialize_list(provider, value, items);
  }
  /* Consume exactly the requested items and one exhaustion probe. Do not drain
   * the entire iterator on an arity error or give aliases a second cursor. */
  size_t offset = provider->shared.collection_value_count;
  if (count > provider->shared.node_capacity || offset > provider->shared.values.limit ||
      count > provider->shared.values.limit - offset)
    return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_ensure_collection_storage(provider, count);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.collection_value_count += count;
  for (size_t i = 0u; i <= count; ++i) {
    JINJA_CMETA_VALUE item;
    int found;
    status = jinja_iterator_next(provider,
        value->kind == JINJA_CMETA_VALUE_LOOP ? value : value->iterator, &item, &found);
    if (status != JINJA_CMETA_OK) return status;
    if (found != (i < count)) return JINJA_CMETA_ERR_RENDER;
    if (i < count) {
      jinja_normalize_call_argument(&item);
      (*jinja_cmeta_values_at(&provider->shared.values, offset + i)) = item;
    }
  }
  *items = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
                               .collection_item_count = count,
                               .collection_values = count ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  return JINJA_CMETA_OK;
}

/* Preorder target instructions are bounded by the parser's node limit. All
 * shapes are checked before publishing any name, so RHS aliases see old values. */
static JINJA_CMETA_STATUS jinja_namespace_target(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_INSTRUCTION *target, JINJA_CMETA_VALUE **dict, vstr *attribute) {
  if (target->target == 0u || target->attribute_offset <= target->target ||
      target->attribute_offset >= target->length) return JINJA_CMETA_ERR_RENDER;
  vstr name = vstr_from_buf(provider->instance->templ->program_strings + target->offset, target->target);
  const JINJA_CMETA_VALUE *owner = jinja_binding_find(provider, name);
  if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
  if (owner == NULL || owner->kind != JINJA_CMETA_VALUE_NAMESPACE || owner->namespace_dict == NULL)
    return JINJA_CMETA_ERR_RENDER;
  *dict = owner->namespace_dict;
  *attribute = vstr_from_buf(provider->instance->templ->program_strings + target->offset + target->attribute_offset,
                            target->length - target->attribute_offset);
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_namespace_guard(JINJA_CMETA_PROVIDER *provider, size_t pc) {
  size_t end = provider->instance->templ->instructions[pc].end;
  if (end > provider->instance->templ->instruction_count || end <= pc) return JINJA_CMETA_ERR_RENDER;
  for (; pc < end; ++pc) {
    const JINJA_CMETA_INSTRUCTION *target = &provider->instance->templ->instructions[pc];
    if (target->opcode == JINJA_CMETA_OP_ASSIGN_ATTRIBUTE) {
      JINJA_CMETA_VALUE *dict;
      vstr attribute;
      JINJA_CMETA_STATUS status = jinja_namespace_target(provider, target, &dict, &attribute);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_assignment_write(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_BINDING *pending) {
  if (pending->attribute == NULL) return jinja_binding_write(provider, pending->name, &pending->value);
  JINJA_CMETA_VALUE *dict;
  vstr attribute;
  JINJA_CMETA_STATUS status = jinja_namespace_target(provider, pending->attribute, &dict, &attribute);
  if (status != JINJA_CMETA_OK) return status;
  JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_STRING, .string = attribute};
  return jinja_dict_snapshot_write(provider, dict, &key, &pending->value);
}

static JINJA_CMETA_STATUS jinja_collect_assignment(JINJA_CMETA_PROVIDER *provider,
                                                  JINJA_CMETA_NODE *context, size_t *pc,
                                                  JINJA_CMETA_VALUE *value,
                                                  JINJA_CMETA_BINDING *pending, size_t *count,
                                                  size_t depth) {
  const JINJA_CMETA_TEMPLATE *templ = provider->instance->templ;
  if (*pc >= templ->instruction_count) return JINJA_CMETA_ERR_RENDER;
  if (depth >= JINJA_CMETA_MAX_COMPILED_EXPRESSIONS) return JINJA_CMETA_ERR_CAPACITY;
  const JINJA_CMETA_INSTRUCTION *target = &templ->instructions[(*pc)++];
  if (target->opcode == JINJA_CMETA_OP_ASSIGN || target->opcode == JINJA_CMETA_OP_ASSIGN_ATTRIBUTE) {
    if (*count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS) return JINJA_CMETA_ERR_CAPACITY;
    jinja_normalize_call_argument(value);
    pending[(*count)++] = (JINJA_CMETA_BINDING){
        vstr_from_buf(templ->program_strings + target->offset, target->length), *value,
        target->opcode == JINJA_CMETA_OP_ASSIGN_ATTRIBUTE ? target : NULL};
    return JINJA_CMETA_OK;
  }
  if (target->opcode != JINJA_CMETA_OP_UNPACK ||
      target->target > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS)
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_VALUE items;
  JINJA_CMETA_STATUS status = jinja_unpack_values(provider, value, target->target, &items);
  if (status != JINJA_CMETA_OK) return status;
  for (size_t i = 0u; i < target->target; ++i) {
    JINJA_CMETA_VALUE item = items.collection_values[i];
    status = jinja_collect_assignment(provider, context, pc, &item,
                                       pending, count, depth + 1u);
    if (status != JINJA_CMETA_OK) return status;
  }
  return *pc == target->end ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_publish_assignments(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_BINDING *pending, size_t count) {
  size_t needed = 0u;
  size_t first = provider->scope_depth == 0u ? 0u : provider->scopes[provider->scope_depth - 1u].first_binding;
  JINJA_CMETA_STATUS status;
  /* Preflight distinct new slots before overwriting an existing binding. */
  for (size_t i = 0u; i < count; ++i) {
    if (provider->activation != NULL) {
      if (pending[i].attribute == NULL && jinja_lexical_binding(provider, pending[i].name, 1) == NULL)
        return JINJA_CMETA_ERR_METADATA;
    }
    int found = 0;
    if (pending[i].attribute != NULL) continue;
    for (size_t j = first; j < provider->binding_count && !found; ++j)
      found = pending[i].name.len == provider->shared.bindings[j].len &&
              memcmp(pending[i].name.data, provider->shared.bindings[j].data, pending[i].name.len) == 0;
    for (size_t j = 0u; j < i && !found; ++j)
      found = pending[i].name.len == pending[j].name.len &&
              memcmp(pending[i].name.data, pending[j].name.data, pending[i].name.len) == 0;
    if (!found) ++needed;
  }
  if (needed > provider->shared.node_capacity - provider->binding_count) return JINJA_CMETA_ERR_CAPACITY;
  for (size_t i = 0u; i < count; ++i) {
    status = jinja_assignment_write(provider, &pending[i]);
    if (status != JINJA_CMETA_OK) return status;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_unpack_assignment(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t *pc, JINJA_CMETA_VALUE *value) {
  JINJA_CMETA_BINDING pending[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  size_t count = 0u;
  JINJA_CMETA_STATUS status = jinja_collect_assignment(provider, context, pc, value, pending, &count, 0u);
  return status == JINJA_CMETA_OK ? jinja_publish_assignments(provider, pending, count) : status;
}

/* Rebuild the yielded target shape from its final bindings. This preserves
 * repeated names and avoids consuming an unpacked iterator again in the body.
 * O(target nodes * active bindings), bounded by the compiled target tree. */
static JINJA_CMETA_STATUS jinja_assignment_result(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t *pc, size_t depth, JINJA_CMETA_VALUE *value) {
  if (*pc >= provider->instance->templ->instruction_count) return JINJA_CMETA_ERR_RENDER;
  if (depth >= JINJA_CMETA_MAX_COMPILED_EXPRESSIONS) return JINJA_CMETA_ERR_CAPACITY;
  const JINJA_CMETA_INSTRUCTION *target = &provider->instance->templ->instructions[(*pc)++];
  if (target->opcode == JINJA_CMETA_OP_ASSIGN) {
    const JINJA_CMETA_VALUE *binding = jinja_binding_find(provider,
        vstr_from_buf(provider->instance->templ->program_strings + target->offset, target->length));
    if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
    if (binding == NULL) return JINJA_CMETA_ERR_RENDER;
    *value = *binding;
    return JINJA_CMETA_OK;
  }
  if (target->opcode != JINJA_CMETA_OP_UNPACK) return JINJA_CMETA_ERR_RENDER;
  size_t offset = provider->shared.collection_value_count;
  size_t count = target->target;
  if (offset > provider->shared.values.limit ||
      count > provider->shared.values.limit - offset) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_STATUS status = jinja_ensure_collection_storage(provider, count);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.collection_value_count += count;
  for (size_t i = 0u; i < count; ++i) {
    status = jinja_assignment_result(provider, context, pc, depth + 1u,
        jinja_cmeta_values_at(&provider->shared.values, offset + i));
    if (status != JINJA_CMETA_OK) return status;
  }
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_TUPLE,
      .collection_item_count = count,
      .collection_values = count ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
  return *pc == target->end ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
}

/* The source cursor is authoritative. Cache only admitted values; length drains
 * on demand, while index/neighbor requests stop at the required admitted item.
 * Both rejected scans and retained values remain bounded by render budgets. */
static JINJA_CMETA_STATUS jinja_filtered_cache_until(JINJA_CMETA_PROVIDER *provider,
                                                    JINJA_CMETA_NODE *node, size_t index) {
  JINJA_CMETA_STATUS status;
  if (!node->iterator_initialized) {
    JINJA_CMETA_NODE *source = node->loop_state->source;
    size_t count;
    if (source->expression_kind == JINJA_CMETA_EXPRESSION_ITEMS) {
      status = jinja_iterator_remaining_bound(provider, source->iterator, &count);
      if (status != JINJA_CMETA_OK) return status;
    } else if (source->expression_kind == JINJA_CMETA_EXPRESSION_RANGE) {
      if (source->range.count > provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
      count = (size_t)source->range.count;
    } else {
      if (!jinja_is_sequence_desc(source->desc) || source->object == NULL) return JINJA_CMETA_ERR_RENDER;
      count = ((const JINJA_CMETA_SEQUENCE_VIEW *)source->object)->count;
    }
    if (count > provider->shared.node_capacity) count = provider->shared.node_capacity;
    size_t offset = provider->shared.collection_value_count;
    if (offset > provider->shared.values.limit ||
        count > provider->shared.values.limit - offset) return JINJA_CMETA_ERR_CAPACITY;
    status = jinja_ensure_collection_storage(provider, count);
    if (status != JINJA_CMETA_OK) return status;
    node->iterator_cache = count ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL;
    node->iterator_cache_capacity = count;
    provider->shared.collection_value_count += count;
    node->iterator_initialized = 1;
  }
  while (!node->iterator_done && node->iterator_cached_count <= index) {
    if (node->loop_state->cursor > UINT_MAX) return JINJA_CMETA_ERR_CAPACITY;
    JINJA_CMETA_NODE *child = jinja_iteration_child_at(node->loop_state->source,
        (unsigned)node->loop_state->cursor, provider);
    if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
    if (child == NULL) {
      node->iterator_done = 1;
      break;
    }
    ++node->loop_state->cursor;
    JINJA_CMETA_VALUE item = {.kind = JINJA_CMETA_VALUE_NODE, .node = *child};
    JINJA_CMETA_VALUE test;
    int truth = 0;
    size_t target = node->loop_state->opener + 1u;
    jinja_normalize_call_argument(&item);
    status = jinja_scope_enter(provider);
    if (status != JINJA_CMETA_OK) return status;
    /* Lookahead can run inside the body: the predicate sees its lexical parent,
     * not body-local assignments. New target slots never overwrite live slots. */
    provider->instance = node->loop_state->instance;
    provider->context = node->loop_state->template_context;
    status = jinja_scope_at(provider, node->loop_state->opener,
        JINJA_CMETA_SCOPE_TEST, node->loop_state->activation);
    if (status != JINJA_CMETA_OK) {
      JINJA_CMETA_STATUS leave_status = jinja_scope_leave(provider);
      if (leave_status != JINJA_CMETA_OK) return leave_status;
      return status;
    }
    int body_autoescape = provider->autoescape;
    provider->autoescape = node->loop_state->autoescape;
    status = jinja_unpack_assignment(provider, node->parent, &target, &item);
    if (status == JINJA_CMETA_OK)
      status = jinja_expression_value(provider, node->parent,
          provider->instance->templ->instructions[node->loop_state->opener].filter_expression, 0u, &test);
    if (status == JINJA_CMETA_OK) status = jinja_value_truthy(provider, &test, &truth);
    if (status == JINJA_CMETA_OK && truth) {
      target = node->loop_state->opener + 1u;
      status = jinja_assignment_result(provider, node->parent, &target, 0u, &item);
    }
    provider->autoescape = body_autoescape;
    JINJA_CMETA_STATUS leave_status = jinja_scope_leave(provider);
    if (status != JINJA_CMETA_OK) return status;
    if (leave_status != JINJA_CMETA_OK) return leave_status;
    if (truth) {
      if (node->iterator_cached_count == node->iterator_cache_capacity) return JINJA_CMETA_ERR_CAPACITY;
      node->iterator_cache[node->iterator_cached_count++] = item;
    }
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_with_begin(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t *pc) {
  JINJA_CMETA_BINDING pending[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  size_t count = 0u, next = *pc + 1u;
  size_t end = provider->instance->templ->instructions[*pc].end;
  JINJA_CMETA_STATUS status;
  if (end < next || end > provider->instance->templ->instruction_count) return JINJA_CMETA_ERR_RENDER;
  /* Every initializer sees the enclosing scope; unpack errors still precede
   * later initializer evaluation, as in Jinja's ordered assignment sequence. */
  while (next < end) {
    JINJA_CMETA_VALUE value;
    status = jinja_program_value(provider, context, &provider->instance->templ->instructions[next], &value);
    if (status == JINJA_CMETA_OK)
      status = jinja_collect_assignment(provider, context, &next, &value, pending, &count, 0u);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (next != end) return JINJA_CMETA_ERR_RENDER;
  status = jinja_scope_enter(provider);
  if (status == JINJA_CMETA_OK) status = jinja_scope_at(provider, *pc, JINJA_CMETA_SCOPE_BODY, NULL);
  if (status == JINJA_CMETA_OK) status = jinja_publish_assignments(provider, pending, count);
  if (status == JINJA_CMETA_OK) *pc = end;
  return status;
}

static JINJA_CMETA_STATUS jinja_capture_begin(JINJA_CMETA_PROVIDER *provider) {
  JINJA_CMETA_STATUS status;
  JINJA_CMETA_TEXT bytes = {0};
  if (provider->shared.capture_count == provider->shared.node_capacity ||
      provider->capture_depth == JINJA_CMETA_MAX_BLOCK_DEPTH) return JINJA_CMETA_ERR_CAPACITY;
  status = jinja_scope_enter(provider);
  if (status != JINJA_CMETA_OK) return status;
  if (provider->shared.capture_buffers == NULL) {
    provider->shared.capture_buffers = (JINJA_CMETA_TEXT *)jinja_provider_zero(provider, provider->shared.node_capacity, sizeof(*provider->shared.capture_buffers));
    if (provider->shared.capture_buffers == NULL) return provider->shared.status;
  }
  status = jinja_cmeta_text_init(&bytes, provider->shared.memory);
  if (status != JINJA_CMETA_OK) return status;
  provider->shared.capture_buffers[provider->shared.capture_count] = bytes;
  provider->capture_stack[provider->capture_depth++] = provider->shared.capture_count++;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_capture_evaluate(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_INSTRUCTION *header, JINJA_CMETA_VALUE *result) {
  JINJA_CMETA_STATUS status;
  if (provider->capture_depth == 0u) return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_TEXT bytes = provider->shared.capture_buffers[provider->capture_stack[provider->capture_depth - 1u]];
  /* The buffer is now sealed. Filters run before leaving this local scope,
   * while the resulting immutable view survives until render cleanup. */
  provider->capture_value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string = vstr_from_buf(jinja_cmeta_text_data(&bytes), jinja_cmeta_text_length(&bytes)), .string_safe = provider->autoescape};
  provider->capture_value_active = 1;
  status = jinja_program_value(provider, context, header, result);
  provider->capture_value_active = 0;
  return status;
}

static JINJA_CMETA_STATUS jinja_capture_end(JINJA_CMETA_PROVIDER *provider,
                                            JINJA_CMETA_NODE *context, size_t opener) {
  const JINJA_CMETA_INSTRUCTION *header;
  JINJA_CMETA_VALUE result;
  JINJA_CMETA_STATUS status;
  size_t target = opener + 1u;
  if (opener >= provider->instance->templ->instruction_count) return JINJA_CMETA_ERR_RENDER;
  header = &provider->instance->templ->instructions[opener];
  if (header->opcode != JINJA_CMETA_OP_CAPTURE_BEGIN) return JINJA_CMETA_ERR_RENDER;
  status = jinja_capture_evaluate(provider, context, header, &result);
  if (status != JINJA_CMETA_OK) return status;
  if (provider->autoescape) {
    JINJA_CMETA_VALUE safe;
    status = jinja_safety_value(provider, &result, JINJA_CMETA_EXPRESSION_SAFE, &safe);
    if (status != JINJA_CMETA_OK) return status;
    result = safe;
  }
  --provider->capture_depth;
  status = jinja_scope_leave(provider);
  if (status == JINJA_CMETA_OK) status = jinja_unpack_assignment(provider, context, &target, &result);
  if (status != JINJA_CMETA_OK) return status;
  return target == header->end ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
}

static JINJA_CMETA_STATUS jinja_filter_end(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t opener) {
  JINJA_CMETA_VALUE result;
  JINJA_CMETA_NODE *node;
  JINJA_CMETA_STATUS status;
  if (opener >= provider->instance->templ->instruction_count ||
      provider->instance->templ->instructions[opener].opcode != JINJA_CMETA_OP_FILTER_BEGIN)
    return JINJA_CMETA_ERR_RENDER;
  status = jinja_capture_evaluate(provider, context, &provider->instance->templ->instructions[opener], &result);
  if (status != JINJA_CMETA_OK) return status;
  if (!jinja_value_is_string(&result)) return JINJA_CMETA_ERR_RENDER;
  --provider->capture_depth;
  status = jinja_scope_leave(provider);
  if (status != JINJA_CMETA_OK) return status;
  node = jinja_provider_value_node(provider, &result, context);
  if (node == NULL) return provider->shared.status;
  /* FilterBlock writes the returned string directly, unlike interpolation. */
  if (jinja_dump(node, jinja_output_verbatim, provider, provider) != 0)
    return provider->shared.status == JINJA_CMETA_OK ? JINJA_CMETA_ERR_RENDER : provider->shared.status;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_function_value(JINJA_CMETA_PROVIDER *provider,
    size_t function, JINJA_CMETA_VALUE *value) {
  if (function >= provider->instance->templ->function_count || provider->activation == NULL)
    return JINJA_CMETA_ERR_METADATA;
  if (provider->shared.closure_count == provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_CLOSURE *closure = (JINJA_CMETA_CLOSURE *)jinja_provider_allocate(provider, 1u, sizeof(*closure));
  if (closure == NULL) return provider->shared.status;
  *closure = (JINJA_CMETA_CLOSURE){.next = provider->shared.closures, .activation = provider->activation,
      .instance = provider->instance, .context = provider->context, .function = function, .autoescape = provider->autoescape};
  provider->shared.closures = closure;
  ++provider->shared.closure_count;
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_CALLABLE,
      .callable_kind = provider->instance->templ->functions[function].is_block
          ? JINJA_CMETA_EXPRESSION_BLOCK : JINJA_CMETA_EXPRESSION_MACRO, .closure = closure};
  return JINJA_CMETA_OK;
}

/* Block identity is (defining instance, function), never a bare function index.
 * Linear lookup is bounded by the render's loaded-template/function quotas. */
static JINJA_CMETA_STATUS jinja_block_value(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TEMPLATE_INSTANCE *start, vstr name,
    JINJA_CMETA_CONTEXT *context, JINJA_CMETA_VALUE *value) {
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  for (JINJA_CMETA_TEMPLATE_INSTANCE *instance = start; instance != NULL; instance = instance->parent) {
    const JINJA_CMETA_TEMPLATE *templ = instance->templ;
    for (size_t i = 0u; i < templ->function_count; ++i) {
      const JINJA_CMETA_FUNCTION *function = &templ->functions[i];
      if (!function->is_block || function->name_length != name.len ||
          memcmp(templ->program_strings + function->name_offset, name.data, name.len) != 0) continue;
      JINJA_CMETA_TEMPLATE_INSTANCE *saved_instance = provider->instance;
      JINJA_CMETA_ACTIVATION *saved_activation = provider->activation;
      provider->instance = instance;
      provider->activation = instance->root_activation;
      JINJA_CMETA_STATUS status = jinja_function_value(provider, i, value);
      provider->instance = saved_instance;
      provider->activation = saved_activation;
      if (status == JINJA_CMETA_OK) {
        value->closure->activation = NULL;
        value->closure->context = context;
      }
      return status;
    }
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_super_value(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CLOSURE *block, JINJA_CMETA_VALUE *value) {
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (block == NULL) return JINJA_CMETA_OK;
  if (block->instance == NULL || block->function >= block->instance->templ->function_count)
    return JINJA_CMETA_ERR_METADATA;
  const JINJA_CMETA_TEMPLATE *templ = block->instance->templ;
  const JINJA_CMETA_FUNCTION *function = &templ->functions[block->function];
  return jinja_block_value(provider, block->instance->parent,
      vstr_from_buf(templ->program_strings + function->name_offset, function->name_length),
      block->context, value);
}

/* Export provenance lives in the root cells. Resolved inputs and imported
 * aliases are not exports; later ordinary assignments can export that name. */
static JINJA_CMETA_STATUS jinja_module_export(JINJA_CMETA_TEMPLATE_INSTANCE *instance,
    vstr name, JINJA_CMETA_VALUE *value) {
  *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
  if (name.len == 0u || name.data[0] == '_') return JINJA_CMETA_OK;
  for (; instance != NULL; instance = instance->parent) {
    if (instance->root_activation == NULL) return JINJA_CMETA_ERR_METADATA;
    const JINJA_CMETA_TEMPLATE *templ = instance->templ;
    const JINJA_CMETA_LEXICAL_SCOPE *root = &templ->lexical_scopes[0];
    for (size_t i = 0u; i < root->binding_count; ++i) {
      const size_t index = templ->cell_bindings[root->first_binding + i].cell;
      if (!vstr_eq(templ->cells[index].name, name)) continue;
      JINJA_CMETA_CELL_VALUE *cell;
      JINJA_CMETA_STATUS status = jinja_cmeta_activation_cell(instance->root_activation, index, &cell);
      if (status != JINJA_CMETA_OK) return status;
      if (cell->bound && cell->assigned)
        *value = cell->exported ? cell->value :
            (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_UNDEFINED};
      break;
    }
  }
  return JINJA_CMETA_OK;
}
/* First visible binding wins. No payload or name storage is duplicated. */
static JINJA_CMETA_STATUS jinja_context_append(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_CONTEXT *context, vstr name, const JINJA_CMETA_VALUE *value) {
  if (value->kind == JINJA_CMETA_VALUE_MISSING) return JINJA_CMETA_OK;
  for (size_t i = 0u; i < context->count; ++i)
    if (vstr_eq(context->entries[i * 2u].string, name)) return JINJA_CMETA_OK;
  const size_t offset = provider->shared.collection_value_count;
  if (context->count == provider->shared.node_capacity ||
      provider->shared.values.limit - offset < 2u) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_STATUS status = jinja_cmeta_values_prepare_context(&provider->shared.values, offset,
      context->count * JINJA_CMETA_VALUE_PAIR_WIDTH);
  if (status != JINJA_CMETA_OK) return status;
  context->entries = jinja_cmeta_values_at(&provider->shared.values,
      offset - context->count * JINJA_CMETA_VALUE_PAIR_WIDTH);
  (*jinja_cmeta_values_at(&provider->shared.values, offset)) = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING, .string = name};
  (*jinja_cmeta_values_at(&provider->shared.values, offset + 1u)) = *value;
  provider->shared.collection_value_count += 2u;
  ++context->count;
  return JINJA_CMETA_OK;
}

/* Snapshot visible locals plus the current template context. O(N²) bounded
 * name comparisons, O(N) VALUE slots; snapshots outlive escaped self/closures. */
static JINJA_CMETA_STATUS jinja_context_derive(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_CONTEXT **result) {
  if (provider->shared.context_count == provider->shared.node_capacity) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_CONTEXT *context = (JINJA_CMETA_CONTEXT *)jinja_provider_zero(provider, 1u, sizeof(*context));
  if (context == NULL) return provider->shared.status;
  context->next = provider->shared.contexts;
  provider->shared.contexts = context;
  ++provider->shared.context_count;
  size_t scope = provider->lexical_scope;
  JINJA_CMETA_ACTIVATION *activation = provider->activation;
  for (;;) {
    const JINJA_CMETA_LEXICAL_SCOPE *frame = &provider->instance->templ->lexical_scopes[scope];
    for (size_t i = 0u; i < frame->binding_count; ++i) {
      size_t index = provider->instance->templ->cell_bindings[frame->first_binding + i].cell;
      JINJA_CMETA_CELL_VALUE *cell;
      JINJA_CMETA_STATUS status = jinja_cmeta_activation_cell(activation, index, &cell);
      if (status != JINJA_CMETA_OK) return status;
      if (cell->bound) status = jinja_context_append(provider, context,
          provider->instance->templ->cells[index].name, &cell->value);
      if (status != JINJA_CMETA_OK) return status;
    }
    if (frame->parent != SIZE_MAX) scope = frame->parent;
    else break;
  }
  JINJA_CMETA_TEMPLATE_INSTANCE *owner = provider->context->owner;
  if (owner != NULL && owner->root_activation != NULL) {
    const JINJA_CMETA_LEXICAL_SCOPE *root = &owner->templ->lexical_scopes[0];
    for (size_t i = 0u; i < root->binding_count; ++i) {
      const size_t index = owner->templ->cell_bindings[root->first_binding + i].cell;
      JINJA_CMETA_CELL_VALUE *cell;
      JINJA_CMETA_STATUS status = jinja_cmeta_activation_cell(owner->root_activation, index, &cell);
      if (status != JINJA_CMETA_OK) return status;
      if (cell->bound) {
        status = jinja_context_append(provider, context, owner->templ->cells[index].name, &cell->value);
        if (status != JINJA_CMETA_OK) return status;
      }
    }
  }
  for (size_t i = 0u; i < provider->context->count; ++i) {
    const JINJA_CMETA_VALUE *entry = &provider->context->entries[i * 2u];
    JINJA_CMETA_STATUS status = jinja_context_append(provider, context, entry->string, entry + 1u);
    if (status != JINJA_CMETA_OK) return status;
  }
  *result = context;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_execute_range(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t begin, size_t end,
    unsigned max_depth, size_t *error_offset, const JINJA_CMETA_RECURSIVE_INPUT *recurse);

/* Included metadata and instances stay alive until render cleanup. Context
 * snapshots borrow the shared fixed-address workspace; admission never resets it. */
static JINJA_CMETA_STATUS jinja_execute_template(JINJA_CMETA_PROVIDER *provider);

static JINJA_CMETA_STATUS jinja_execute_instance(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TEMPLATE_INSTANCE *instance) {
  if (provider->shared.call_depth >= provider->shared.max_render_depth)
    return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_TEMPLATE_INSTANCE *saved_instance = provider->instance;
  JINJA_CMETA_ACTIVATION *saved_activation = provider->activation;
  JINJA_CMETA_CONTEXT *saved_context = provider->context;
  const JINJA_CMETA_FUNCTION *saved_default = provider->default_function;
  JINJA_CMETA_CLOSURE *saved_caller = provider->caller;
  const JINJA_CMETA_CLOSURE *saved_block = provider->active_block;
  const JINJA_CMETA_INSTRUCTION *saved_control = provider->pending_control;
  const size_t lexical = provider->lexical_scope, scopes = provider->scope_depth;
  const size_t bindings = provider->binding_count, captures = provider->capture_depth;
  const size_t parameter = provider->default_parameter, caller_expression = provider->caller_expression;
  const int escaping = provider->autoescape;
  const int strict_undefined = provider->strict_undefined;
  JINJA_CMETA_STATUS status = jinja_scope_enter(provider);
  if (status != JINJA_CMETA_OK) return status;
  provider->instance = instance;
  provider->context = &instance->root_context;
  provider->activation = NULL;
  provider->lexical_scope = 0u;
  provider->default_function = NULL;
  provider->caller = NULL;
  provider->active_block = NULL;
  provider->caller_expression = SIZE_MAX;
  provider->pending_control = NULL;
  provider->autoescape = instance->templ->autoescape;
  provider->strict_undefined = instance->templ->undefined_policy == JINJA_CMETA_UNDEFINED_STRICT;
  ++provider->shared.call_depth;
  status = jinja_scope_initialize(provider, 0u, NULL);
  instance->root_activation = provider->activation;
  if (status == JINJA_CMETA_OK)
    status = jinja_execute_template(provider);
  if (status == JINJA_CMETA_OK && provider->pending_control != NULL)
    status = JINJA_CMETA_ERR_RENDER;
  jinja_record_render_error(provider, status);
  --provider->shared.call_depth;
  provider->instance = saved_instance;
  provider->activation = saved_activation;
  provider->context = saved_context;
  provider->lexical_scope = lexical;
  provider->scope_depth = scopes;
  provider->binding_count = bindings;
  provider->capture_depth = captures;
  provider->default_function = saved_default;
  provider->default_parameter = parameter;
  provider->caller = saved_caller;
  provider->active_block = saved_block;
  provider->caller_expression = caller_expression;
  provider->pending_control = saved_control;
  provider->autoescape = escaping;
  provider->strict_undefined = strict_undefined;
  return status;
}

/* Cache only completed, context-free modules within this render. No borrowed
 * runtime value escapes into the environment or survives render cleanup. */
static JINJA_CMETA_STATUS jinja_load_instance(JINJA_CMETA_PROVIDER *provider,
    vstr name, int cacheable, JINJA_CMETA_TEMPLATE_INSTANCE **result) {
  JINJA_CMETA_ENV *env = provider->instance->templ->env;
  *result = NULL;
  if (env == NULL) return JINJA_CMETA_ERR_LOADER;
  if (cacheable) {
    for (JINJA_CMETA_TEMPLATE_INSTANCE *it = provider->shared.instances; it != NULL; it = it->next) {
      if (it->cacheable && it->templ->env == env && vstr_eq(it->templ->name, name)) {
        if (it->module_state != JINJA_CMETA_MODULE_READY) return JINJA_CMETA_ERR_RENDER;
        *result = it;
        return JINJA_CMETA_OK;
      }
    }
  }
  if (provider->shared.loaded_templates >= env->options.max_loaded_templates ||
      provider->shared.loaded_templates >= provider->shared.node_capacity - 1u)
    return JINJA_CMETA_ERR_CAPACITY;
  size_t bytes = 0u;
  JINJA_CMETA_ERROR detail = JINJA_CMETA_ERROR_INIT;
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_load_bounded(env, name,
      env->options.max_loaded_source_bytes - provider->shared.loaded_source_bytes, &bytes, &detail);
  if (templ == NULL) {
    if (detail.status != JINJA_CMETA_ERR_NOT_FOUND) *provider->shared.error = detail;
    return detail.status;
  }
  provider->shared.loaded_source_bytes += bytes;
  JINJA_CMETA_TEMPLATE_INSTANCE *instance =
      (JINJA_CMETA_TEMPLATE_INSTANCE *)jinja_provider_zero(provider, 1u, sizeof(*instance));
  if (instance == NULL) {
    jinja_cmeta_release(templ);
    return provider->shared.status;
  }
  instance->templ = templ;
  instance->chain_root = instance;
  instance->root_context.owner = instance;
  instance->cacheable = cacheable;
  instance->next = provider->shared.instances;
  provider->shared.instances = instance;
  ++provider->shared.loaded_templates;
  *result = instance;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_instance_context(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_TEMPLATE_INSTANCE *instance) {
  JINJA_CMETA_CONTEXT *derived;
  JINJA_CMETA_STATUS status = jinja_context_derive(provider, &derived);
  if (status != JINJA_CMETA_OK) return status;
  instance->root_context = *derived;
  instance->root_context.owner = instance;
  instance->root_visible = provider->instance->root_visible;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_execute_template(JINJA_CMETA_PROVIDER *provider) {
  JINJA_CMETA_TEMPLATE_INSTANCE *instance = provider->instance;
  JINJA_CMETA_OUTPUT_GUARD guard = {
      .previous = provider->output_guard, .capture_depth = provider->capture_depth};
  provider->output_guard = &guard;
  JINJA_CMETA_STATUS status = jinja_execute_range(provider, &provider->shared.nodes[0], 0u,
      instance->templ->instruction_count, provider->shared.max_render_depth,
      provider->shared.error_offset, NULL);
  provider->output_guard = guard.previous;
  if (status == JINJA_CMETA_OK && instance->parent != NULL) {
    /* Finish child initialization before constructing the parent's live root. */
    status = jinja_instance_context(provider, instance->parent);
    if (status == JINJA_CMETA_OK) status = jinja_execute_instance(provider, instance->parent);
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_execute_translation(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_INSTRUCTION *instruction) {
  const JINJA_CMETA_TEMPLATE *templ = provider->instance->templ;
  const JINJA_CMETA_TRANSLATION *translation;
  JINJA_CMETA_CALL_ARGUMENT *arguments = NULL;
  JINJA_CMETA_CALL_RESULT result = {0};
  JINJA_CMETA_VALUE value = {0};
  JINJA_CMETA_NODE *node;
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  if (templ->env == NULL || templ->env->options.translation == NULL ||
      instruction->target >= templ->translation_count) return JINJA_CMETA_ERR_UNSUPPORTED;
  translation = &templ->translations[instruction->target];
  if (translation->first_binding > templ->translation_binding_count ||
      translation->binding_count > templ->translation_binding_count - translation->first_binding)
    return JINJA_CMETA_ERR_METADATA;
  if (translation->binding_count != 0u) {
    arguments = (JINJA_CMETA_CALL_ARGUMENT *)jinja_provider_allocate(provider,
        translation->binding_count, sizeof(*arguments));
    if (arguments == NULL) return provider->shared.status;
  }
  for (size_t i = 0u; i < translation->binding_count; ++i) {
    const JINJA_CMETA_TRANSLATION_BINDING *binding =
        &templ->translation_bindings[translation->first_binding + i];
    if (binding->name_offset > SIZE_MAX - binding->name_length ||
        binding->expression >= templ->expression_count) {
      status = JINJA_CMETA_ERR_METADATA;
      goto cleanup;
    }
    arguments[i].name = vstr_from_buf(templ->program_strings + binding->name_offset,
        binding->name_length);
    status = jinja_expression_value(provider, context, binding->expression, 0u, &value);
    if (status != JINJA_CMETA_OK) goto cleanup;
    jinja_normalize_call_argument(&value);
    if (provider->strict_undefined && value.kind == JINJA_CMETA_VALUE_UNDEFINED) {
      status = JINJA_CMETA_ERR_RENDER;
      goto cleanup;
    }
    status = jinja_host_call_value(provider, &value, &arguments[i].value);
    if (status != JINJA_CMETA_OK) goto cleanup;
    arguments[i].descriptor = NULL;
    arguments[i].object = NULL;
  }
  status = templ->env->options.translation(templ->env->options.translation_userdata,
      vstr_from_buf(templ->program_strings + translation->context_offset,
          translation->context_length),
      vstr_from_buf(templ->program_strings + translation->singular_offset,
          translation->singular_length),
      vstr_from_buf(templ->program_strings + translation->plural_offset,
          translation->plural_length), arguments, translation->binding_count, &result);
  if (status != JINJA_CMETA_OK) {
    jinja_cmeta_error_set(provider->shared.error, status, *provider->shared.error_offset,
        "translation callback failed");
    goto cleanup;
  }
  if (result.value.kind != JINJA_CMETA_CALL_VALUE_STRING ||
      !vstr_is_valid(result.value.string) ||
      vstr_utf8_invalid_offset(result.value.string) != VSTR_NPOS) {
    status = JINJA_CMETA_ERR_METADATA;
    goto cleanup;
  }
  const size_t offset = provider->shared.slice_byte_count;
  if (jinja_concat_write(result.value.string.data, result.value.string.len, provider) != 0) {
    status = provider->shared.status;
    goto cleanup;
  }
  value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
      .string_safe = result.value.string_safe,
      .string = result.value.string.len == 0u ? vstr_from_cstr("")
          : vstr_from_buf(provider->shared.slice_bytes + offset, result.value.string.len)};
  node = jinja_provider_value_node(provider, &value, context);
  if (node == NULL) { status = provider->shared.status; goto cleanup; }
  status = jinja_dump(node, provider->autoescape && !jinja_value_is_safe(&value)
          ? jinja_output_escaped : jinja_output_verbatim, provider, provider) != 0
      ? (provider->shared.status == JINJA_CMETA_OK ? JINJA_CMETA_ERR_RENDER : provider->shared.status)
      : JINJA_CMETA_OK;
cleanup:
  jinja_cmeta_memory_drop(arguments);
  return status;
}

static JINJA_CMETA_STATUS jinja_execute_include(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_INSTRUCTION *instruction) {
  if (provider->shared.call_depth >= provider->shared.max_render_depth)
    return JINJA_CMETA_ERR_CAPACITY;
  JINJA_CMETA_VALUE names;
  JINJA_CMETA_STATUS status = jinja_program_value(provider, context, instruction, &names);
  if (status != JINJA_CMETA_OK) return status;
  const int single = jinja_value_is_string(&names);
  if (names.kind == JINJA_CMETA_VALUE_UNDEFINED || names.kind == JINJA_CMETA_VALUE_NONE)
    return JINJA_CMETA_ERR_RENDER;
  if (!single) {
    status = jinja_materialize_list(provider, &names, &names);
    if (status != JINJA_CMETA_OK) return status;
  }
  const size_t count = single ? 1u : names.collection_item_count;
  for (size_t i = 0u; i < count; ++i) {
    vstr name;
    int valid = 0;
    status = jinja_lookup_string_key(provider,
        single ? &names : &names.collection_values[i], &name, &valid);
    if (status != JINJA_CMETA_OK) return status;
    if (!valid) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_TEMPLATE_INSTANCE *instance;
    status = jinja_load_instance(provider, name, 0, &instance);
    if (status == JINJA_CMETA_ERR_NOT_FOUND) continue;
    if (status != JINJA_CMETA_OK) return status;
    if (instruction->with_context) status = jinja_instance_context(provider, instance);
    if (status == JINJA_CMETA_OK) status = jinja_execute_instance(provider, instance);
    return status;
  }
  return instruction->ignore_missing ? JINJA_CMETA_OK : JINJA_CMETA_ERR_NOT_FOUND;
}

static JINJA_CMETA_STATUS jinja_execute_reference(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t pc) {
  const JINJA_CMETA_TEMPLATE *templ = provider->instance->templ;
  const JINJA_CMETA_INSTRUCTION *instruction = &templ->instructions[pc];
  JINJA_CMETA_VALUE target;
  JINJA_CMETA_STATUS status = jinja_program_value(provider, context, instruction, &target);
  if (status != JINJA_CMETA_OK) return status;
  vstr name;
  int valid = 0;
  status = jinja_lookup_string_key(provider, &target, &name, &valid);
  if (status != JINJA_CMETA_OK) return status;
  if (!valid) return JINJA_CMETA_ERR_RENDER;
  const int extending = instruction->opcode == JINJA_CMETA_OP_EXTENDS;
  if (extending) {
    if (provider->instance->parent != NULL || provider->output_guard == NULL)
      return JINJA_CMETA_ERR_RENDER;
    for (JINJA_CMETA_TEMPLATE_INSTANCE *it = provider->instance->chain_root;
         it != NULL; it = it->parent)
      if (it->templ->env == templ->env && vstr_eq(it->templ->name, name))
        return JINJA_CMETA_ERR_RENDER;
  }
  JINJA_CMETA_TEMPLATE_INSTANCE *instance;
  status = jinja_load_instance(provider, name, !extending && !instruction->with_context, &instance);
  if (status != JINJA_CMETA_OK) return status;
  if (extending) {
    instance->chain_root = provider->instance->chain_root;
    provider->instance->parent = instance;
    provider->output_guard->suppress = 1;
    return JINJA_CMETA_OK;
  }
  if (instance->module_state != JINJA_CMETA_MODULE_READY) {
    if (instruction->with_context) {
      status = jinja_instance_context(provider, instance);
      if (status != JINJA_CMETA_OK) return status;
    }
    const size_t captures = provider->capture_depth, scopes = provider->scope_depth;
    const size_t bindings = provider->binding_count, lexical = provider->lexical_scope;
    JINJA_CMETA_ACTIVATION *activation = provider->activation;
    instance->module_state = JINJA_CMETA_MODULE_INITIALIZING;
    status = jinja_capture_begin(provider);
    if (status == JINJA_CMETA_OK) status = jinja_execute_instance(provider, instance);
    if (status == JINJA_CMETA_OK) {
      JINJA_CMETA_TEXT bytes = provider->shared.capture_buffers[provider->capture_stack[captures]];
      instance->module_body = vstr_from_buf(jinja_cmeta_text_data(&bytes), jinja_cmeta_text_length(&bytes));
      instance->module_value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_MODULE, .instance = instance};
      status = jinja_value_identify(provider, &instance->module_value);
      if (status == JINJA_CMETA_OK) instance->module_state = JINJA_CMETA_MODULE_READY;
    }
    provider->capture_depth = captures;
    provider->scope_depth = scopes;
    provider->binding_count = bindings;
    provider->lexical_scope = lexical;
    provider->activation = activation;
    if (status != JINJA_CMETA_OK) {
      instance->module_state = JINJA_CMETA_MODULE_FAILED;
      return status;
    }
  }
  if (instruction->flag < 0 ||
      (size_t)instruction->flag > (templ->instruction_count - pc - 1u) / 2u)
    return JINJA_CMETA_ERR_METADATA;
  for (size_t i = 0u; i < (size_t)instruction->flag; ++i) {
    const JINJA_CMETA_INSTRUCTION *source = &templ->instructions[pc + 1u + i * 2u];
    const JINJA_CMETA_INSTRUCTION *alias = source + 1;
    if (source->opcode != JINJA_CMETA_OP_IMPORT_NAME || alias->opcode != JINJA_CMETA_OP_IMPORT_NAME)
      return JINJA_CMETA_ERR_METADATA;
    const vstr binding_name = vstr_from_buf(templ->program_strings + alias->offset, alias->length);
    JINJA_CMETA_VALUE value = instance->module_value;
    if (instruction->opcode == JINJA_CMETA_OP_FROM) {
      status = jinja_module_export(instance,
          vstr_from_buf(templ->program_strings + source->offset, source->length), &value);
    }
    if (status == JINJA_CMETA_OK) status = jinja_binding_write(provider, binding_name, &value);
    if (status != JINJA_CMETA_OK) return status;
    const JINJA_CMETA_LEXICAL_SCOPE *scope = &templ->lexical_scopes[provider->lexical_scope];
    int found = 0;
    for (size_t j = 0u; j < scope->binding_count; ++j) {
      const size_t index = templ->cell_bindings[scope->first_binding + j].cell;
      if (!vstr_eq(templ->cells[index].name, binding_name)) continue;
      JINJA_CMETA_CELL_VALUE *cell;
      status = jinja_cmeta_activation_cell(provider->activation, index, &cell);
      if (status != JINJA_CMETA_OK) return status;
      cell->exported = 0;
      found = 1;
      break;
    }
    if (!found) return JINJA_CMETA_ERR_METADATA;
  }
  return JINJA_CMETA_OK;
}
/* Each immutable interval owns its loop/escape stacks. Normal returns balance
   scope/capture depths; a pending control transfers unwinding to the loop owner.
   Filter recursion is bounded by lexical nesting.
   FOR_NEXT is the only back edge; its index and node budget bound iteration. */
static JINJA_CMETA_STATUS jinja_execute_range_impl(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t begin, size_t end,
    unsigned max_depth, size_t *error_offset, const JINJA_CMETA_RECURSIVE_INPUT *recurse) {
  JINJA_LOOP_FRAME loops[JINJA_CMETA_MAX_BLOCK_DEPTH];
  int escaping[JINJA_CMETA_MAX_BLOCK_DEPTH];
  const JINJA_CMETA_TEMPLATE *templ = provider->instance->templ;
  const size_t entry_scope_depth = provider->scope_depth;
  const size_t entry_capture_depth = provider->capture_depth;
  size_t pc = begin, loop_depth = 0u, escaping_depth = 0u;
  JINJA_CMETA_ACTIVATION *else_activation = NULL;
  size_t else_opener = SIZE_MAX;
  if (begin > end || end > templ->instruction_count) return JINJA_CMETA_ERR_RENDER;
  while (pc < end || provider->pending_control != NULL) {
    if (provider->pending_control != NULL) {
      const JINJA_CMETA_INSTRUCTION *control = provider->pending_control;
      if (loop_depth == 0u || loops[loop_depth - 1u].opener != control->target)
        return JINJA_CMETA_OK;
      JINJA_LOOP_FRAME *frame = &loops[loop_depth - 1u];
      const JINJA_CMETA_INSTRUCTION *header = &templ->instructions[frame->opener];
      if (provider->scope_depth < (size_t)frame->scope_depth + 1u ||
          provider->capture_depth < frame->capture_depth || escaping_depth < frame->escaping_depth)
        return JINJA_CMETA_ERR_RENDER;
      while (provider->scope_depth > (size_t)frame->scope_depth + 1u) {
        JINJA_CMETA_STATUS unwind = jinja_scope_leave(provider);
        if (unwind != JINJA_CMETA_OK) return unwind;
      }
      provider->capture_depth = frame->capture_depth;
      escaping_depth = frame->escaping_depth;
      provider->autoescape = frame->autoescape;
      provider->pending_control = NULL;
      if (control->flag) {
        JINJA_CMETA_STATUS unwind = jinja_scope_leave(provider);
        if (unwind != JINJA_CMETA_OK) return unwind;
        context = frame->parent;
        pc = frame->completed_body ? header->end : header->target;
        else_activation = frame->activation;
        else_opener = frame->opener;
        --loop_depth;
      } else {
        frame->continuing = 1u;
        pc = header->target - 1u;
      }
      if (pc < begin || pc > end) return JINJA_CMETA_ERR_RENDER;
      continue;
    }
    const JINJA_CMETA_INSTRUCTION *instruction = &templ->instructions[pc];
    JINJA_CMETA_VALUE value = {0};
    JINJA_CMETA_STATUS status;
    JINJA_CMETA_NODE *node;
    int truth = 0;
    *error_offset = instruction->source_offset;
    if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
    if (instruction->depth > max_depth) return JINJA_CMETA_ERR_CAPACITY;
    switch (instruction->opcode) {
    case JINJA_CMETA_OP_IMPORT:
    case JINJA_CMETA_OP_FROM:
    case JINJA_CMETA_OP_EXTENDS:
      status = jinja_execute_reference(provider, context, pc);
      if (status != JINJA_CMETA_OK) return status;
      pc += 1u + (size_t)instruction->flag * 2u;
      break;
    case JINJA_CMETA_OP_IMPORT_NAME:
      return JINJA_CMETA_ERR_METADATA;
    case JINJA_CMETA_OP_UNSUPPORTED_REFERENCE:
      return JINJA_CMETA_ERR_UNSUPPORTED;
    case JINJA_CMETA_OP_INCLUDE:
      status = jinja_execute_include(provider, context, instruction);
      if (status != JINJA_CMETA_OK) return status;
      ++pc;
      break;
    case JINJA_CMETA_OP_BLOCK: {
      const JINJA_CMETA_FUNCTION *function = &templ->functions[instruction->target];
      if (provider->instance->parent != NULL && provider->active_block == NULL) {
        pc = function->body_end;
        break;
      }
      JINJA_CMETA_CONTEXT *block_context = provider->context;
      static const JINJA_CMETA_CALL_INPUT input = {0};
      if (function->scoped) {
        status = jinja_context_derive(provider, &block_context);
        if (status != JINJA_CMETA_OK) return status;
      }
      status = jinja_block_value(provider, provider->instance->chain_root,
          vstr_from_buf(templ->program_strings + function->name_offset, function->name_length),
          block_context, &value);
      if (status != JINJA_CMETA_OK) return status;
      status = jinja_invoke_function(provider, value.closure, &input, NULL);
      if (status != JINJA_CMETA_OK) return status;
      pc = function->body_end;
      break;
    }
    case JINJA_CMETA_OP_FUNCTION: {
      status = jinja_function_value(provider, instruction->target, &value);
      if (status != JINJA_CMETA_OK) return status;
      const JINJA_CMETA_FUNCTION *function = &templ->functions[instruction->target];
      if (function->call_expression == SIZE_MAX) {
        status = jinja_binding_write(provider,
            vstr_from_buf(templ->program_strings + function->name_offset, function->name_length), &value);
      } else {
        JINJA_CMETA_CLOSURE *saved_caller = provider->caller;
        size_t saved_expression = provider->caller_expression;
        provider->caller = value.closure;
        provider->caller_expression = function->call_expression;
        status = jinja_expression_value(provider, context, function->call_expression, 0u, &value);
        provider->caller = saved_caller;
        provider->caller_expression = saved_expression;
        if (status == JINJA_CMETA_OK) {
          node = jinja_provider_value_node(provider, &value, context);
          if (node == NULL) return provider->shared.status;
          if (jinja_dump(node, provider->autoescape && !jinja_value_is_safe(&value)
                  ? jinja_output_escaped : jinja_output_verbatim, provider, provider) != 0)
            return provider->shared.status == JINJA_CMETA_OK ? JINJA_CMETA_ERR_RENDER : provider->shared.status;
        }
      }
      if (status != JINJA_CMETA_OK) return status;
      pc = function->body_end;
      break;
    }
    case JINJA_CMETA_OP_TEXT:
      if (jinja_output_verbatim(templ->program_strings + instruction->offset,
                                 instruction->length, provider) != 0)
        return provider->shared.status == JINJA_CMETA_OK ? JINJA_CMETA_ERR_RENDER : provider->shared.status;
      ++pc;
      break;
    case JINJA_CMETA_OP_OUTPUT:
      if (jinja_output_suppressed(provider)) { ++pc; break; }
      status = jinja_program_value(provider, context, instruction, &value);
      if (status != JINJA_CMETA_OK) return status;
      if (provider->strict_undefined && value.kind == JINJA_CMETA_VALUE_UNDEFINED)
        return JINJA_CMETA_ERR_RENDER;
      if (value.kind == JINJA_CMETA_VALUE_MODULE) {
        if (value.instance == NULL || value.instance->module_state != JINJA_CMETA_MODULE_READY)
          return JINJA_CMETA_ERR_METADATA;
        if (jinja_output_verbatim(value.instance->module_body.data,
                value.instance->module_body.len, provider) != 0)
          return provider->shared.status == JINJA_CMETA_OK ? JINJA_CMETA_ERR_RENDER : provider->shared.status;
        ++pc;
        break;
      }
      node = jinja_provider_value_node(provider, &value, context);
      if (node == NULL) return provider->shared.status;
      if (jinja_dump(node, provider->autoescape && !jinja_value_is_safe(&value)
                              ? jinja_output_escaped : jinja_output_verbatim,
                      provider, provider) != 0)
        return provider->shared.status == JINJA_CMETA_OK ? JINJA_CMETA_ERR_RENDER : provider->shared.status;
      ++pc;
      break;
    case JINJA_CMETA_OP_EVALUATE:
      status = jinja_program_value(provider, context, instruction, &value);
      if (status != JINJA_CMETA_OK) return status;
      ++pc;
      break;
    case JINJA_CMETA_OP_TRANSLATE:
      if (!jinja_output_suppressed(provider)) {
        status = jinja_execute_translation(provider, context, instruction);
        if (status != JINJA_CMETA_OK) return status;
      }
      ++pc;
      break;
    case JINJA_CMETA_OP_TEST:
      status = jinja_program_value(provider, context, instruction, &value);
      if (status == JINJA_CMETA_OK) status = jinja_value_truthy(provider, &value, &truth);
      if (status != JINJA_CMETA_OK) return status;
      pc = (truth != instruction->flag) ? pc + 1u : instruction->target;
      break;
    case JINJA_CMETA_OP_AUTOESCAPE_BEGIN:
      if (escaping_depth == JINJA_CMETA_MAX_BLOCK_DEPTH) return JINJA_CMETA_ERR_CAPACITY;
      status = jinja_program_value(provider, context, instruction, &value);
      if (status == JINJA_CMETA_OK) status = jinja_value_truthy(provider, &value, &truth);
      if (status != JINJA_CMETA_OK) return status;
      status = jinja_scope_enter(provider);
      if (status == JINJA_CMETA_OK) status = jinja_scope_at(provider, pc, JINJA_CMETA_SCOPE_BODY, NULL);
      if (status != JINJA_CMETA_OK) return status;
      escaping[escaping_depth++] = provider->autoescape;
      provider->autoescape = truth != instruction->flag;
      ++pc;
      break;
    case JINJA_CMETA_OP_AUTOESCAPE_END:
      if (escaping_depth == 0u) return JINJA_CMETA_ERR_RENDER;
      status = jinja_scope_leave(provider);
      if (status != JINJA_CMETA_OK) return status;
      provider->autoescape = escaping[--escaping_depth];
      ++pc;
      break;
    case JINJA_CMETA_OP_JUMP:
      pc = instruction->target;
      break;
    case JINJA_CMETA_OP_LOOP_CONTROL:
      provider->pending_control = instruction;
      continue;
    case JINJA_CMETA_OP_ASSIGN:
      status = jinja_program_value(provider, context, instruction, &value);
      if (status == JINJA_CMETA_OK)
        status = jinja_binding_write(provider,
            vstr_from_buf(templ->program_strings + instruction->offset, instruction->length), &value);
      if (status != JINJA_CMETA_OK) return status;
      ++pc;
      break;
    case JINJA_CMETA_OP_ASSIGN_ATTRIBUTE:
    case JINJA_CMETA_OP_UNPACK:
      status = jinja_namespace_guard(provider, pc);
      if (status == JINJA_CMETA_OK) status = jinja_program_value(provider, context, instruction, &value);
      if (status == JINJA_CMETA_OK) status = jinja_unpack_assignment(provider, context, &pc, &value);
      if (status != JINJA_CMETA_OK) return status;
      break;
    case JINJA_CMETA_OP_FILTER_BEGIN:
      if (instruction->end <= pc || instruction->end >= end ||
          templ->instructions[instruction->end].opcode != JINJA_CMETA_OP_FILTER_END ||
          templ->instructions[instruction->end].target != pc)
        return JINJA_CMETA_ERR_RENDER;
      status = jinja_capture_begin(provider);
      if (status == JINJA_CMETA_OK) status = jinja_scope_at(provider, pc, JINJA_CMETA_SCOPE_BODY, NULL);
      if (status != JINJA_CMETA_OK) return status;
      status = jinja_execute_range(provider, context, pc + 1u, instruction->end,
                                   max_depth, error_offset, NULL);
      if (status != JINJA_CMETA_OK) return status;
      if (provider->pending_control != NULL) continue;
      *error_offset = templ->instructions[instruction->end].source_offset;
      status = jinja_filter_end(provider, context, pc);
      if (status != JINJA_CMETA_OK) return status;
      pc = instruction->end + 1u;
      break;
    case JINJA_CMETA_OP_FILTER_END:
      return JINJA_CMETA_ERR_RENDER;
    case JINJA_CMETA_OP_CAPTURE_BEGIN:
      status = jinja_capture_begin(provider);
      if (status == JINJA_CMETA_OK) status = jinja_scope_at(provider, pc, JINJA_CMETA_SCOPE_BODY, NULL);
      if (status != JINJA_CMETA_OK) return status;
      pc = instruction->end;
      break;
    case JINJA_CMETA_OP_CAPTURE_END:
      status = jinja_capture_end(provider, context, instruction->target);
      if (status != JINJA_CMETA_OK) return status;
      ++pc;
      break;
    case JINJA_CMETA_OP_WITH_BEGIN:
      status = jinja_with_begin(provider, context, &pc);
      if (status != JINJA_CMETA_OK) return status;
      break;
    case JINJA_CMETA_OP_SCOPE_BEGIN:
    case JINJA_CMETA_OP_SCOPE_END:
      status = instruction->opcode == JINJA_CMETA_OP_SCOPE_BEGIN
                   ? jinja_scope_enter(provider) : jinja_scope_leave(provider);
      if (status == JINJA_CMETA_OK && instruction->opcode == JINJA_CMETA_OP_SCOPE_BEGIN) {
        status = else_activation != NULL && else_opener == instruction->target
            ? jinja_scope_load(provider, jinja_scope_index(provider, instruction->target, JINJA_CMETA_SCOPE_ELSE),
                else_activation)
            : jinja_scope_at(provider, instruction->target, JINJA_CMETA_SCOPE_ELSE, NULL);
        else_activation = NULL;
        else_opener = SIZE_MAX;
      }
      if (status != JINJA_CMETA_OK) return status;
      ++pc;
      break;
    case JINJA_CMETA_OP_FOR_BEGIN: {
      else_activation = NULL;
      else_opener = SIZE_MAX;
      JINJA_CMETA_NODE *child;
      if (loop_depth == JINJA_CMETA_MAX_BLOCK_DEPTH) return JINJA_CMETA_ERR_CAPACITY;
      if (recurse != NULL && pc == begin) {
        value = *recurse->value;
        status = JINJA_CMETA_OK;
      } else status = jinja_program_value(provider, context, instruction, &value);
      if (status != JINJA_CMETA_OK) return status;
      if (value.kind == JINJA_CMETA_VALUE_UNDEFINED) {
        pc = instruction->target;
        break;
      }
      if (jinja_value_is_string(&value)) {
        /* Iteration yields plain Unicode characters, including for Markup.
         * Reuse the bounded linear decoder and its render-owned views. */
        status = jinja_materialize_list(provider, &value, &value);
        if (status != JINJA_CMETA_OK) return status;
      }
      if (value.kind == JINJA_CMETA_VALUE_LOOP) {
        status = jinja_create_iterator(provider, &value, JINJA_CMETA_ITERATOR_LOOP, &value);
        if (status != JINJA_CMETA_OK) return status;
      }
      if (!jinja_value_is_container(value.kind) && value.kind != JINJA_CMETA_VALUE_RANGE &&
          value.kind != JINJA_CMETA_VALUE_ITERATOR &&
          !(value.kind == JINJA_CMETA_VALUE_NODE && jinja_is_sequence_desc(value.node.desc)))
        return JINJA_CMETA_ERR_RENDER;
      node = jinja_provider_value_node(provider, &value, context);
      if (node == NULL) return provider->shared.status;
      node->loop_alias = templ->expressions[instruction->expression].loop_alias;
      if (instruction->flag || instruction->recursive) {
        JINJA_CMETA_NODE *filtered = instruction->flag ? jinja_provider_reserve(provider) : node;
        if (filtered == NULL) return provider->shared.status;
        JINJA_CMETA_LOOP_STATE *filter = (JINJA_CMETA_LOOP_STATE *)jinja_provider_zero(provider, 1u, sizeof(*filter));
        if (filter == NULL) return provider->shared.status;
        *filter = (JINJA_CMETA_LOOP_STATE){.next = provider->shared.loop_states,
            .instance = provider->instance, .template_context = provider->context,
            .activation = provider->activation,
            .lexical_scope = provider->lexical_scope,
            .source = instruction->flag ? node : NULL,
            .opener = pc,
            .autoescape = provider->autoescape, .context = context,
            .recursive_depth = recurse != NULL && pc == begin ? recurse->caller->recursive_depth + 1u : 0u};
        provider->shared.loop_states = filter;
        filtered->loop_state = filter;
        filtered->parent = context;
        filtered->loop_alias = node->loop_alias;
        node = filtered;
      }
      child = jinja_loop_advance(provider, node);
      if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
      if (child == NULL) {
        else_activation = NULL;
        else_opener = pc;
        pc = instruction->target;
        break;
      }
      loops[loop_depth++] = (JINJA_LOOP_FRAME){.sequence = node, .parent = context, .opener = pc,
          .scope_depth = (uint8_t)provider->scope_depth, .capture_depth = (uint8_t)provider->capture_depth,
          .escaping_depth = (uint8_t)escaping_depth, .autoescape = (uint8_t)provider->autoescape};
      context = child;
      status = jinja_scope_enter(provider);
      if (status == JINJA_CMETA_OK) status = jinja_scope_at(provider, pc, JINJA_CMETA_SCOPE_BODY, NULL);
      if (status == JINJA_CMETA_OK) status = jinja_loop_bind(provider, child);
      if (status != JINJA_CMETA_OK) return status;
      loops[loop_depth - 1u].activation = provider->activation;
      loops[loop_depth - 1u].lexical_scope = provider->lexical_scope;
      value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NODE, .node = *child};
      jinja_normalize_call_argument(&value);
      ++pc;
      status = jinja_unpack_assignment(provider, context, &pc, &value);
      if (status != JINJA_CMETA_OK) return status;
      break;
    }
    case JINJA_CMETA_OP_FOR_NEXT: {
      JINJA_LOOP_FRAME *frame;
      if (loop_depth == 0u) return JINJA_CMETA_ERR_RENDER;
      frame = &loops[loop_depth - 1u];
      if (frame->opener != instruction->target) return JINJA_CMETA_ERR_RENDER;
      if (!frame->continuing) frame->completed_body = 1u;
      frame->continuing = 0u;
      if (frame->sequence->loop_current == NULL) return JINJA_CMETA_ERR_RENDER;
      status = jinja_scope_leave(provider);
      if (status != JINJA_CMETA_OK) return status;
      node = jinja_loop_advance(provider, frame->sequence);
      if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
      if (node != NULL) {
        context = node;
        status = jinja_scope_enter(provider);
        if (status == JINJA_CMETA_OK && frame->activation != NULL)
          status = jinja_scope_load(provider, frame->lexical_scope, frame->activation);
        if (status == JINJA_CMETA_OK) status = jinja_loop_bind(provider, node);
        if (status != JINJA_CMETA_OK) return status;
        value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_NODE, .node = *node};
        jinja_normalize_call_argument(&value);
        pc = frame->opener + 1u;
        status = jinja_unpack_assignment(provider, context, &pc, &value);
        if (status != JINJA_CMETA_OK) return status;
      } else {
        context = frame->parent;
        --loop_depth;
        pc = frame->completed_body ? instruction->end : templ->instructions[frame->opener].target;
        else_activation = frame->activation;
        else_opener = frame->opener;
      }
      break;
    }
    default:
      return JINJA_CMETA_ERR_RENDER;
    }
    if (pc < begin || pc > end) return JINJA_CMETA_ERR_RENDER;
  }
  return loop_depth == 0u && escaping_depth == 0u &&
                 provider->scope_depth == entry_scope_depth &&
                 provider->capture_depth == entry_capture_depth
             ? JINJA_CMETA_OK : JINJA_CMETA_ERR_RENDER;
}

/* Reentry borrows the caller's diagnostic cursor. Successful children restore it;
 * failed children leave the innermost failing instruction visible while unwinding. */
static JINJA_CMETA_STATUS jinja_execute_range(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, size_t begin, size_t end,
    unsigned max_depth, size_t *error_offset, const JINJA_CMETA_RECURSIVE_INPUT *recurse) {
  const size_t caller_offset = *error_offset;
  JINJA_CMETA_STATUS status = jinja_execute_range_impl(provider, context, begin, end,
      max_depth, error_offset, recurse);
  if (status == JINJA_CMETA_OK) *error_offset = caller_offset;
  else jinja_record_render_error(provider, status);
  return status;
}

static JINJA_CMETA_STATUS jinja_macro_parameters(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_FUNCTION *function, const JINJA_CMETA_CALL_INPUT *input) {
  unsigned char used[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS] = {0};
  JINJA_CMETA_STATUS status;
  const JINJA_CMETA_TEMPLATE *templ = provider->instance->templ;
  int explicit_caller = 0;
  if (input->positional > function->parameter_count && !function->accepts_varargs)
    return JINJA_CMETA_ERR_RENDER;
  for (size_t i = 0u; i < function->parameter_count; ++i) {
    const JINJA_CMETA_PARAMETER *parameter = &templ->parameters[function->first_parameter + i];
    vstr name = vstr_from_buf(templ->program_strings + parameter->name_offset, parameter->name_length);
    explicit_caller |= jinja_name_equal(name.data, name.len, "caller");
    const JINJA_CMETA_VALUE *argument = i < input->positional ? &input->values[i] : NULL;
    for (size_t j = input->positional; i >= input->positional && j < input->count; ++j) {
      if (name.len != input->keywords[j].len || memcmp(name.data, input->keywords[j].data, name.len) != 0)
        continue;
      if (argument != NULL) return JINJA_CMETA_ERR_RENDER;
      argument = &input->values[j];
      used[j] = 1u;
    }
    if (argument != NULL) {
      status = jinja_binding_write(provider, name, argument);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  if (function->uses_caller && !explicit_caller) {
    JINJA_CMETA_VALUE injected = {0};
    for (size_t j = input->positional; j < input->count; ++j) {
      if (used[j] || !jinja_name_equal(input->keywords[j].data, input->keywords[j].len, "caller")) continue;
      injected = input->values[j];
      if (injected.kind == JINJA_CMETA_VALUE_NONE) injected = (JINJA_CMETA_VALUE){0};
      used[j] = 1u;
    }
    status = jinja_binding_write(provider, vstr_from_cstr("caller"), &injected);
    if (status != JINJA_CMETA_OK) return status;
  }
  JINJA_CMETA_VALUE kwargs = {0};
  if (function->accepts_kwargs) {
    status = jinja_build_mapping(provider, NULL, 0u,
        JINJA_CMETA_EXPRESSION_DICT_CALL, 0u, &kwargs, NULL);
    if (status != JINJA_CMETA_OK) return status;
  }
  for (size_t j = input->positional; j < input->count; ++j) {
    if (used[j]) continue;
    if (!function->accepts_kwargs) return JINJA_CMETA_ERR_RENDER;
    JINJA_CMETA_VALUE key = {.kind = JINJA_CMETA_VALUE_STRING, .string = input->keywords[j]};
    status = jinja_dict_snapshot_write(provider, &kwargs, &key, &input->values[j]);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (function->accepts_kwargs) {
    status = jinja_binding_write(provider, vstr_from_cstr("kwargs"), &kwargs);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (function->accepts_varargs) {
    size_t count = input->positional > function->parameter_count
        ? input->positional - function->parameter_count : 0u;
    size_t offset = provider->shared.collection_value_count;
    if (offset > provider->shared.values.limit || count > provider->shared.values.limit - offset)
      return JINJA_CMETA_ERR_CAPACITY;
    status = jinja_ensure_collection_storage(provider, count);
    if (status != JINJA_CMETA_OK) return status;
    if (count != 0u) memcpy(jinja_cmeta_values_at(&provider->shared.values, offset),
        input->values + function->parameter_count, count * sizeof(*input->values));
    provider->shared.collection_value_count += count;
    JINJA_CMETA_VALUE varargs = {.kind = JINJA_CMETA_VALUE_TUPLE, .collection_item_count = count,
        .collection_values = count ? jinja_cmeta_values_at(&provider->shared.values, offset) : NULL};
    status = jinja_binding_write(provider, vstr_from_cstr("varargs"), &varargs);
    if (status != JINJA_CMETA_OK) return status;
  }
  /* All parameters exist before evaluating defaults. Explicit Undefined is
   * bound, whereas missing parameters are materialized in declaration order. */
  for (size_t i = 0u; i < function->parameter_count; ++i) {
    const JINJA_CMETA_PARAMETER *parameter = &templ->parameters[function->first_parameter + i];
    vstr name = vstr_from_buf(templ->program_strings + parameter->name_offset, parameter->name_length);
    const JINJA_CMETA_CELL_BINDING *binding = jinja_lexical_binding(provider, name, 1);
    JINJA_CMETA_CELL_VALUE *cell;
    if (binding == NULL) return JINJA_CMETA_ERR_METADATA;
    status = jinja_cmeta_activation_cell(provider->activation, binding->cell, &cell);
    if (status != JINJA_CMETA_OK) return status;
    if (cell->bound && cell->value.kind != JINJA_CMETA_VALUE_MISSING) continue;
    JINJA_CMETA_VALUE value = {0};
    if (parameter->default_expression != SIZE_MAX) {
      provider->default_function = function;
      provider->default_parameter = i;
      status = jinja_expression_value(provider, &provider->shared.nodes[0], parameter->default_expression, 0u, &value);
      provider->default_function = NULL;
      if (status != JINJA_CMETA_OK) return status;
    }
    *cell = (JINJA_CMETA_CELL_VALUE){.bound = 1, .value = value};
  }
  return JINJA_CMETA_OK;
}

/* A NULL result streams a statement block; callable invocations capture once. */
static JINJA_CMETA_STATUS jinja_invoke_function(JINJA_CMETA_PROVIDER *provider,
    const JINJA_CMETA_CLOSURE *closure, const JINJA_CMETA_CALL_INPUT *input,
    JINJA_CMETA_VALUE *value) {
  if (closure == NULL || closure->instance == NULL ||
      closure->function >= closure->instance->templ->function_count)
    return JINJA_CMETA_ERR_METADATA;
  if (provider->shared.call_depth >= provider->shared.max_render_depth) return JINJA_CMETA_ERR_CAPACITY;
  const JINJA_CMETA_FUNCTION *function = &closure->instance->templ->functions[closure->function];
  if (function->is_block && (input->count != 0u || function->required)) return JINJA_CMETA_ERR_RENDER;
  const JINJA_CMETA_CLOSURE *saved_block = provider->active_block;
  JINJA_CMETA_TEMPLATE_INSTANCE *saved_instance = provider->instance;
  JINJA_CMETA_ACTIVATION *saved_activation = provider->activation;
  JINJA_CMETA_CONTEXT *saved_context = provider->context;
  size_t saved_lexical = provider->lexical_scope, scopes = provider->scope_depth;
  size_t bindings = provider->binding_count, captures = provider->capture_depth;
  int escaping = provider->autoescape;
  JINJA_CMETA_CLOSURE *saved_caller = provider->caller;
  const JINJA_CMETA_FUNCTION *saved_default = provider->default_function;
  size_t saved_parameter = provider->default_parameter;
  provider->default_function = NULL;
  provider->caller = NULL;
  provider->active_block = function->is_block ? closure : NULL;
  provider->instance = closure->instance;
  provider->context = closure->context;
  ++provider->shared.call_depth;
  JINJA_CMETA_STATUS status = value != NULL ? jinja_capture_begin(provider) : jinja_scope_enter(provider);
  if (status != JINJA_CMETA_OK) goto restore;
  status = jinja_scope_initialize(provider, function->scope, closure->activation);
  if (status != JINJA_CMETA_OK) goto restore;
  provider->autoescape = function->is_block
      ? closure->instance->templ->autoescape
      : closure->autoescape;
  if (!function->is_block) {
    status = jinja_macro_parameters(provider, function, input);
    if (status != JINJA_CMETA_OK) goto restore;
  }
  status = jinja_execute_range(provider, &provider->shared.nodes[0], function->body_begin, function->body_end,
      provider->shared.max_render_depth, provider->shared.error_offset, NULL);
  if (status == JINJA_CMETA_OK && value != NULL) {
    JINJA_CMETA_TEXT bytes = provider->shared.capture_buffers[provider->capture_stack[captures]];
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
        .string = vstr_from_buf(jinja_cmeta_text_data(&bytes), jinja_cmeta_text_length(&bytes)), .string_safe = escaping};
  }
restore:
  jinja_record_render_error(provider, status);
  provider->active_block = saved_block;
  /* Function locals may be captured by an escaped inner function. Only restore
   * dynamic visibility here; the render store retains the defining activation. */
  provider->instance = saved_instance;
  provider->activation = saved_activation;
  provider->context = saved_context;
  provider->lexical_scope = saved_lexical;
  provider->scope_depth = scopes;
  provider->binding_count = bindings;
  provider->capture_depth = captures;
  provider->autoescape = escaping;
  provider->caller = saved_caller;
  provider->default_function = saved_default;
  provider->default_parameter = saved_parameter;
  --provider->shared.call_depth;
  return status;
}

static JINJA_CMETA_STATUS jinja_invoke_loop(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *sequence, const JINJA_CMETA_CALL_INPUT *call, JINJA_CMETA_VALUE *value) {
  if (sequence == NULL || call->count != 1u || (call->positional == 0u &&
      !jinja_name_equal(call->keywords[0].data, call->keywords[0].len, "iterable")))
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_VALUE argument;
  JINJA_CMETA_STATUS status;
  const JINJA_CMETA_LOOP_STATE *state = sequence->loop_state;
  if (state == NULL || state->instance == NULL ||
      state->opener >= state->instance->templ->instruction_count ||
      !state->instance->templ->instructions[state->opener].recursive) return JINJA_CMETA_ERR_RENDER;
  if (state->recursive_depth + 1u >= provider->shared.max_render_depth ||
      provider->shared.call_depth >= provider->shared.max_render_depth - 1u) return JINJA_CMETA_ERR_CAPACITY;
  argument = call->values[0];
  jinja_normalize_call_argument(&argument);
  size_t scopes = provider->scope_depth;
  size_t bindings = provider->binding_count;
  size_t captures = provider->capture_depth;
  JINJA_CMETA_TEMPLATE_INSTANCE *saved_instance = provider->instance;
  JINJA_CMETA_ACTIVATION *saved_activation = provider->activation;
  JINJA_CMETA_CONTEXT *saved_context = provider->context;
  size_t saved_lexical = provider->lexical_scope;
  int escaping = provider->autoescape;
  const JINJA_CMETA_FUNCTION *saved_default = provider->default_function;
  size_t saved_parameter = provider->default_parameter;
  provider->default_function = NULL;
  ++provider->shared.call_depth;
  status = jinja_capture_begin(provider);
  if (status != JINJA_CMETA_OK) goto restore;
  provider->activation = state->activation;
  provider->instance = state->instance;
  provider->context = state->template_context;
  provider->lexical_scope = state->lexical_scope;
  provider->autoescape = state->autoescape;
  JINJA_CMETA_RECURSIVE_INPUT input = {&argument, state};
  status = jinja_execute_range(provider, state->context, state->opener,
      provider->instance->templ->instructions[state->opener].end, provider->shared.max_render_depth,
      provider->shared.error_offset, &input);
  if (status == JINJA_CMETA_OK) {
    JINJA_CMETA_TEXT bytes = provider->shared.capture_buffers[provider->capture_stack[captures]];
    *value = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_STRING,
        .string = vstr_from_buf(jinja_cmeta_text_data(&bytes), jinja_cmeta_text_length(&bytes)), .string_safe = state->autoescape};
  }
restore:
  jinja_record_render_error(provider, status);
  --provider->shared.call_depth;
  provider->autoescape = escaping;
  provider->instance = saved_instance;
  provider->activation = saved_activation;
  provider->context = saved_context;
  provider->lexical_scope = saved_lexical;
  provider->capture_depth = captures;
  provider->scope_depth = scopes;
  provider->binding_count = bindings;
  provider->default_function = saved_default;
  provider->default_parameter = saved_parameter;
  return status;
}

static JINJA_CMETA_STATUS jinja_evaluate_recursive_loop(JINJA_CMETA_PROVIDER *provider,
    JINJA_CMETA_NODE *context, const JINJA_CMETA_EXPRESSION_NODE *expression,
    size_t expression_index, size_t depth, JINJA_CMETA_VALUE *value, JINJA_CMETA_VALUE *arguments) {
  const JINJA_CMETA_VALUE *bound_loop = jinja_binding_find(provider, vstr_from_cstr("loop"));
  if (provider->shared.status != JINJA_CMETA_OK) return provider->shared.status;
  const JINJA_CMETA_NODE *loop = bound_loop != NULL && bound_loop->kind == JINJA_CMETA_VALUE_LOOP &&
      bound_loop->loop != NULL ? bound_loop->loop->loop_current : jinja_nearest_loop_node(context);
  JINJA_CMETA_STATUS status;
  if (expression->first_collection_item > provider->instance->templ->collection_item_count ||
      expression->collection_item_count > provider->instance->templ->collection_item_count - expression->first_collection_item)
    return JINJA_CMETA_ERR_RENDER;
  JINJA_CMETA_CALL_INPUT call = {0};
  status = jinja_collect_call_input(provider, context, expression, expression_index, depth, arguments, &call);
  if (status != JINJA_CMETA_OK) return status;
  if (loop == NULL || (bound_loop != NULL && bound_loop->kind != JINJA_CMETA_VALUE_LOOP))
    return JINJA_CMETA_ERR_RENDER;
  return jinja_invoke_loop(provider, loop->loop_sequence, &call, value);
}

static JINJA_CMETA_RENDER_OPTIONS jinja_render_options(const JINJA_CMETA_RENDER_OPTIONS *options) {
  JINJA_CMETA_RENDER_OPTIONS result = JINJA_CMETA_RENDER_OPTIONS_INIT;
  if (options != NULL) result = *options;
  if (result.max_nodes == 0u) result.max_nodes = JINJA_CMETA_DEFAULT_MAX_NODES;
  if (result.max_string_bytes == 0u) result.max_string_bytes = JINJA_CMETA_DEFAULT_MAX_STRING_BYTES;
  if (result.max_render_depth == 0u) result.max_render_depth = JINJA_CMETA_MAX_BLOCK_DEPTH;
  if (result.max_value_visits == 0u) result.max_value_visits = JINJA_CMETA_DEFAULT_MAX_VALUE_VISITS;
  if (result.max_value_depth == 0u) result.max_value_depth = JINJA_CMETA_MAX_VALUE_DEPTH;
  return result;
}

static JINJA_CMETA_STATUS jinja_render(const JINJA_CMETA_TEMPLATE *templ,
                                      const cmeta_data_desc *root_desc, const void *root,
                                      const JINJA_CMETA_RENDER_OPTIONS *options,
                                      const JINJA_CMETA_RENDERER *renderer, void *renderer_data,
                                      const JINJA_CMETA_RUNTIME_CONFIG *config,
                                      JINJA_CMETA_MEMORY *memory,
                                      JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_RENDER_OPTIONS resolved = jinja_render_options(options);
  JINJA_CMETA_TEMPLATE_INSTANCE root_instance = {.templ = templ, .root_visible = 1};
  root_instance.chain_root = &root_instance;
  root_instance.root_context.owner = &root_instance;
  JINJA_CMETA_PROVIDER provider = {.instance = &root_instance};
  JINJA_CMETA_MEMORY local_memory = {.limit = SIZE_MAX};
  provider.shared.memory = memory != NULL ? memory : &local_memory;
  provider.shared.values.memory = provider.shared.memory;
  JINJA_CMETA_ERROR detail = JINJA_CMETA_ERROR_INIT;
  if (error == NULL) error = &detail;
  provider.shared.error = error;
  size_t error_offset = 0u;
  const size_t max_values = config != NULL ? config->max_values : JINJA_CMETA_DEFAULT_MAX_VALUES;
  const size_t max_cells = config != NULL ? config->max_cells : JINJA_CMETA_DEFAULT_MAX_CELLS;
  const size_t max_activations = config != NULL ? config->max_activations : resolved.max_nodes;

  jinja_cmeta_error_clear(error);
  if (templ != NULL) jinja_cmeta_error_name(error, templ->name);
  if (resolved.max_value_depth > JINJA_CMETA_MAX_VALUE_DEPTH) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "max_value_depth exceeds the supported traversal stack bound");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (templ == NULL || templ->program_strings == NULL || root == NULL || renderer == NULL ||
      renderer->write == NULL ||
      !cmeta_data_desc_valid(root_desc)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
                          "render requires a template, renderer, and valid root metadata");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (resolved.max_nodes > SIZE_MAX / sizeof(*provider.shared.nodes) ||
      resolved.max_nodes > SIZE_MAX / sizeof(*provider.shared.bindings) ||
      resolved.max_nodes > SIZE_MAX / sizeof(*provider.shared.capture_buffers) ||
      resolved.max_nodes > SIZE_MAX / sizeof(*provider.shared.changed_values) ||

      max_values > SIZE_MAX / sizeof(JINJA_CMETA_VALUE)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, 0u,
                          "render workspace capacity overflows addressable memory");
    return JINJA_CMETA_ERR_CAPACITY;
  }
  provider.shared.nodes = (JINJA_CMETA_NODE *)jinja_provider_zero(&provider, resolved.max_nodes, sizeof(*provider.shared.nodes));
  if (provider.shared.nodes == NULL) {
    jinja_cmeta_error_set(error, provider.shared.status, 0u,
                          "unable to allocate the render node workspace");
    return provider.shared.status;
  }
  provider.shared.changed_values =
      (JINJA_CMETA_VALUE *)jinja_provider_zero(&provider, resolved.max_nodes, sizeof(*provider.shared.changed_values));
  if (provider.shared.changed_values == NULL) {
    jinja_cmeta_memory_drop(provider.shared.nodes);
    jinja_cmeta_error_set(error, provider.shared.status, 0u,
                          "unable to allocate the loop call workspace");
    return provider.shared.status;
  }
  provider.shared.node_capacity = resolved.max_nodes;
  provider.shared.changed_value_capacity = resolved.max_nodes;
  provider.shared.values.limit = max_values;
  provider.instance = &root_instance;
  provider.context = &provider.instance->root_context;
  provider.autoescape = templ->autoescape;
  provider.strict_undefined =
      templ->undefined_policy == JINJA_CMETA_UNDEFINED_STRICT;
  provider.shared.max_string_bytes = resolved.max_string_bytes;
  provider.shared.max_render_depth = resolved.max_render_depth;
  provider.shared.max_value_visits = resolved.max_value_visits;
  provider.shared.max_value_depth = resolved.max_value_depth;
  provider.shared.error_offset = &error_offset;
  provider.shared.status = JINJA_CMETA_OK;
  provider.shared.renderer = renderer;
  provider.shared.renderer_data = renderer_data;
  (void)jinja_provider_node(&provider, root, root_desc, NULL);

  if (provider.shared.status == JINJA_CMETA_OK) {
    provider.shared.status = jinja_cmeta_cells_init(&provider.shared.cells, templ, max_activations,
        max_cells);
    if (provider.shared.status == JINJA_CMETA_OK) {
      provider.shared.cells.memory = provider.shared.memory;
      provider.shared.status = jinja_scope_initialize(&provider, 0u, NULL);
    }
    provider.instance->root_activation = provider.activation;
  }
  if (provider.shared.status == JINJA_CMETA_OK)
    provider.shared.status = jinja_execute_template(&provider);
  if (provider.shared.status != JINJA_CMETA_OK && error->status == JINJA_CMETA_OK)
    jinja_cmeta_error_set(error, provider.shared.status, error_offset,
                          provider.shared.status == JINJA_CMETA_ERR_CAPACITY && provider.shared.value_limit_error != NULL
                              ? provider.shared.value_limit_error
                          : provider.shared.status == JINJA_CMETA_ERR_CAPACITY
                              ? "render workspace or value limit exceeded"
                          : provider.shared.status == JINJA_CMETA_ERR_METADATA
                              ? "CMeta descriptor or borrowed view is invalid"
                              : "template rendering failed");
  for (size_t i = 0u; i < provider.shared.capture_count; ++i) jinja_cmeta_text_destroy(&provider.shared.capture_buffers[i]);
  while (provider.shared.closures != NULL) {
    JINJA_CMETA_CLOSURE *closure = provider.shared.closures;
    provider.shared.closures = closure->next;
    jinja_cmeta_memory_drop(closure);
  }
  jinja_cmeta_cells_destroy(&provider.shared.cells);
  while (provider.shared.helpers != NULL) {
    JINJA_CMETA_HELPER *helper = provider.shared.helpers;
    provider.shared.helpers = helper->next;
    jinja_cmeta_memory_drop(helper);
  }
  while (provider.shared.contexts != NULL) {
    JINJA_CMETA_CONTEXT *context = provider.shared.contexts;
    provider.shared.contexts = context->next;
    jinja_cmeta_memory_drop(context);
  }
  while (provider.shared.batches != NULL) {
    JINJA_CMETA_BATCH *batch = provider.shared.batches;
    provider.shared.batches = batch->next;
    jinja_cmeta_memory_drop(batch);
  }
  while (provider.shared.transforms != NULL) {
    JINJA_CMETA_TRANSFORM *transform = provider.shared.transforms;
    provider.shared.transforms = transform->next;
    jinja_cmeta_memory_drop(transform->unique_seen);
    jinja_cmeta_memory_drop(transform);
  }
  while (provider.shared.slicers != NULL) {
    JINJA_CMETA_SLICER *slicer = provider.shared.slicers;
    provider.shared.slicers = slicer->next;
    jinja_cmeta_memory_drop(slicer);
  }
  jinja_cmeta_memory_drop(provider.shared.capture_buffers);
  jinja_cmeta_memory_drop(provider.shared.slice_bytes);
  while (provider.shared.loop_states != NULL) {
    JINJA_CMETA_LOOP_STATE *filter = provider.shared.loop_states;
    provider.shared.loop_states = filter->next;
    jinja_cmeta_memory_drop(filter);
  }
  jinja_cmeta_memory_drop(provider.shared.bindings);
  jinja_cmeta_values_destroy(&provider.shared.values);
  jinja_cmeta_memory_drop(provider.shared.changed_values);
  jinja_cmeta_memory_drop(provider.shared.call_arguments);
  jinja_cmeta_memory_drop(provider.shared.nodes);
  while (provider.shared.instances != NULL) {
    JINJA_CMETA_TEMPLATE_INSTANCE *instance = provider.shared.instances;
    provider.shared.instances = instance->next;
    jinja_cmeta_release((JINJA_CMETA_TEMPLATE *)instance->templ);
    jinja_cmeta_memory_drop(instance);
  }
  return provider.shared.status;
}

JINJA_CMETA_STATUS jinja_cmeta_render(const JINJA_CMETA_TEMPLATE *templ,
    const cmeta_data_desc *root_desc, const void *root,
    const JINJA_CMETA_RENDER_OPTIONS *options, const JINJA_CMETA_RENDERER *renderer,
    void *renderer_data, JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_STATUS status = jinja_render(templ, root_desc, root, options,
      renderer, renderer_data, NULL, NULL, error);
  return status;
}

JINJA_CMETA_STATUS jinja_cmeta_render_ex(const JINJA_CMETA_TEMPLATE *templ,
    const cmeta_data_desc *root_desc, const void *root,
    const JINJA_CMETA_RENDER_OPTIONS *options, const JINJA_CMETA_RUNTIME_CONFIG *config,
    const JINJA_CMETA_RENDERER *renderer, void *renderer_data, JINJA_CMETA_ERROR *error) {
  return jinja_render(templ, root_desc, root, options, renderer, renderer_data, config, NULL, error);
}

typedef struct JINJA_STRING_OUTPUT {
  JINJA_CMETA_TEXT bytes;
  size_t limit;
  JINJA_CMETA_STATUS status;
} JINJA_STRING_OUTPUT;

static int jinja_string_output_write(const char *text, size_t size, void *opaque) {
  JINJA_STRING_OUTPUT *output = (JINJA_STRING_OUTPUT *)opaque;
  const size_t used = jinja_cmeta_text_length(&output->bytes);
  if (output->status != JINJA_CMETA_OK) return -1;
  if (!jinja_cmeta_string_append_fits(used, size, output->limit)) {
    output->status = JINJA_CMETA_ERR_CAPACITY;
    return -1;
  }
  output->status = jinja_cmeta_text_append(&output->bytes, text, size);
  return output->status == JINJA_CMETA_OK ? 0 : -1;
}

JINJA_CMETA_STATUS jinja_cmeta_render_string(const JINJA_CMETA_TEMPLATE *templ,
    const cmeta_data_desc *root_desc, const void *root, const JINJA_CMETA_RENDER_OPTIONS *options,
    char **out_text, JINJA_CMETA_ERROR *error) {
  return jinja_cmeta_render_string_ex(templ, root_desc, root, options, NULL, out_text, error);
}

JINJA_CMETA_STATUS jinja_cmeta_render_string_ex(const JINJA_CMETA_TEMPLATE *templ,
    const cmeta_data_desc *root_desc, const void *root, const JINJA_CMETA_RENDER_OPTIONS *options,
    const JINJA_CMETA_RUNTIME_CONFIG *config, char **out_text, JINJA_CMETA_ERROR *error) {
  const JINJA_CMETA_RENDERER renderer = {jinja_string_output_write};
  JINJA_STRING_OUTPUT output = {0};
  JINJA_CMETA_MEMORY memory = {.limit = SIZE_MAX};
  JINJA_CMETA_STATUS status;

  jinja_cmeta_error_clear(error);
  if (templ != NULL) jinja_cmeta_error_name(error, templ->name);
  if (out_text == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u, "out_text must not be NULL");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  *out_text = NULL;
  output.limit = jinja_render_options(options).max_string_bytes;
  status = jinja_cmeta_text_init(&output.bytes, &memory);
  if (status != JINJA_CMETA_OK) {
    jinja_cmeta_error_set(error, status, 0u, "unable to initialize the string renderer");
    return status;
  }
  status = jinja_render(templ, root_desc, root, options, &renderer, &output, config, &memory, error);
  if (output.status != JINJA_CMETA_OK) {
    status = output.status;
    jinja_cmeta_error_set(error, status, error == NULL ? 0u : error->offset,
        "unable to retain rendered output within byte budget");
  }
  if (status == JINJA_CMETA_OK) {
    const size_t bytes = jinja_cmeta_text_length(&output.bytes) + 1u;
    void *copy = NULL;
    status = jinja_cmeta_memory_status(jinja_cmeta_memory_allocate(&memory, bytes, &copy));
    if (status == JINJA_CMETA_OK) {
      memcpy(copy, jinja_cmeta_text_data(&output.bytes), bytes);
      /* The copy and retained output overlap in the ledger until transfer. */
      jinja_cmeta_memory_disown(&memory, bytes);
      *out_text = (char *)copy;
    } else {
      jinja_cmeta_error_set(error, status, 0u, "unable to copy the rendered output");
    }
  }
  jinja_cmeta_text_destroy(&output.bytes);
  return status;
}
