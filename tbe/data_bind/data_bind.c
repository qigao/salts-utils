/**
 * @file data_bind.c
 * @brief Schema-driven pure C data binding and serialization
 */

#include "data_bind.h"
#include "data_bind_internal.h"
#include "data_bind_schema_internal.h"
#include "data_bind_temporal_adapter.h"
#include "data_bind_stream_format_state_internal.h"
#include "fmt.h"
#include "node_tree.h"
#include "re.h"
#include "schema_builtin_type.h"
#include "schema_cmeta.h"
#include "schema_parser_dsl.h"
#include "tbe_typed.h"
#include "tbe_error.h"
#include "tbe_wire.h"
#include <json_parser.h>
#include <csv_parser.h>
#include <dsv_filter.h>
#include <cyaml.h>
#include <cyaml_json_adapter.h>
#include <xml_parser/xml_parser.h>
#include <xml_parser/xml_sax.h>
#include <query_vm.h>
#include <salts_fs.h>
#include <tstr.h>
#include <salts_thread.h>
#include <salts_uuid.h>
#include <cstl.h>

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DataBindQueryStatus db_query_status_from_native(qvm_status_t status) {
  switch (status) {
  case QVM_STATUS_OK:
    return DATA_BIND_QUERY_OK;
  case QVM_STATUS_INVALID_ARGUMENT:
    return DATA_BIND_QUERY_INVALID_ARGUMENT;
  case QVM_STATUS_INVALID_PROGRAM:
    return DATA_BIND_QUERY_INVALID_PROGRAM;
  case QVM_STATUS_UNSUPPORTED:
    return DATA_BIND_QUERY_UNSUPPORTED;
  case QVM_STATUS_BACKEND_ERROR:
    return DATA_BIND_QUERY_BACKEND_ERROR;
  case QVM_STATUS_NO_MEMORY:
    return DATA_BIND_QUERY_NO_MEMORY;
  case QVM_STATUS_RESOURCE_LIMIT:
    return DATA_BIND_QUERY_RESOURCE_LIMIT;
  case QVM_STATUS_BUFFER_TOO_SMALL:
    return DATA_BIND_QUERY_BUFFER_TOO_SMALL;
  default:
    return DATA_BIND_QUERY_BACKEND_ERROR;
  }
}

/* DataBind owns diagnostics after the native query/program has been released. */
static void db_query_diagnostic_copy(DataBindQueryDiagnostic *out,
                                     const qvm_diagnostic_t *native) {
  size_t size;
  if (!out || out->size < sizeof(*out) || !native) return;
  size = out->size;
  *out = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  out->size = size;
  out->status = db_query_status_from_native(native->status);
  out->instruction = native->instruction;
  out->opcode = native->opcode;
  out->operand = native->operand;
  snprintf(out->message, sizeof(out->message), "%s", native->message ? native->message : "");
}

static qvm_limits_t db_query_native_limits(const DataBindQueryLimits *limits) {
  if (!limits) return qvm_default_limits();
  return (qvm_limits_t){limits->max_instructions, limits->max_operands,
                        limits->max_regexes, limits->max_steps};
}

typedef enum data_bind_wire_type {
  DB_WIRE_UNDEFINED,
  DB_WIRE_U8,
  DB_WIRE_I8,
  DB_WIRE_U16,
  DB_WIRE_I16,
  DB_WIRE_U32,
  DB_WIRE_I32,
  DB_WIRE_U64,
  DB_WIRE_I64,
  DB_WIRE_F32,
  DB_WIRE_F64
} data_bind_wire_type_t;

typedef struct {
  const char *name;
  int size;
  data_bind_wire_type_t wire_type;
  unsigned char is_float : 1;
  unsigned char is_64 : 1;
} type_meta_t;

/* Small object pool for frequently allocated DataBindValue nodes */
#define VALUE_POOL_SIZE 64
#define DATA_BIND_FILE_STREAM_CHUNK_SIZE 65536
#define DATA_BIND_JSON_STREAM_MAX_DEPTH 256U
enum data_bind_json_path_stream_mode {
  DATA_BIND_JSON_PATH_STREAM_NONE = 0,
  DATA_BIND_JSON_PATH_STREAM_FIRST,
  DATA_BIND_JSON_PATH_STREAM_ALL
};

/*
 * Fixed atomic slots avoid the ABA reclamation problem of a shared lock-free
 * linked list. A ready bitmap avoids scanning empty/full pools; operations are
 * otherwise bounded by VALUE_POOL_SIZE.
 */
#define VALUE_POOL_FULL_MASK UINT64_MAX
_Static_assert(VALUE_POOL_SIZE == sizeof(uint64_t) * CHAR_BIT,
               "value pool bitmap must cover every slot");

static _Atomic(DataBindValue *) g_value_pool_slots[VALUE_POOL_SIZE];
static uintptr_t g_value_pool_closed_slot_storage;
#define VALUE_POOL_CLOSED_SLOT ((DataBindValue *)(void *)&g_value_pool_closed_slot_storage)

enum value_pool_state { VALUE_POOL_DISABLED = 0, VALUE_POOL_ENABLED };

static salts_mutex_t g_value_pool_control_mutex;
static atomic_int g_value_pool_state = VALUE_POOL_ENABLED;
static _Atomic uint64_t g_value_pool_ready_mask;
static atomic_size_t g_value_pool_allocated_count;
static atomic_size_t g_value_pool_reused_count;
static salts_once_t g_value_pool_once = SALTS_ONCE_INIT;
static SALTS_THREAD_LOCAL size_t g_value_pool_take_cursor;
static SALTS_THREAD_LOCAL size_t g_value_pool_put_cursor;

static void value_pool_init_once(void) { salts_mutex_init(&g_value_pool_control_mutex); }

static int value_pool_is_enabled(void) {
  return atomic_load_explicit(&g_value_pool_state, memory_order_acquire) == VALUE_POOL_ENABLED;
}

static DataBindValue *value_pool_take(void) {
  size_t attempts;

  /* Bounded rescan: a benign concurrent claim falls back to direct allocation
   * instead of mutating the process-global pool policy. */
  for (attempts = 0; attempts < VALUE_POOL_SIZE * 2u; ++attempts) {
    uint64_t ready = atomic_load_explicit(&g_value_pool_ready_mask, memory_order_acquire);
    size_t offset;
    size_t start = g_value_pool_take_cursor;
    uint64_t bit = 0;
    size_t slot = 0;
    DataBindValue *value;

    if (ready == 0) return NULL;
    for (offset = 0; offset < VALUE_POOL_SIZE; ++offset) {
      slot = (start + offset) % VALUE_POOL_SIZE;
      bit = UINT64_C(1) << slot;
      if ((ready & bit) != 0) break;
    }
    if (offset == VALUE_POOL_SIZE) return NULL;

    if (!atomic_compare_exchange_strong_explicit(&g_value_pool_ready_mask, &ready, ready & ~bit,
                                                 memory_order_acq_rel, memory_order_acquire)) {
      /* Another take or the disable path changed the bitmap; rescan. */
      continue;
    }

    value = atomic_load_explicit(&g_value_pool_slots[slot], memory_order_acquire);
    if (value != NULL && value != VALUE_POOL_CLOSED_SLOT &&
        atomic_compare_exchange_strong_explicit(&g_value_pool_slots[slot], &value, NULL,
                                                memory_order_acquire, memory_order_relaxed)) {
      g_value_pool_take_cursor = (slot + 1U) % VALUE_POOL_SIZE;
      return value;
    }
    /* The claimed slot was closed or raced away; the cleared bit keeps the
     * scan making progress. */
  }
  return NULL;
}

static int value_pool_put(DataBindValue *value) {
  uint64_t ready = atomic_load_explicit(&g_value_pool_ready_mask, memory_order_relaxed);
  size_t offset;
  size_t start = g_value_pool_put_cursor;

  if (ready == VALUE_POOL_FULL_MASK) return 0;
  for (offset = 0; offset < VALUE_POOL_SIZE; ++offset) {
    size_t slot = (start + offset) % VALUE_POOL_SIZE;
    DataBindValue *expected = NULL;
    if (atomic_compare_exchange_strong_explicit(&g_value_pool_slots[slot], &expected, value,
                                                memory_order_release, memory_order_relaxed)) {
      g_value_pool_put_cursor = (slot + 1U) % VALUE_POOL_SIZE;
      atomic_fetch_or_explicit(&g_value_pool_ready_mask, UINT64_C(1) << slot, memory_order_release);
      return 1;
    }
  }
  return 0;
}

struct DataBind {
  Node *schema_root;
  uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE];
};

struct data_bind_stream_t {
  DataBind *codec;
  char *type_name;
  char *path_or_expr;
  DataBindValue **out_value;
  DataBindValue *internal_out_value;
  DataBindError *error;
  DataBindRecordFn record_callback;
  void *record_callback_user;
  uint64_t record_callback_index;
  DataBindStreamOutputMode output_mode;
  DataBindStreamLimits limits;
  DataBindQueryLimits query_limits;
  DataBindQueryDiagnostic query_diagnostic;
  int query_limits_configured;
  size_t total_input_bytes;
  size_t result_count;
  DataBindStatus (*feed_fn)(data_bind_stream_t *parser, const char *data, size_t len,
                            DataBindError *error);
  DataBindStatus (*finish_fn)(data_bind_stream_t *parser, DataBindValue **out_value,
                              DataBindError *error);
  DataBindStatus (*bind_fn)(DataBind *codec, const char *type_name, const char *text, size_t len,
                            const char *path, const DataBindQueryLimits *query_limits,
                            DataBindQueryDiagnostic *query_diagnostic,
                            DataBindValue **out_value, DataBindError *error);
  char *buffer;
  size_t size;
  size_t capacity;
  DataBindValue *csv_values;
  DataBindValue *stream_values;
  data_bind_stream_format_state *format_state;
  int finished;
  int started;
  int record_callback_stopped;
  int record_callback_failed;
  int limit_failed;
  int canceled;
};

static DataBindStatus data_bind_query_failure_status(
    const DataBindQueryDiagnostic *diagnostic);

typedef struct db_dynamic_type db_dynamic_type_t;

typedef struct db_dynamic_field_type {
  char *stable_name;
  const db_dynamic_type_t *value_type;
} db_dynamic_field_type_t;

struct db_dynamic_type {
  cmeta_data_kind kind;
  const cmeta_data_desc *canonical_data;
  cmeta_type_identity owned_identity;
  const cmeta_type_identity *identity;
  char *semantic_key;
  char *owned_stable_id;
  db_dynamic_field_type_t *fields;
  size_t field_count;
  const db_dynamic_type_t *element_type;
  const db_dynamic_type_t *key_type;
  const db_dynamic_type_t *value_type;
  int building;
  int complete;
};

struct db_dynamic_graph {
  size_t references;
  db_dynamic_type_t *nodes;
  size_t node_count;
  size_t node_capacity;
};

struct DataBindObject {
  char *type_name;
  DataBindValue *value;
  uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE];
};

typedef enum {
  EF_INT,
  EF_U32,
  EF_I64,
  EF_U64,
  EF_DBL,
  EF_BOOL,
  EF_UUID,
  EF_STR,
  EF_FIX_BYTES,
  EF_VAR_BYTES,
  EF_LIST_INT,
  EF_LIST_U32,
  EF_LIST_I64,
  EF_LIST_U64,
  EF_LIST_DBL,
  EF_LIST_BOOL,
  EF_LIST_STR,
  EF_LIST_OBJ,
  EF_SET_INT,
  EF_SET_U32,
  EF_SET_I64,
  EF_SET_U64,
  EF_SET_DBL,
  EF_SET_BOOL,
  EF_SET_STR,
  EF_MAP_STR_STR,
  EF_MAP_STR_INT,
  EF_MAP_STR_U32,
  EF_MAP_STR_I64,
  EF_MAP_STR_U64,
  EF_MAP_STR_DBL,
  EF_MAP_STR_BOOL,
  EF_GROUP,
  EF_OBJECT
} emit_kind_t;

typedef struct emit_field emit_field_t;
typedef struct {
  emit_field_t *items;
  size_t count;
  size_t capacity;
} emit_field_array_t;
struct emit_field {
  char *name;
  emit_kind_t kind;
  int size;
  data_bind_wire_type_t wire_type;
  unsigned char is_float : 1;
  unsigned char is_64 : 1;
  unsigned char has_set_bytes : 1;
  size_t fixed_count;
  int group_dim;
  emit_field_array_t children;
};

/* Derived view over the shared schema_builtin_type.h table. The init writes are
 * idempotent, so a concurrent duplicate init is benign; the array and the table
 * never move afterwards. */
#define TYPE_META_COUNT (sizeof(SCHEMA_BUILTIN_TYPES) / sizeof(SCHEMA_BUILTIN_TYPES[0]))
static type_meta_t g_type_metas[TYPE_META_COUNT];
static atomic_int g_type_metas_ready = 0;

static data_bind_wire_type_t db_wire_type_from_reader(const char *reader) {
  if (reader == NULL) return DB_WIRE_UNDEFINED;
  if (strcmp(reader, "u8") == 0) return DB_WIRE_U8;
  if (strcmp(reader, "i8") == 0) return DB_WIRE_I8;
  if (strcmp(reader, "u16") == 0) return DB_WIRE_U16;
  if (strcmp(reader, "i16") == 0) return DB_WIRE_I16;
  if (strcmp(reader, "u32") == 0) return DB_WIRE_U32;
  if (strcmp(reader, "i32") == 0) return DB_WIRE_I32;
  if (strcmp(reader, "u64") == 0) return DB_WIRE_U64;
  if (strcmp(reader, "i64") == 0) return DB_WIRE_I64;
  if (strcmp(reader, "f32") == 0) return DB_WIRE_F32;
  if (strcmp(reader, "f64") == 0) return DB_WIRE_F64;
  return DB_WIRE_UNDEFINED;
}

static const type_meta_t *find_type_meta(const char *type) {
  const schema_builtin_type_info_t *info = schema_builtin_type_find(type);
  size_t index;
  size_t i;
  if (info == NULL) return NULL;
  if (!atomic_load_explicit(&g_type_metas_ready, memory_order_acquire)) {
    salts_once(&g_value_pool_once, value_pool_init_once);
    salts_mutex_lock(&g_value_pool_control_mutex);
    if (!atomic_load_explicit(&g_type_metas_ready, memory_order_relaxed)) {
      for (i = 0; i < TYPE_META_COUNT; ++i) {
        const schema_builtin_type_info_t *src = &SCHEMA_BUILTIN_TYPES[i];
        g_type_metas[i].name = src->name;
        g_type_metas[i].size = (int)src->size;
        g_type_metas[i].wire_type = db_wire_type_from_reader(src->wire_reader);
        g_type_metas[i].is_float = src->data->kind == CMETA_DATA_FLOAT;
        g_type_metas[i].is_64 =
            (g_type_metas[i].wire_type == DB_WIRE_U64 || g_type_metas[i].wire_type == DB_WIRE_I64);
      }
      atomic_store_explicit(&g_type_metas_ready, 1, memory_order_release);
    }
    salts_mutex_unlock(&g_value_pool_control_mutex);
  }
  index = (size_t)(info - SCHEMA_BUILTIN_TYPES);
  return &g_type_metas[index];
}

static char *dbv_strdup(const char *src) {
  size_t len;
  char *dst;
  if (src == NULL) return NULL;
  len = strlen(src) + 1;
  dst = (char *)malloc(len);
  if (dst == NULL) return NULL;
  memcpy(dst, src, len);
  return dst;
}

static db_dynamic_graph_t *db_dynamic_graph_retain(db_dynamic_graph_t *graph) {
  if (graph == NULL || graph->references == SIZE_MAX) return NULL;
  graph->references++;
  return graph;
}

static void db_dynamic_graph_release(db_dynamic_graph_t *graph) {
  size_t i;
  size_t field_index;
  if (graph == NULL || graph->references == 0u) return;
  graph->references--;
  if (graph->references != 0u) return;
  for (i = 0u; i < graph->node_count; ++i) {
    for (field_index = 0u; field_index < graph->nodes[i].field_count;
         ++field_index)
      free(graph->nodes[i].fields[field_index].stable_name);
    free(graph->nodes[i].fields);
    free(graph->nodes[i].owned_stable_id);
    free(graph->nodes[i].semantic_key);
  }
  free(graph->nodes);
  free(graph);
}

static DataBindStatus db_dynamic_attach_root(DataBind *codec,
                                             const char *root_type,
                                             int synthetic_sequence,
                                             DataBindValue *value);

static DataBindValue *dbv_retain(DataBindValue *value) {
  if (value == NULL || value->references == SIZE_MAX) return NULL;
  value->references++;
  return value;
}

static void dbv_release(DataBindValue *value);
static DataBindValue *dbv_string(const char *value);

static bool db_owned_value_slot_copy(void *destination_, const void *source_) {
  db_owned_value_slot_t *destination = (db_owned_value_slot_t *)destination_;
  const db_owned_value_slot_t *source = (const db_owned_value_slot_t *)source_;
  if (destination == NULL || source == NULL) return false;
  destination->value = dbv_retain(source->value);
  return source->value == NULL || destination->value != NULL;
}

static void db_owned_value_slot_move(void *destination_, void *source_) {
  db_owned_value_slot_t *destination = (db_owned_value_slot_t *)destination_;
  db_owned_value_slot_t *source = (db_owned_value_slot_t *)source_;
  destination->value = source->value;
  source->value = NULL;
}

static void db_owned_value_slot_destroy(void *slot_) {
  db_owned_value_slot_t *slot = (db_owned_value_slot_t *)slot_;
  dbv_release(slot->value);
  slot->value = NULL;
}

static bool db_field_slot_copy(void *destination_, const void *source_) {
  db_field_slot_t *destination = (db_field_slot_t *)destination_;
  const db_field_slot_t *source = (const db_field_slot_t *)source_;
  if (destination == NULL || source == NULL) return false;
  memset(destination, 0, sizeof(*destination));
  if (source->name != NULL) {
    destination->name = dbv_strdup(source->name);
    if (destination->name == NULL) return false;
  }
  destination->value = dbv_retain(source->value);
  if (source->value != NULL && destination->value == NULL) {
    free(destination->name);
    destination->name = NULL;
    return false;
  }
  return true;
}

static void db_field_slot_move(void *destination_, void *source_) {
  db_field_slot_t *destination = (db_field_slot_t *)destination_;
  db_field_slot_t *source = (db_field_slot_t *)source_;
  destination->name = source->name;
  destination->value = source->value;
  source->name = NULL;
  source->value = NULL;
}

static void db_field_slot_destroy(void *slot_) {
  db_field_slot_t *slot = (db_field_slot_t *)slot_;
  free(slot->name);
  slot->name = NULL;
  dbv_release(slot->value);
  slot->value = NULL;
}

static const cmeta_type_traits DB_OWNED_VALUE_SLOT_TRAITS = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL,
    NULL,
    NULL,
    db_owned_value_slot_copy,
    db_owned_value_slot_move,
    db_owned_value_slot_destroy};

static const cmeta_type_desc DB_OWNED_VALUE_SLOT_TYPE = {
    "salts-utils.databind.owned-value-slot",
    sizeof(db_owned_value_slot_t),
    _Alignof(db_owned_value_slot_t),
    CMETA_T_OBJECT,
    NULL,
    &DB_OWNED_VALUE_SLOT_TRAITS,
    NULL};

static const cmeta_type_traits DB_FIELD_SLOT_TRAITS = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL,
    NULL,
    NULL,
    db_field_slot_copy,
    db_field_slot_move,
    db_field_slot_destroy};

static const cmeta_type_desc DB_FIELD_SLOT_TYPE = {
    "salts-utils.databind.field-slot", sizeof(db_field_slot_t),
    _Alignof(db_field_slot_t), CMETA_T_OBJECT, NULL, &DB_FIELD_SLOT_TRAITS, NULL};

#define DATA_BIND_SEMANTIC_MAX_DEPTH 64u
#define DATA_BIND_SEMANTIC_HASH_OFFSET UINT64_C(1469598103934665603)
#define DATA_BIND_SEMANTIC_HASH_PRIME UINT64_C(1099511628211)

static const vec_t *dbv_ordered_values_const(const DataBindValue *value) {
  if (value == NULL) return NULL;
  if (value->kind == DATA_BIND_VALUE_LIST) return &value->data.sequence.values;
  if (value->kind == DATA_BIND_VALUE_SET) return &value->data.set.ordered_values;
  return NULL;
}

static vec_t *dbv_ordered_values(DataBindValue *value) {
  return (vec_t *)dbv_ordered_values_const(value);
}

static uint64_t dbv_hash_bytes(const void *data, size_t len) {
  const unsigned char *bytes = (const unsigned char *)data;
  uint64_t hash = DATA_BIND_SEMANTIC_HASH_OFFSET;
  size_t i;
  if (data == NULL && len != 0u) return 0u;
  for (i = 0u; i < len; ++i) {
    hash ^= bytes[i];
    hash *= DATA_BIND_SEMANTIC_HASH_PRIME;
  }
  return hash;
}

static uint64_t dbv_hash_mix(uint64_t hash, uint64_t value) {
  hash ^= value;
  hash *= DATA_BIND_SEMANTIC_HASH_PRIME;
  return hash;
}

static bool dbv_semantic_equal_depth(const DataBindValue *left,
                                     const DataBindValue *right,
                                     size_t depth);
static uint64_t dbv_semantic_hash_depth(const DataBindValue *value,
                                        size_t depth);

static bool dbv_semantic_sequence_equal(const vec_t *left, const vec_t *right,
                                        size_t depth) {
  size_t i;
  if (left == NULL || right == NULL || vec_size(left) != vec_size(right))
    return false;
  for (i = 0u; i < vec_size(left); ++i) {
    const db_owned_value_slot_t *left_slot =
        (const db_owned_value_slot_t *)vec_at_const(left, i);
    const db_owned_value_slot_t *right_slot =
        (const db_owned_value_slot_t *)vec_at_const(right, i);
    if (left_slot == NULL || right_slot == NULL ||
        !dbv_semantic_equal_depth(left_slot->value, right_slot->value,
                                  depth + 1u))
      return false;
  }
  return true;
}

static bool dbv_semantic_set_equal(const vec_t *left, const vec_t *right,
                                   size_t depth) {
  size_t i;
  size_t j;
  if (left == NULL || right == NULL || vec_size(left) != vec_size(right))
    return false;
  for (i = 0u; i < vec_size(left); ++i) {
    const db_owned_value_slot_t *left_slot =
        (const db_owned_value_slot_t *)vec_at_const(left, i);
    bool found = false;
    if (left_slot == NULL || left_slot->value == NULL) return false;
    for (j = 0u; j < vec_size(right); ++j) {
      const db_owned_value_slot_t *right_slot =
          (const db_owned_value_slot_t *)vec_at_const(right, j);
      if (right_slot != NULL &&
          dbv_semantic_equal_depth(left_slot->value, right_slot->value,
                                   depth + 1u)) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

static bool dbv_semantic_object_equal(const DataBindValue *left,
                                      const DataBindValue *right,
                                      size_t depth) {
  const vec_t *left_fields = &left->data.object.fields;
  const vec_t *right_fields = &right->data.object.fields;
  size_t i;
  if (vec_size(left_fields) != vec_size(right_fields)) return false;
  for (i = 0u; i < vec_size(left_fields); ++i) {
    const db_field_slot_t *left_field =
        (const db_field_slot_t *)vec_at_const(left_fields, i);
    const db_field_slot_t *right_field =
        (const db_field_slot_t *)vec_at_const(right_fields, i);
    if (left_field == NULL || right_field == NULL || left_field->name == NULL ||
        right_field->name == NULL || strcmp(left_field->name, right_field->name) != 0 ||
        !dbv_semantic_equal_depth(left_field->value, right_field->value,
                                  depth + 1u))
      return false;
  }
  return true;
}

static bool dbv_semantic_map_equal(const DataBindValue *left,
                                   const DataBindValue *right,
                                   size_t depth) {
  const vec_t *left_entries = &left->data.map.ordered_entries;
  const vec_t *right_entries = &right->data.map.ordered_entries;
  size_t i;
  size_t j;
  if (vec_size(left_entries) != vec_size(right_entries)) return false;
  for (i = 0u; i < vec_size(left_entries); ++i) {
    const db_map_entry_slot_t *left_entry =
        (const db_map_entry_slot_t *)vec_at_const(left_entries, i);
    bool found = false;
    if (left_entry == NULL || left_entry->key_value == NULL ||
        left_entry->value == NULL)
      return false;
    for (j = 0u; j < vec_size(right_entries); ++j) {
      const db_map_entry_slot_t *right_entry =
          (const db_map_entry_slot_t *)vec_at_const(right_entries, j);
      if (right_entry != NULL && right_entry->key_value != NULL &&
          right_entry->value != NULL &&
          dbv_semantic_equal_depth(left_entry->key_value,
                                   right_entry->key_value, depth + 1u) &&
          dbv_semantic_equal_depth(left_entry->value, right_entry->value,
                                   depth + 1u)) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

static bool dbv_semantic_equal_depth(const DataBindValue *left,
                                     const DataBindValue *right,
                                     size_t depth) {
  if (left == NULL || right == NULL || depth > DATA_BIND_SEMANTIC_MAX_DEPTH)
    return false;
  if ((left->type_identity == NULL) != (right->type_identity == NULL)) return false;
  if (left->type_identity != NULL &&
      !cmeta_type_identity_equal(left->type_identity, right->type_identity))
    return false;
  if (left->kind != right->kind) return false;
  switch (left->kind) {
  case DATA_BIND_VALUE_NULL:
    return true;
  case DATA_BIND_VALUE_INT:
    return left->data.int_val == right->data.int_val;
  case DATA_BIND_VALUE_INT64:
    return left->data.int64_val == right->data.int64_val;
  case DATA_BIND_VALUE_UINT64:
    return left->data.uint64_val == right->data.uint64_val;
  case DATA_BIND_VALUE_DOUBLE:
    return cmeta_traits_double.equal(&left->data.double_val,
                                     &right->data.double_val);
  case DATA_BIND_VALUE_BOOL:
    return (left->data.bool_val != 0) == (right->data.bool_val != 0);
  case DATA_BIND_VALUE_STRING:
    return left->data.string_val.len == right->data.string_val.len &&
           (left->data.string_val.len == 0u ||
            (left->data.string_val.ptr != NULL && right->data.string_val.ptr != NULL &&
             memcmp(left->data.string_val.ptr, right->data.string_val.ptr,
                    left->data.string_val.len) == 0));
  case DATA_BIND_VALUE_BYTES:
    return left->data.bytes_val.len == right->data.bytes_val.len &&
           (left->data.bytes_val.len == 0u ||
            (left->data.bytes_val.ptr != NULL && right->data.bytes_val.ptr != NULL &&
             memcmp(left->data.bytes_val.ptr, right->data.bytes_val.ptr,
                    left->data.bytes_val.len) == 0));
  case DATA_BIND_VALUE_UUID:
    return memcmp(left->data.uuid_val.bytes, right->data.uuid_val.bytes,
                  sizeof(left->data.uuid_val.bytes)) == 0;
  case DATA_BIND_VALUE_DATETIME:
    return left->data.datetime_val.year == right->data.datetime_val.year &&
           left->data.datetime_val.month == right->data.datetime_val.month &&
           left->data.datetime_val.day == right->data.datetime_val.day &&
           left->data.datetime_val.hour == right->data.datetime_val.hour &&
           left->data.datetime_val.minute == right->data.datetime_val.minute &&
           left->data.datetime_val.second == right->data.datetime_val.second &&
           left->data.datetime_val.millisecond == right->data.datetime_val.millisecond &&
           left->data.datetime_val.tz_offset == right->data.datetime_val.tz_offset &&
           left->data.datetime_val.has_tz == right->data.datetime_val.has_tz &&
           left->data.datetime_val.day_of_week == right->data.datetime_val.day_of_week;
  case DATA_BIND_VALUE_DATE:
    return left->data.date_val.year == right->data.date_val.year &&
           left->data.date_val.month == right->data.date_val.month &&
           left->data.date_val.day == right->data.date_val.day;
  case DATA_BIND_VALUE_TIME:
    return left->data.time_val.hour == right->data.time_val.hour &&
           left->data.time_val.minute == right->data.time_val.minute &&
           left->data.time_val.second == right->data.time_val.second &&
           left->data.time_val.millisecond == right->data.time_val.millisecond;
  case DATA_BIND_VALUE_DURATION:
    return left->data.duration_ms == right->data.duration_ms;
  case DATA_BIND_VALUE_DECIMAL:
    return left->data.decimal_val.mantissa == right->data.decimal_val.mantissa &&
           left->data.decimal_val.scale == right->data.decimal_val.scale;
  case DATA_BIND_VALUE_BIGINT:
    return left->data.bigint_val.ptr != NULL && right->data.bigint_val.ptr != NULL &&
           strcmp(left->data.bigint_val.ptr, right->data.bigint_val.ptr) == 0;
  case DATA_BIND_VALUE_MONEY:
    return left->data.money_val.amount.mantissa ==
               right->data.money_val.amount.mantissa &&
           left->data.money_val.amount.scale == right->data.money_val.amount.scale &&
           memcmp(left->data.money_val.currency, right->data.money_val.currency,
                  sizeof(left->data.money_val.currency)) == 0;
  case DATA_BIND_VALUE_OBJECT:
    return dbv_semantic_object_equal(left, right, depth);
  case DATA_BIND_VALUE_LIST:
    return dbv_semantic_sequence_equal(&left->data.sequence.values,
                                       &right->data.sequence.values, depth);
  case DATA_BIND_VALUE_SET:
    return dbv_semantic_set_equal(&left->data.set.ordered_values,
                                  &right->data.set.ordered_values, depth);
  case DATA_BIND_VALUE_MAP:
    return dbv_semantic_map_equal(left, right, depth);
  default:
    return false;
  }
}

static uint64_t dbv_semantic_hash_sequence(const vec_t *values, size_t depth,
                                           bool ordered) {
  uint64_t hash = dbv_hash_mix(DATA_BIND_SEMANTIC_HASH_OFFSET,
                               values != NULL ? vec_size(values) : 0u);
  uint64_t unordered = 0u;
  size_t i;
  if (values == NULL) return hash;
  for (i = 0u; i < vec_size(values); ++i) {
    const db_owned_value_slot_t *slot =
        (const db_owned_value_slot_t *)vec_at_const(values, i);
    uint64_t child_hash = slot != NULL
                              ? dbv_semantic_hash_depth(slot->value, depth + 1u)
                              : 0u;
    if (ordered)
      hash = dbv_hash_mix(hash, child_hash);
    else
      unordered ^= dbv_hash_mix(DATA_BIND_SEMANTIC_HASH_OFFSET, child_hash);
  }
  return ordered ? hash : dbv_hash_mix(hash, unordered);
}

static uint64_t dbv_semantic_hash_depth(const DataBindValue *value,
                                        size_t depth) {
  uint64_t hash;
  size_t i;
  if (value == NULL || depth > DATA_BIND_SEMANTIC_MAX_DEPTH) return 0u;
  hash = dbv_hash_mix(DATA_BIND_SEMANTIC_HASH_OFFSET, (uint64_t)value->kind);
  switch (value->kind) {
  case DATA_BIND_VALUE_NULL:
    return hash;
  case DATA_BIND_VALUE_INT:
    return dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.int_val);
  case DATA_BIND_VALUE_INT64:
    return dbv_hash_mix(hash, (uint64_t)value->data.int64_val);
  case DATA_BIND_VALUE_UINT64:
    return dbv_hash_mix(hash, value->data.uint64_val);
  case DATA_BIND_VALUE_DOUBLE:
    return dbv_hash_mix(hash, cmeta_traits_double.hash(&value->data.double_val));
  case DATA_BIND_VALUE_BOOL:
    return dbv_hash_mix(hash, value->data.bool_val != 0 ? 1u : 0u);
  case DATA_BIND_VALUE_STRING:
    return dbv_hash_mix(hash, dbv_hash_bytes(value->data.string_val.ptr,
                                             value->data.string_val.len));
  case DATA_BIND_VALUE_BYTES:
    return dbv_hash_mix(hash, dbv_hash_bytes(value->data.bytes_val.ptr,
                                             value->data.bytes_val.len));
  case DATA_BIND_VALUE_UUID:
    return dbv_hash_mix(hash, dbv_hash_bytes(value->data.uuid_val.bytes,
                                             sizeof(value->data.uuid_val.bytes)));
  case DATA_BIND_VALUE_DATETIME: {
    const DataBindDateTime *datetime = &value->data.datetime_val;
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->year);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->month);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->day);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->hour);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->minute);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->second);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->millisecond);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->tz_offset);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->has_tz);
    return dbv_hash_mix(hash, (uint64_t)(uint32_t)datetime->day_of_week);
  }
  case DATA_BIND_VALUE_DATE:
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.date_val.year);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.date_val.month);
    return dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.date_val.day);
  case DATA_BIND_VALUE_TIME:
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.time_val.hour);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.time_val.minute);
    hash = dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.time_val.second);
    return dbv_hash_mix(hash,
                        (uint64_t)(uint32_t)value->data.time_val.millisecond);
  case DATA_BIND_VALUE_DURATION:
    return dbv_hash_mix(hash, (uint64_t)value->data.duration_ms);
  case DATA_BIND_VALUE_DECIMAL:
    hash = dbv_hash_mix(hash, (uint64_t)value->data.decimal_val.mantissa);
    return dbv_hash_mix(hash, (uint64_t)(uint32_t)value->data.decimal_val.scale);
  case DATA_BIND_VALUE_BIGINT:
    return dbv_hash_mix(hash,
                        dbv_hash_bytes(value->data.bigint_val.ptr,
                                       value->data.bigint_val.ptr != NULL
                                           ? strlen(value->data.bigint_val.ptr)
                                           : 0u));
  case DATA_BIND_VALUE_MONEY:
    hash = dbv_hash_mix(hash, (uint64_t)value->data.money_val.amount.mantissa);
    hash = dbv_hash_mix(hash,
                        (uint64_t)(uint32_t)value->data.money_val.amount.scale);
    return dbv_hash_mix(hash,
                        dbv_hash_bytes(value->data.money_val.currency,
                                       sizeof(value->data.money_val.currency)));
  case DATA_BIND_VALUE_OBJECT:
    hash = dbv_hash_mix(hash, vec_size(&value->data.object.fields));
    for (i = 0u; i < vec_size(&value->data.object.fields); ++i) {
      const db_field_slot_t *field = (const db_field_slot_t *)vec_at_const(
          &value->data.object.fields, i);
      if (field == NULL || field->name == NULL) return 0u;
      hash = dbv_hash_mix(hash, dbv_hash_bytes(field->name, strlen(field->name)));
      hash = dbv_hash_mix(hash,
                          dbv_semantic_hash_depth(field->value, depth + 1u));
    }
    return hash;
  case DATA_BIND_VALUE_LIST:
    return dbv_semantic_hash_sequence(&value->data.sequence.values, depth, true);
  case DATA_BIND_VALUE_SET:
    return dbv_semantic_hash_sequence(&value->data.set.ordered_values, depth, false);
  case DATA_BIND_VALUE_MAP: {
    const vec_t *entries = &value->data.map.ordered_entries;
    uint64_t unordered = 0u;
    hash = dbv_hash_mix(hash, vec_size(entries));
    for (i = 0u; i < vec_size(entries); ++i) {
      const db_map_entry_slot_t *entry =
          (const db_map_entry_slot_t *)vec_at_const(entries, i);
      uint64_t entry_hash;
      if (entry == NULL || entry->key_value == NULL || entry->value == NULL)
        return 0u;
      entry_hash = dbv_hash_mix(
          dbv_semantic_hash_depth(entry->key_value, depth + 1u),
          dbv_semantic_hash_depth(entry->value, depth + 1u));
      unordered ^= entry_hash;
    }
    return dbv_hash_mix(hash, unordered);
  }
  default:
    return 0u;
  }
}

static bool db_value_ref_key_equal(const void *left_, const void *right_) {
  const db_value_ref_key_t *left = (const db_value_ref_key_t *)left_;
  const db_value_ref_key_t *right = (const db_value_ref_key_t *)right_;
  return left != NULL && right != NULL &&
         dbv_semantic_equal_depth(left->value, right->value, 0u);
}

static uint64_t db_value_ref_key_hash(const void *value_) {
  const db_value_ref_key_t *value = (const db_value_ref_key_t *)value_;
  return value != NULL ? dbv_semantic_hash_depth(value->value, 0u) : 0u;
}

static bool db_value_ref_key_copy(void *destination_, const void *source_) {
  db_value_ref_key_t *destination = (db_value_ref_key_t *)destination_;
  const db_value_ref_key_t *source = (const db_value_ref_key_t *)source_;
  if (destination == NULL || source == NULL) return false;
  destination->value = source->value;
  return true;
}

static void db_value_ref_key_move(void *destination_, void *source_) {
  db_value_ref_key_t *destination = (db_value_ref_key_t *)destination_;
  db_value_ref_key_t *source = (db_value_ref_key_t *)source_;
  if (destination == NULL || source == NULL) return;
  destination->value = source->value;
  source->value = NULL;
}

static void db_value_ref_key_destroy(void *value_) {
  db_value_ref_key_t *value = (db_value_ref_key_t *)value_;
  if (value != NULL) value->value = NULL;
}

static const cmeta_type_traits DB_VALUE_REF_KEY_TRAITS = {
    CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY |
        CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY | CMETA_TRAIT_TRIVIAL_COPY |
        CMETA_TRAIT_TRIVIAL_DESTROY,
    db_value_ref_key_equal,
    db_value_ref_key_hash,
    NULL,
    db_value_ref_key_copy,
    db_value_ref_key_move,
    db_value_ref_key_destroy};

static const cmeta_type_desc DB_VALUE_REF_KEY_TYPE = {
    "salts-utils.databind.value-ref-key",
    sizeof(db_value_ref_key_t),
    _Alignof(db_value_ref_key_t),
    CMETA_T_OBJECT,
    NULL,
    &DB_VALUE_REF_KEY_TRAITS,
    NULL};

static bool db_map_entry_slot_copy(void *destination_, const void *source_) {
  db_map_entry_slot_t *destination = (db_map_entry_slot_t *)destination_;
  const db_map_entry_slot_t *source = (const db_map_entry_slot_t *)source_;
  if (destination == NULL || source == NULL || source->key_value == NULL ||
      source->public_key_text == NULL || source->value == NULL)
    return false;
  memset(destination, 0, sizeof(*destination));
  destination->public_key_text = dbv_strdup(source->public_key_text);
  if (destination->public_key_text == NULL) return false;
  destination->key_value = dbv_retain(source->key_value);
  if (destination->key_value == NULL) {
    free(destination->public_key_text);
    destination->public_key_text = NULL;
    return false;
  }
  destination->value = dbv_retain(source->value);
  if (destination->value == NULL) {
    dbv_release(destination->key_value);
    destination->key_value = NULL;
    free(destination->public_key_text);
    destination->public_key_text = NULL;
    return false;
  }
  return true;
}

static void db_map_entry_slot_move(void *destination_, void *source_) {
  db_map_entry_slot_t *destination = (db_map_entry_slot_t *)destination_;
  db_map_entry_slot_t *source = (db_map_entry_slot_t *)source_;
  if (destination == NULL || source == NULL) return;
  *destination = *source;
  memset(source, 0, sizeof(*source));
}

static void db_map_entry_slot_destroy(void *slot_) {
  db_map_entry_slot_t *slot = (db_map_entry_slot_t *)slot_;
  if (slot == NULL) return;
  dbv_release(slot->key_value);
  slot->key_value = NULL;
  free(slot->public_key_text);
  slot->public_key_text = NULL;
  dbv_release(slot->value);
  slot->value = NULL;
}

static const cmeta_type_traits DB_MAP_ENTRY_SLOT_TRAITS = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL,
    NULL,
    NULL,
    db_map_entry_slot_copy,
    db_map_entry_slot_move,
    db_map_entry_slot_destroy};

static const cmeta_type_desc DB_MAP_ENTRY_SLOT_TYPE = {
    "salts-utils.databind.map-entry-slot",
    sizeof(db_map_entry_slot_t),
    _Alignof(db_map_entry_slot_t),
    CMETA_T_OBJECT,
    NULL,
    &DB_MAP_ENTRY_SLOT_TRAITS,
    NULL};

static bool db_map_index_value_copy(void *destination_, const void *source_) {
  if (destination_ == NULL || source_ == NULL) return false;
  *(db_map_index_value_t *)destination_ = *(const db_map_index_value_t *)source_;
  return true;
}

static void db_map_index_value_move(void *destination_, void *source_) {
  if (destination_ == NULL || source_ == NULL) return;
  *(db_map_index_value_t *)destination_ = *(db_map_index_value_t *)source_;
}

static void db_map_index_value_destroy(void *value_) { (void)value_; }

static const cmeta_type_traits DB_MAP_INDEX_VALUE_TRAITS = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    NULL,
    NULL,
    NULL,
    db_map_index_value_copy,
    db_map_index_value_move,
    db_map_index_value_destroy};

static const cmeta_type_desc DB_MAP_INDEX_VALUE_TYPE = {
    "salts-utils.databind.map-index-value",
    sizeof(db_map_index_value_t),
    _Alignof(db_map_index_value_t),
    CMETA_T_OBJECT,
    NULL,
    &DB_MAP_INDEX_VALUE_TRAITS,
    NULL};

static stl_status db_vec_init(vec_t *vec, const cmeta_type_desc *slot_type,
                              size_t limit) {
  if (vec == NULL || slot_type == NULL) return STL_INVALID_ARGUMENT;
  memset(vec, 0, sizeof(*vec));
  vec->cmeta.descriptor = &stl_vec_container_desc;
  vec->element_type = slot_type;
  return vec_init(vec, limit);
}

static stl_status db_set_init(db_set_storage_t *set, size_t limit) {
  stl_status status;
  if (set == NULL) return STL_INVALID_ARGUMENT;
  memset(set, 0, sizeof(*set));
  status = db_vec_init(&set->ordered_values, &DB_OWNED_VALUE_SLOT_TYPE, limit);
  if (status != STL_OK) return status;
  set->membership.cmeta.descriptor = &stl_hash_set_container_desc;
  set->membership.element_type = &DB_VALUE_REF_KEY_TYPE;
  status = hash_set_init(&set->membership, limit);
  if (status != STL_OK) {
    vec_destroy(&set->ordered_values);
  } else {
    set->generation = vec_generation(&set->ordered_values);
  }
  return status;
}

static stl_status db_map_init(db_map_storage_t *map, size_t limit) {
  stl_status status;
  if (map == NULL) return STL_INVALID_ARGUMENT;
  memset(map, 0, sizeof(*map));
  status = db_vec_init(&map->ordered_entries, &DB_MAP_ENTRY_SLOT_TYPE, limit);
  if (status != STL_OK) return status;
  map->index.cmeta.descriptor = &stl_hash_map_container_desc;
  map->index.key_type = &DB_VALUE_REF_KEY_TYPE;
  map->index.value_type = &DB_MAP_INDEX_VALUE_TYPE;
  status = hash_map_init(&map->index, limit);
  if (status != STL_OK) {
    vec_destroy(&map->ordered_entries);
  } else {
    map->generation = vec_generation(&map->ordered_entries);
  }
  return status;
}

static DataBindStatus db_status_from_stl(stl_status status) {
  switch (status) {
  case STL_OK:
    return DATA_BIND_OK;
  case STL_OUT_OF_MEMORY:
    return DATA_BIND_ERR_OOM;
  case STL_CAPACITY_EXCEEDED:
    return DATA_BIND_ERR_LIMIT;
  case STL_INVALID_ARGUMENT:
  case STL_TYPE_MISMATCH:
  case STL_TRAIT_MISSING:
    return DATA_BIND_ERR_SCHEMA;
  case STL_EMPTY:
  case STL_NOT_FOUND:
  default:
    return DATA_BIND_ERR_RUNTIME;
  }
}

static DataBindValue *dbv_new(DataBindValueKind kind) {
  DataBindValue *value = NULL;
  int pool_enabled = value_pool_is_enabled();
  stl_status status = STL_OK;

  if (pool_enabled && atomic_load_explicit(&g_value_pool_ready_mask, memory_order_relaxed) != 0) {
    value = value_pool_take();
    if (value != NULL)
      atomic_fetch_add_explicit(&g_value_pool_reused_count, 1U, memory_order_relaxed);
  }

  if (value != NULL) {
    memset(value, 0, sizeof(*value));
  } else {
    value = (DataBindValue *)calloc(1, sizeof(*value));
    if (value != NULL) {
      atomic_fetch_add_explicit(&g_value_pool_allocated_count, 1U, memory_order_relaxed);
    }
  }

  if (value != NULL) {
    value->references = 1u;
    value->kind = kind;
    if (kind == DATA_BIND_VALUE_OBJECT)
      status = db_vec_init(&value->data.object.fields, &DB_FIELD_SLOT_TYPE, SIZE_MAX);
    else if (kind == DATA_BIND_VALUE_LIST)
      status = db_vec_init(&value->data.sequence.values, &DB_OWNED_VALUE_SLOT_TYPE, SIZE_MAX);
    else if (kind == DATA_BIND_VALUE_SET)
      status = db_set_init(&value->data.set, SIZE_MAX);
    else if (kind == DATA_BIND_VALUE_MAP)
      status = db_map_init(&value->data.map, SIZE_MAX);
    if (status != STL_OK) {
      value->references = 0u;
      if (!pool_enabled || !value_pool_put(value)) free(value);
      value = NULL;
    }
  }
  return value;
}

static DataBindValue *dbv_attach_canonical_identity(const char *type_name,
                                                    DataBindValue *value) {
  const cmeta_data_desc *canonical;
  if (value == NULL || type_name == NULL) return value;
  canonical = schema_cmeta_builtin_data(type_name);
  if (canonical == NULL) return value;
  if (!cmeta_data_desc_valid(canonical)) {
    data_bind_value_free(value);
    return NULL;
  }
  value->type_identity = canonical->storage_type->identity;
  return value;
}

static DataBindStatus dbv_collection_push(DataBindValue *sequence,
                                          DataBindValue *value) {
  db_owned_value_slot_t slot;
  stl_status status;
  vec_t *ordered_values;
  if (sequence == NULL ||
      (sequence->kind != DATA_BIND_VALUE_LIST &&
       sequence->kind != DATA_BIND_VALUE_SET) ||
      value == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (sequence->kind == DATA_BIND_VALUE_SET) {
    db_value_ref_key_t key = {value};
    db_value_ref_key_t stored_key;
    const db_owned_value_slot_t *stored_slot;
    stl_status rollback_status;
    if (hash_set_contains(&sequence->data.set.membership, &key)) {
      dbv_release(value);
      return DATA_BIND_OK;
    }
    ordered_values = &sequence->data.set.ordered_values;
    slot.value = value;
    status = vec_push(ordered_values, &slot);
    if (status != STL_OK) return db_status_from_stl(status);
    stored_slot = (const db_owned_value_slot_t *)vec_at_const(
        ordered_values, vec_size(ordered_values) - 1u);
    if (stored_slot == NULL || stored_slot->value == NULL) {
      rollback_status = vec_pop(ordered_values, NULL);
      return rollback_status == STL_OK ? DATA_BIND_ERR_RUNTIME
                                       : db_status_from_stl(rollback_status);
    }
    stored_key.value = stored_slot->value;
    status = hash_set_add(&sequence->data.set.membership, &stored_key);
    if (status != STL_OK) {
      rollback_status = vec_pop(ordered_values, NULL);
      return rollback_status == STL_OK ? db_status_from_stl(status)
                                       : db_status_from_stl(rollback_status);
    }
    dbv_release(value);
    ++sequence->data.set.generation;
    return DATA_BIND_OK;
  }

  slot.value = value;
  status = vec_push(&sequence->data.sequence.values, &slot);
  if (status == STL_OK) dbv_release(value);
  return db_status_from_stl(status);
}

static DataBindStatus dbv_collection_reserve(DataBindValue *collection,
                                              size_t min_capacity) {
  stl_status status;
  vec_t *ordered_values;
  if (collection == NULL ||
      (collection->kind != DATA_BIND_VALUE_LIST &&
       collection->kind != DATA_BIND_VALUE_SET))
    return DATA_BIND_ERR_INVALID_ARG;
  ordered_values = dbv_ordered_values(collection);
  if (collection->kind == DATA_BIND_VALUE_SET) {
    status = hash_set_reserve(&collection->data.set.membership, min_capacity);
    if (status != STL_OK) return db_status_from_stl(status);
  }
  return db_status_from_stl(vec_reserve(ordered_values, min_capacity));
}

static int dbv_sequence_set_limit(DataBindValue *sequence, size_t limit) {
  vec_t *values;
  if (sequence == NULL || sequence->kind != DATA_BIND_VALUE_LIST || limit == 0u)
    return 0;
  values = &sequence->data.sequence.values;
  if (!values->initialized || vec_size(values) > limit) return 0;
  values->element_limit = limit;
  return 1;
}

static DataBindStatus dbv_object_set(DataBindValue *object, const char *name,
                                     DataBindValue *value) {
  db_field_slot_t slot;
  stl_status status;
  if (object == NULL || object->kind != DATA_BIND_VALUE_OBJECT ||
      name == NULL || value == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  slot.name = (char *)name;
  slot.value = value;
  status = vec_push(&object->data.object.fields, &slot);
  if (status == STL_OK) dbv_release(value);
  return db_status_from_stl(status);
}

static int dbv_string_map_set(DataBindValue *map, const char *key,
                              DataBindValue *value) {
  db_map_storage_t *storage;
  db_map_entry_slot_t slot;
  DataBindValue *key_value;
  db_value_ref_key_t lookup_key;
  const db_map_index_value_t *existing;
  stl_status status;
  if (map == NULL || map->kind != DATA_BIND_VALUE_MAP || key == NULL ||
      value == NULL)
    return 0;
  storage = &map->data.map;
  key_value = dbv_string(key);
  if (key_value == NULL) return 0;
  lookup_key.value = key_value;
  existing =
      (const db_map_index_value_t *)hash_map_get_const(&storage->index,
                                                       &lookup_key);
  if (existing != NULL) {
    db_map_entry_slot_t *stored = (db_map_entry_slot_t *)vec_at(
        &storage->ordered_entries, existing->ordered_index);
    DataBindValue *replaced;
    if (stored == NULL || stored->key_value == NULL || stored->value == NULL) {
      dbv_release(key_value);
      return 0;
    }
    replaced = stored->value;
    stored->value = value;
    dbv_release(replaced);
    dbv_release(key_value);
    ++storage->generation;
    return 1;
  }

  slot.key_value = key_value;
  slot.public_key_text = (char *)key;
  slot.value = value;
  status = vec_push(&storage->ordered_entries, &slot);
  if (status == STL_OK) {
    const size_t ordered_index = vec_size(&storage->ordered_entries) - 1u;
    const db_map_entry_slot_t *stored = (const db_map_entry_slot_t *)vec_at_const(
        &storage->ordered_entries, ordered_index);
    db_value_ref_key_t stored_key;
    db_map_index_value_t index_value = {ordered_index};
    if (stored == NULL || stored->key_value == NULL) {
      (void)vec_pop(&storage->ordered_entries, NULL);
      status = STL_INVALID_ARGUMENT;
    } else {
      stored_key.value = stored->key_value;
      status = hash_map_put(&storage->index, &stored_key, &index_value);
      if (status != STL_OK) (void)vec_pop(&storage->ordered_entries, NULL);
    }
  }
  dbv_release(key_value);
  if (status != STL_OK) return 0;
  dbv_release(value);
  ++storage->generation;
  return 1;
}

DataBindStatus data_bind_internal_test_touch_generation(DataBindValue *value) {
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  switch (value->kind) {
  case DATA_BIND_VALUE_OBJECT:
    if (data_bind_internal_storage_kind(value) != DB_INTERNAL_STORAGE_VEC)
      return DATA_BIND_ERR_RUNTIME;
    ++value->data.object.fields.generation;
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_LIST:
    if (data_bind_internal_storage_kind(value) != DB_INTERNAL_STORAGE_VEC)
      return DATA_BIND_ERR_RUNTIME;
    ++value->data.sequence.values.generation;
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_SET:
    if (data_bind_internal_storage_kind(value) != DB_INTERNAL_STORAGE_ORDERED_SET)
      return DATA_BIND_ERR_RUNTIME;
    ++value->data.set.generation;
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_MAP:
    if (data_bind_internal_storage_kind(value) != DB_INTERNAL_STORAGE_ORDERED_MAP)
      return DATA_BIND_ERR_RUNTIME;
    ++value->data.map.generation;
    return DATA_BIND_OK;
  default:
    return DATA_BIND_ERR_INVALID_ARG;
  }
}

static int dbv_map_has_key(const DataBindValue *map, const char *key) {
  DataBindValue *key_value;
  db_value_ref_key_t lookup_key;
  int found;
  if (map == NULL || map->kind != DATA_BIND_VALUE_MAP || key == NULL) return 0;
  key_value = dbv_string(key);
  if (key_value == NULL) return 0;
  lookup_key.value = key_value;
  found = hash_map_contains(&map->data.map.index, &lookup_key) ? 1 : 0;
  dbv_release(key_value);
  return found;
}

static void dbv_release(DataBindValue *value) {
  db_dynamic_graph_t *owned_graph;
  if (value == NULL || value->references == 0u) return;
  value->references--;
  if (value->references != 0u) return;
  owned_graph = value->owned_graph;
  value->owned_graph = NULL;
  value->type_identity = NULL;
  switch (value->kind) {
  case DATA_BIND_VALUE_OBJECT:
    vec_destroy(&value->data.object.fields);
    break;
  case DATA_BIND_VALUE_LIST:
    vec_destroy(&value->data.sequence.values);
    break;
  case DATA_BIND_VALUE_SET:
    hash_set_destroy(&value->data.set.membership);
    vec_destroy(&value->data.set.ordered_values);
    break;
  case DATA_BIND_VALUE_MAP:
    hash_map_destroy(&value->data.map.index);
    vec_destroy(&value->data.map.ordered_entries);
    break;
  case DATA_BIND_VALUE_STRING:
    free(value->data.string_val.ptr);
    break;
  case DATA_BIND_VALUE_BIGINT:
    free(value->data.bigint_val.ptr);
    break;
  case DATA_BIND_VALUE_BYTES:
    free(value->data.bytes_val.ptr);
    break;
  default:
    break;
  }

  db_dynamic_graph_release(owned_graph);
  if (value_pool_is_enabled() && value_pool_put(value)) return;
  free(value);
}

void data_bind_value_free(DataBindValue *value) { dbv_release(value); }

static DataBindValue *dbv_int(int32_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_INT);
  if (v != NULL) v->data.int_val = value;
  return v;
}

static DataBindValue *dbv_int64(int64_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_INT64);
  if (v != NULL) v->data.int64_val = value;
  return v;
}

static DataBindValue *dbv_uint32_compat(uint32_t value) {
  return value <= INT32_MAX ? dbv_int((int32_t)value) : dbv_int64((int64_t)value);
}

static DataBindValue *dbv_uint64(uint64_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_UINT64);
  if (v != NULL) v->data.uint64_val = value;
  return v;
}

static DataBindValue *dbv_double(double value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_DOUBLE);
  if (v != NULL) v->data.double_val = value;
  return v;
}

static DataBindValue *dbv_bool(int value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_BOOL);
  if (v != NULL) v->data.bool_val = value != 0;
  return v;
}

static DataBindValue *dbv_string_n(const char *value, size_t len) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_STRING);
  if (v == NULL || (value == NULL && len != 0) || len == SIZE_MAX ||
      !vstr_utf8_valid(vstr_from_buf(value != NULL ? value : "", len))) {
    data_bind_value_free(v);
    return NULL;
  }
  v->data.string_val.ptr = (char *)malloc(len + 1u);
  if (v->data.string_val.ptr == NULL) {
    data_bind_value_free(v);
    return NULL;
  }
  if (len != 0) memcpy(v->data.string_val.ptr, value, len);
  v->data.string_val.ptr[len] = '\0';
  v->data.string_val.len = len;
  return v;
}

static DataBindValue *dbv_string(const char *value) {
  const char *text = value != NULL ? value : "";
  return dbv_string_n(text, strlen(text));
}

static DataBindValue *dbv_bytes(const uint8_t *data, size_t len) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_BYTES);
  if (v == NULL) return NULL;
  if (len > 0) {
    v->data.bytes_val.ptr = (uint8_t *)malloc(len);
    if (v->data.bytes_val.ptr == NULL) {
      data_bind_value_free(v);
      return NULL;
    }
    if (data != NULL) memcpy(v->data.bytes_val.ptr, data, len);
  }
  v->data.bytes_val.len = len;
  return v;
}

static DataBindValue *dbv_uuid_bytes(const uint8_t *data) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_UUID);
  if (v == NULL || data == NULL) {
    data_bind_value_free(v);
    return NULL;
  }
  memcpy(v->data.uuid_val.bytes, data, sizeof(v->data.uuid_val.bytes));
  return v;
}

static DataBindValue *dbv_uuid_text(const char *text) {
  salts_uuid_t uuid;
  if (text == NULL || salts_uuid_parse(text, &uuid) != SALTS_OK) return NULL;
  return dbv_uuid_bytes(uuid.bytes);
}

static DataBindValue *dbv_datetime(DataBindDateTime value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_DATETIME);
  if (v == NULL) return NULL;
  v->data.datetime_val = value;
  return v;
}

static DataBindValue *dbv_datetime_text(const char *text) {
  DataBindDateTime value;
  if (text == NULL ||
      data_bind_temporal_parse_datetime(text, strlen(text), &value) !=
          DATA_BIND_OK)
    return NULL;
  return dbv_datetime(value);
}

static int db_date_valid(int year, int month, int day) {
  static const int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days;
  if (year < 1 || month < 1 || month > 12 || day < 1) return 0;
  days = days_per_month[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days = 29;
  return day <= days;
}

static int db_parse_date_text(const char *text, DataBindDate *out) {
  int year = 0, month = 0, day = 0, consumed = 0;
  if (text == NULL || out == NULL) return 0;
  if (sscanf(text, "%d-%d-%d%n", &year, &month, &day, &consumed) == 3 ||
      sscanf(text, "%d/%d/%d%n", &year, &month, &day, &consumed) == 3) {
    if (text[consumed] != '\0' || !db_date_valid(year, month, day)) return 0;
    out->year = year;
    out->month = month;
    out->day = day;
    return 1;
  }
  return data_bind_temporal_parse_date(text, strlen(text), out) ==
         DATA_BIND_OK;
}

static int db_parse_time_text(const char *text, DataBindTime *out) {
  if (text == NULL || out == NULL || strchr(text, ':') == NULL) return 0;
  return data_bind_temporal_parse_time(text, strlen(text), out) ==
         DATA_BIND_OK;
}

static int db_parse_duration_text(const char *text, int64_t *out) {
  const char *p;
  int sign = 1;
  double total = 0.0;
  int saw_value = 0;
  long long hours = 0, minutes = 0, seconds = 0, millis = 0;
  int consumed = 0;
  int colon_sign;
  if (text == NULL || out == NULL) return 0;
  if (sscanf(text, "%lld:%lld:%lld.%lld%n", &hours, &minutes, &seconds, &millis, &consumed) == 4 &&
      text[consumed] == '\0' && minutes >= 0 && minutes <= 59 && seconds >= 0 && seconds <= 60 &&
      millis >= 0 && millis <= 999) {
    colon_sign = hours < 0 ? -1 : 1;
    if (hours < 0) hours = -hours;
    *out =
        (int64_t)(colon_sign * (hours * 3600000LL + minutes * 60000LL + seconds * 1000LL + millis));
    return 1;
  }
  if (sscanf(text, "%lld:%lld:%lld%n", &hours, &minutes, &seconds, &consumed) == 3 &&
      text[consumed] == '\0' && minutes >= 0 && minutes <= 59 && seconds >= 0 && seconds <= 60) {
    colon_sign = hours < 0 ? -1 : 1;
    if (hours < 0) hours = -hours;
    *out = (int64_t)(colon_sign * (hours * 3600000LL + minutes * 60000LL + seconds * 1000LL));
    return 1;
  }
  p = text;
  while (isspace((unsigned char)*p))
    p++;
  if (*p == '-') {
    sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  while (*p != '\0') {
    char *next = NULL;
    double n;
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '\0') break;
    errno = 0;
    n = strtod(p, &next);
    if (errno != 0 || next == p) return 0;
    p = next;
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '\0') {
      total += n;
      saw_value = 1;
      break;
    }
    if (p[0] == 'm' && p[1] == 's') {
      total += n;
      p += 2;
    } else if (*p == 's') {
      total += n * 1000.0;
      p++;
    } else if (*p == 'm') {
      total += n * 60000.0;
      p++;
    } else if (*p == 'h') {
      total += n * 3600000.0;
      p++;
    } else if (*p == 'd') {
      total += n * 86400000.0;
      p++;
    } else {
      return 0;
    }
    saw_value = 1;
  }
  if (!saw_value || total > (double)INT64_MAX) return 0;
  *out = (int64_t)(sign * total);
  return 1;
}

static int db_date_to_text(DataBindDate date, char *out, size_t len) {
  return out != NULL && len > 0 && db_date_valid(date.year, date.month, date.day) &&
         snprintf(out, len, "%04d-%02d-%02d", date.year, date.month, date.day) > 0;
}

static int db_time_to_text(DataBindTime time, char *out, size_t len) {
  if (out == NULL || len == 0 || time.hour < 0 || time.hour > 23 || time.minute < 0 ||
      time.minute > 59 || time.second < 0 || time.second > 60 || time.millisecond < 0 ||
      time.millisecond > 999)
    return 0;
  if (time.millisecond > 0)
    return snprintf(out, len, "%02d:%02d:%02d.%03d", time.hour, time.minute, time.second,
                    time.millisecond) > 0;
  return snprintf(out, len, "%02d:%02d:%02d", time.hour, time.minute, time.second) > 0;
}

static int db_duration_to_text(int64_t ms, char *out, size_t len) {
  int64_t rem;
  int64_t hours;
  int64_t minutes;
  int64_t seconds;
  if (out == NULL || len == 0) return 0;
  rem = ms < 0 ? -ms : ms;
  hours = rem / 3600000;
  rem %= 3600000;
  minutes = rem / 60000;
  rem %= 60000;
  seconds = rem / 1000;
  rem %= 1000;
  return snprintf(out, len, "%s%lld:%02lld:%02lld.%03lld", ms < 0 ? "-" : "", (long long)hours,
                  (long long)minutes, (long long)seconds, (long long)rem) > 0;
}

static DataBindValue *dbv_date(DataBindDate value) {
  DataBindValue *v;
  if (!db_date_valid(value.year, value.month, value.day)) return NULL;
  v = dbv_new(DATA_BIND_VALUE_DATE);
  if (v != NULL) v->data.date_val = value;
  return v;
}

static DataBindValue *dbv_date_text(const char *text) {
  DataBindDate date;
  if (!db_parse_date_text(text, &date)) return NULL;
  return dbv_date(date);
}

static DataBindValue *dbv_time(DataBindTime value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_TIME);
  if (v != NULL) v->data.time_val = value;
  return v;
}

static DataBindValue *dbv_time_text(const char *text) {
  DataBindTime time;
  if (!db_parse_time_text(text, &time)) return NULL;
  return dbv_time(time);
}

static DataBindValue *dbv_duration(int64_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_DURATION);
  if (v != NULL) v->data.duration_ms = value;
  return v;
}

static DataBindValue *dbv_duration_text(const char *text) {
  int64_t ms = 0;
  if (!db_parse_duration_text(text, &ms)) return NULL;
  return dbv_duration(ms);
}

static int db_decimal_normalize(DataBindDecimal *value) {
  if (value == NULL) return 0;
  if (value->scale < 0) return 0;
  while (value->scale > 0 && value->mantissa % 10 == 0) {
    value->mantissa /= 10;
    value->scale--;
  }
  if (value->mantissa == 0) value->scale = 0;
  return 1;
}

static int db_parse_decimal_text(const char *text, DataBindDecimal *out) {
  const char *p;
  int sign = 1;
  int saw_digit = 0;
  int saw_dot = 0;
  int32_t scale = 0;
  uint64_t acc = 0;
  uint64_t limit;

  if (text == NULL || out == NULL) return 0;
  p = text;
  while (isspace((unsigned char)*p))
    p++;
  if (*p == '-') {
    sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  limit = sign < 0 ? (uint64_t)INT64_MAX + 1ULL : (uint64_t)INT64_MAX;
  while (*p != '\0') {
    if (isdigit((unsigned char)*p)) {
      unsigned digit = (unsigned)(*p - '0');
      if (acc > (limit - digit) / 10ULL) return 0;
      acc = acc * 10ULL + digit;
      saw_digit = 1;
      if (saw_dot) {
        if (scale == INT32_MAX) return 0;
        scale++;
      }
      p++;
      continue;
    }
    if (*p == '.') {
      if (saw_dot) return 0;
      saw_dot = 1;
      p++;
      continue;
    }
    if (isspace((unsigned char)*p)) {
      while (isspace((unsigned char)*p))
        p++;
      if (*p == '\0') break;
    }
    return 0;
  }
  if (!saw_digit) return 0;
  if (sign < 0) {
    out->mantissa = acc == (uint64_t)INT64_MAX + 1ULL ? INT64_MIN : -(int64_t)acc;
  } else {
    out->mantissa = (int64_t)acc;
  }
  out->scale = scale;
  return db_decimal_normalize(out);
}

static int db_decimal_to_text(DataBindDecimal value, char *out, size_t len) {
  char digits[32];
  char *p = digits + sizeof(digits);
  uint64_t mag;
  size_t digit_count;
  size_t pos = 0;
  int negative;

  if (out == NULL || len == 0 || value.scale < 0) return 0;
  db_decimal_normalize(&value);
  negative = value.mantissa < 0;
  mag = negative ? (uint64_t)(-(value.mantissa + 1)) + 1ULL : (uint64_t)value.mantissa;
  *--p = '\0';
  do {
    *--p = (char)('0' + (mag % 10ULL));
    mag /= 10ULL;
  } while (mag != 0);
  digit_count = strlen(p);

  if (negative) {
    if (pos + 1 >= len) return 0;
    out[pos++] = '-';
  }

  if (value.scale == 0) {
    if (pos + digit_count >= len) return 0;
    memcpy(out + pos, p, digit_count + 1);
    return 1;
  }

  if ((size_t)value.scale >= digit_count) {
    size_t zeros = (size_t)value.scale - digit_count;
    if (pos + 2 + zeros + digit_count >= len) return 0;
    out[pos++] = '0';
    out[pos++] = '.';
    while (zeros-- > 0)
      out[pos++] = '0';
    memcpy(out + pos, p, digit_count);
    pos += digit_count;
    out[pos] = '\0';
    return 1;
  }

  {
    size_t whole = digit_count - (size_t)value.scale;
    if (pos + digit_count + 1 >= len) return 0;
    memcpy(out + pos, p, whole);
    pos += whole;
    out[pos++] = '.';
    memcpy(out + pos, p + whole, (size_t)value.scale);
    pos += (size_t)value.scale;
    out[pos] = '\0';
    return 1;
  }
}

static DataBindValue *dbv_decimal(DataBindDecimal value) {
  DataBindValue *v;
  if (!db_decimal_normalize(&value)) return NULL;
  v = dbv_new(DATA_BIND_VALUE_DECIMAL);
  if (v != NULL) v->data.decimal_val = value;
  return v;
}

static DataBindValue *dbv_decimal_text(const char *text) {
  DataBindDecimal value;
  if (!db_parse_decimal_text(text, &value)) return NULL;
  return dbv_decimal(value);
}

static int db_validate_currency(const char *text);

static int db_bigint_canonical_text(const char *text, char *out, size_t len) {
  const char *p;
  const char *digits;
  size_t digits_len;
  int negative = 0;
  if (text == NULL || out == NULL || len == 0) return 0;
  p = text;
  while (isspace((unsigned char)*p))
    p++;
  if (*p == '-') {
    negative = 1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  digits = p;
  while (*p == '0')
    p++;
  if (!isdigit((unsigned char)*p)) {
    const char *q = digits;
    int saw_zero = 0;
    while (*q == '0') {
      saw_zero = 1;
      q++;
    }
    while (isspace((unsigned char)*q))
      q++;
    if (!saw_zero || *q != '\0' || len < 2) return 0;
    memcpy(out, "0", 2);
    return 1;
  }
  digits = p;
  while (isdigit((unsigned char)*p))
    p++;
  digits_len = (size_t)(p - digits);
  while (isspace((unsigned char)*p))
    p++;
  if (*p != '\0' || digits_len == 0) return 0;
  if ((negative ? 1 : 0) + digits_len + 1 > len) return 0;
  if (negative) {
    out[0] = '-';
    memcpy(out + 1, digits, digits_len);
    out[digits_len + 1] = '\0';
  } else {
    memcpy(out, digits, digits_len);
    out[digits_len] = '\0';
  }
  return 1;
}

static DataBindValue *dbv_bigint_text(const char *text) {
  char stack_buf[256];
  char *canonical = stack_buf;
  size_t need;
  DataBindValue *v;
  if (text == NULL) return NULL;
  need = strlen(text) + 2;
  if (need > sizeof(stack_buf)) {
    canonical = (char *)malloc(need);
    if (canonical == NULL) return NULL;
  }
  if (!db_bigint_canonical_text(text, canonical, need)) {
    if (canonical != stack_buf) free(canonical);
    return NULL;
  }
  v = dbv_new(DATA_BIND_VALUE_BIGINT);
  if (v == NULL) {
    if (canonical != stack_buf) free(canonical);
    return NULL;
  }
  v->data.bigint_val.ptr = dbv_strdup(canonical);
  if (canonical != stack_buf) free(canonical);
  if (v->data.bigint_val.ptr == NULL) {
    data_bind_value_free(v);
    return NULL;
  }
  return v;
}

static int db_parse_money_text(const char *text, DataBindMoney *out) {
  char token_a[128];
  char token_b[128];
  char extra[2];
  DataBindDecimal amount;
  if (text == NULL || out == NULL) return 0;
  token_a[0] = token_b[0] = extra[0] = '\0';
  if (sscanf(text, " %127s %127s %1s", token_a, token_b, extra) != 2) return 0;
  if (db_validate_currency(token_a) && db_parse_decimal_text(token_b, &amount)) {
    out->amount = amount;
    memcpy(out->currency, token_a, 4);
    return 1;
  }
  if (db_parse_decimal_text(token_a, &amount) && db_validate_currency(token_b)) {
    out->amount = amount;
    memcpy(out->currency, token_b, 4);
    return 1;
  }
  return 0;
}

static int db_money_to_text(DataBindMoney value, char *out, size_t len) {
  char amount[64];
  if (out == NULL || len == 0 || !db_validate_currency(value.currency)) return 0;
  if (!db_decimal_to_text(value.amount, amount, sizeof(amount))) return 0;
  return snprintf(out, len, "%s %s", value.currency, amount) > 0;
}

static DataBindValue *dbv_money(DataBindMoney value) {
  DataBindValue *v;
  if (!db_validate_currency(value.currency) || !db_decimal_normalize(&value.amount)) return NULL;
  v = dbv_new(DATA_BIND_VALUE_MONEY);
  if (v != NULL) v->data.money_val = value;
  return v;
}

static DataBindValue *dbv_money_text(const char *text) {
  DataBindMoney value;
  if (!db_parse_money_text(text, &value)) return NULL;
  return dbv_money(value);
}

#define DATA_BIND_VALUE_CLONE_MAX_DEPTH 32u

static DataBindStatus dbv_clone_tree(const DataBindValue *source, size_t depth,
                                     DataBindValue **out_value) {
  DataBindValue *copy = NULL;
  DataBindValue *child = NULL;
  size_t i;
  DataBindStatus status;

  if (out_value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out_value = NULL;
  if (source == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (depth > DATA_BIND_VALUE_CLONE_MAX_DEPTH) return DATA_BIND_ERR_RUNTIME;

  switch (source->kind) {
  case DATA_BIND_VALUE_NULL:
    copy = dbv_new(DATA_BIND_VALUE_NULL);
    break;
  case DATA_BIND_VALUE_OBJECT:
    copy = dbv_new(DATA_BIND_VALUE_OBJECT);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    for (i = 0; i < vec_size(&source->data.object.fields); ++i) {
      const db_field_slot_t *field =
          (const db_field_slot_t *)vec_at_const(&source->data.object.fields, i);
      if (field->name == NULL || field->value == NULL) {
        status = DATA_BIND_ERR_RUNTIME;
        goto fail;
      }
      status = dbv_clone_tree(field->value, depth + 1u, &child);
      if (status != DATA_BIND_OK) goto fail;
      status = dbv_object_set(copy, field->name, child);
      if (status != DATA_BIND_OK) goto fail;
      child = NULL;
    }
    break;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET: {
    const vec_t *source_values = dbv_ordered_values_const(source);
    copy = dbv_new(source->kind);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    for (i = 0; i < vec_size(source_values); ++i) {
      const db_owned_value_slot_t *slot = (const db_owned_value_slot_t *)vec_at_const(
          source_values, i);
      if (slot == NULL || slot->value == NULL) {
        status = DATA_BIND_ERR_RUNTIME;
        goto fail;
      }
      status = dbv_clone_tree(slot->value, depth + 1u, &child);
      if (status != DATA_BIND_OK) goto fail;
      status = dbv_collection_push(copy, child);
      if (status != DATA_BIND_OK) goto fail;
      child = NULL;
    }
    break;
  }
  case DATA_BIND_VALUE_MAP:
    copy = dbv_new(DATA_BIND_VALUE_MAP);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    for (i = 0; i < vec_size(&source->data.map.ordered_entries); ++i) {
      const db_map_entry_slot_t *entry =
          (const db_map_entry_slot_t *)vec_at_const(
              &source->data.map.ordered_entries, i);
      if (entry == NULL || entry->public_key_text == NULL ||
          entry->key_value == NULL || entry->value == NULL) {
        status = DATA_BIND_ERR_RUNTIME;
        goto fail;
      }
      status = dbv_clone_tree(entry->value, depth + 1u, &child);
      if (status != DATA_BIND_OK) goto fail;
      if (!dbv_string_map_set(copy, entry->public_key_text, child)) {
        status = DATA_BIND_ERR_OOM;
        goto fail;
      }
      {
        db_map_entry_slot_t *new_entry = (db_map_entry_slot_t *)vec_at(
            &copy->data.map.ordered_entries,
            vec_size(&copy->data.map.ordered_entries) - 1u);
        if (new_entry == NULL || new_entry->key_value == NULL) {
          status = DATA_BIND_ERR_RUNTIME;
          goto fail;
        }
        new_entry->key_value->type_identity = entry->key_value->type_identity;
      }
      child = NULL;
    }
    break;
  case DATA_BIND_VALUE_INT:
    copy = dbv_int(source->data.int_val);
    break;
  case DATA_BIND_VALUE_INT64:
    copy = dbv_int64(source->data.int64_val);
    break;
  case DATA_BIND_VALUE_UINT64:
    copy = dbv_uint64(source->data.uint64_val);
    break;
  case DATA_BIND_VALUE_DOUBLE:
    copy = dbv_double(source->data.double_val);
    break;
  case DATA_BIND_VALUE_BOOL:
    copy = dbv_bool(source->data.bool_val);
    break;
  case DATA_BIND_VALUE_STRING:
    if (source->data.string_val.ptr == NULL) return DATA_BIND_ERR_RUNTIME;
    copy = dbv_string_n(source->data.string_val.ptr, source->data.string_val.len);
    break;
  case DATA_BIND_VALUE_BYTES:
    if (source->data.bytes_val.len > 0u && source->data.bytes_val.ptr == NULL) {
      return DATA_BIND_ERR_RUNTIME;
    }
    copy = dbv_bytes(source->data.bytes_val.ptr, source->data.bytes_val.len);
    break;
  case DATA_BIND_VALUE_UUID:
    copy = dbv_uuid_bytes(source->data.uuid_val.bytes);
    break;
  case DATA_BIND_VALUE_DATETIME:
    copy = dbv_datetime(source->data.datetime_val);
    break;
  case DATA_BIND_VALUE_DATE:
    copy = dbv_date(source->data.date_val);
    break;
  case DATA_BIND_VALUE_TIME:
    copy = dbv_time(source->data.time_val);
    break;
  case DATA_BIND_VALUE_DURATION:
    copy = dbv_duration(source->data.duration_ms);
    break;
  case DATA_BIND_VALUE_DECIMAL:
    copy = dbv_decimal(source->data.decimal_val);
    break;
  case DATA_BIND_VALUE_BIGINT:
    if (source->data.bigint_val.ptr == NULL) return DATA_BIND_ERR_RUNTIME;
    copy = dbv_bigint_text(source->data.bigint_val.ptr);
    break;
  case DATA_BIND_VALUE_MONEY:
    copy = dbv_money(source->data.money_val);
    break;
  default:
    return DATA_BIND_ERR_RUNTIME;
  }

  if (copy == NULL) return DATA_BIND_ERR_OOM;
  copy->type_identity = source->type_identity;
  *out_value = copy;
  return DATA_BIND_OK;

fail:
  data_bind_value_free(child);
  data_bind_value_free(copy);
  return status;
}

DataBindStatus data_bind_value_clone(const DataBindValue *value, DataBindValue **out_value) {
  DataBindStatus status = dbv_clone_tree(value, 0u, out_value);
  db_dynamic_graph_t *graph;
  if (status != DATA_BIND_OK || value == NULL || out_value == NULL || *out_value == NULL ||
      value->owned_graph == NULL)
    return status;
  graph = db_dynamic_graph_retain(value->owned_graph);
  if (graph == NULL) {
    data_bind_value_free(*out_value);
    *out_value = NULL;
    return DATA_BIND_ERR_LIMIT;
  }
  (*out_value)->owned_graph = graph;
  return DATA_BIND_OK;
}

typedef enum data_bind_text_kind {
  DB_TEXT_NUMBER,
  DB_TEXT_INTEGER,
  DB_TEXT_STRING,
  DB_TEXT_BYTES,
  DB_TEXT_BOOL,
  DB_TEXT_UUID,
  DB_TEXT_DATETIME,
  DB_TEXT_DATE,
  DB_TEXT_TIME,
  DB_TEXT_DURATION,
  DB_TEXT_DECIMAL,
  DB_TEXT_BIGINT,
  DB_TEXT_MONEY,
  DB_TEXT_UNSUPPORTED
} data_bind_text_kind_t;

typedef struct data_bind_csv_headers {
  char **names;
  size_t count;
  size_t capacity;
} data_bind_csv_headers_t;

typedef struct data_bind_index_list {
  size_t *values;
  size_t count;
  size_t capacity;
} data_bind_index_list_t;

static void db_error_clear(DataBindError *error) {
  if (error == NULL || error->size < sizeof(error->size)) return;
  if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  if (error->size >= offsetof(DataBindError, line) + sizeof(error->line)) error->line = -1;
  if (error->size >= offsetof(DataBindError, column) + sizeof(error->column)) error->column = -1;
  if (error->size >= offsetof(DataBindError, path) + sizeof(error->path)) error->path[0] = '\0';
  if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
    error->message[0] = '\0';
}

/**
 * @brief Format error path for consistent error reporting across input formats.
 * @param out Output buffer for formatted path
 * @param out_size Size of output buffer
 * @param format Format identifier: "binary", "json", "csv", "xml"
 * @param location Format-specific location (e.g., "offset 123", "$.path", "row 5 col 3")
 */
static void db_error_format_path(char *out, size_t out_size, const char *format,
                                 const char *location) {
  if (out == NULL || out_size == 0) return;
  if (format == NULL || format[0] == '\0') {
    snprintf(out, out_size, "%s", location != NULL ? location : "");
  } else if (location != NULL && location[0] != '\0') {
    snprintf(out, out_size, "%s: %s", format, location);
  } else {
    snprintf(out, out_size, "%s", format);
  }
}

static DataBindStatus db_error_set(DataBindError *error, DataBindStatus code, const char *path,
                                   int line, int column, const char *fmt, ...) {
  va_list ap;
  if (error != NULL && error->size >= sizeof(error->size)) {
    if (error->size >= offsetof(DataBindError, code) + sizeof(error->code)) error->code = code;
    if (error->size >= offsetof(DataBindError, line) + sizeof(error->line)) error->line = line;
    if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
      error->column = column;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path)) {
      snprintf(error->path, sizeof(error->path), "%s", path != NULL ? path : "");
    }
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message)) {
      va_start(ap, fmt);
      vsnprintf(error->message, sizeof(error->message), fmt, ap);
      va_end(ap);
    }
  }
  return code;
}

static DataBindStatus db_error_code_or(const DataBindError *error, DataBindStatus fallback) {
  if (error == NULL || error->size < offsetof(DataBindError, code) + sizeof(error->code) ||
      error->code == DATA_BIND_OK)
    return fallback;
  return error->code;
}

static DataBindStatus db_codec_error(DataBind *codec, DataBindError *error, DataBindStatus code,
                                     const char *fmt, ...) {
  char msg[512];
  va_list ap;
  (void)codec;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  return db_error_set(error, code, NULL, -1, -1, "%s", msg);
}

static Node *find_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP) return NULL;
  for (i = 0; i < parent->data.map.count; i++)
    if (strcmp(parent->data.map.items[i]->name, name) == 0) return parent->data.map.items[i];
  return NULL;
}

static const char *get_string_val(Node *node) {
  return (node != NULL && node->type == NODE_STRING) ? node->data.string_val : NULL;
}
static int field_flag(Node *field_node, const char *name) {
  Node *n = find_child(field_node, name);
  return n != NULL && n->type == NODE_STRING && strcmp(n->data.string_val, "1") == 0;
}

static int record_flag(Node *node, const char *name) { return field_flag(node, name); }

static const char *node_attribute_value_at(Node *node, const char *name, size_t index) {
  Node *attrs;
  size_t i, match = 0;
  if (node == NULL || name == NULL) return NULL;
  attrs = find_child(node, "attributes");
  if (attrs == NULL || attrs->type != NODE_LIST) return NULL;
  for (i = 0; i < attrs->data.list.count; i++) {
    Node *attr = attrs->data.list.items[i];
    const char *attr_name = get_string_val(find_child(attr, "name"));
    if (attr_name != NULL && strcmp(attr_name, name) == 0) {
      if (match == index) return get_string_val(find_child(attr, "value"));
      ++match;
    }
  }
  return NULL;
}

static size_t node_attribute_count(Node *node, const char *name) {
  size_t count = 0;
  while (node_attribute_value_at(node, name, count) != NULL) ++count;
  return count;
}

static const char *node_attribute_value(Node *node, const char *name) {
  return node_attribute_value_at(node, name, 0);
}

static const char *field_format(Node *field) { return node_attribute_value(field, "format"); }

static const char *field_binding_name(Node *field) {
  const char *mapped = node_attribute_value(field, "name");
  const char *canonical = get_string_val(find_child(field, "name"));
  return mapped != NULL && mapped[0] != '\0' ? mapped : canonical;
}

static size_t field_input_name_count(Node *field) {
  return node_attribute_count(field, "alias") + 2u;
}

static const char *field_input_name_at(Node *field, size_t index) {
  size_t aliases = node_attribute_count(field, "alias");
  if (index == 0) return field_binding_name(field);
  if (index <= aliases) return node_attribute_value_at(field, "alias", index - 1u);
  if (index == aliases + 1u) return get_string_val(find_child(field, "name"));
  return NULL;
}

static int field_input_name_is_duplicate(Node *field, size_t index, const char *candidate) {
  size_t i;
  if (candidate == NULL) return 1;
  for (i = 0; i < index; ++i) {
    const char *previous = field_input_name_at(field, i);
    if (previous != NULL && strcmp(previous, candidate) == 0) return 1;
  }
  return 0;
}

static int field_accepts_name(Node *field, const char *candidate) {
  size_t i;
  if (candidate == NULL) return 0;
  for (i = 0; i < field_input_name_count(field); ++i) {
    const char *accepted = field_input_name_at(field, i);
    if (accepted != NULL && strcmp(accepted, candidate) == 0) return 1;
  }
  return 0;
}

static json_value_t *json_field_value(Node *field, const json_value_t *object) {
  size_t i;
  if (object == NULL) return NULL;
  for (i = 0; i < field_input_name_count(field); ++i) {
    const char *candidate = field_input_name_at(field, i);
    json_value_t *value;
    if (field_input_name_is_duplicate(field, i, candidate)) continue;
    value = json_object_get(object, candidate);
    if (value != NULL) return value;
  }
  return NULL;
}

static int db_text_is_empty(const char *text) { return text == NULL || text[0] == '\0'; }

static int db_parse_double_text(const char *text, double *out) {
  char *end = NULL;
  double value;
  if (text == NULL || out == NULL) return 0;
  errno = 0;
  value = strtod(text, &end);
  if (errno != 0 || end == text || end == NULL || *end != '\0') return 0;
  *out = value;
  return 1;
}

static int db_parse_bool_text(const char *text, int *out) {
  if (text == NULL || out == NULL) return 0;
  if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0 || strcmp(text, "yes") == 0) {
    *out = 1;
    return 1;
  }
  if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0 || strcmp(text, "no") == 0) {
    *out = 0;
    return 1;
  }
  return 0;
}

static int db_is_alpha_num(char c) { return isalnum((unsigned char)c) != 0; }

static int db_is_hex_char(char c) { return isxdigit((unsigned char)c) != 0; }

static int db_validate_ipv4_n(const char *text, size_t len) {
  size_t pos = 0;
  int part;
  if (text == NULL || len == 0) return 0;
  for (part = 0; part < 4; part++) {
    int value = 0;
    int digits = 0;
    if (pos >= len || !isdigit((unsigned char)text[pos])) return 0;
    while (pos < len && isdigit((unsigned char)text[pos])) {
      value = value * 10 + (text[pos] - '0');
      digits++;
      if (digits > 3 || value > 255) return 0;
      pos++;
    }
    if (part < 3) {
      if (pos >= len || text[pos] != '.') return 0;
      pos++;
    }
  }
  return pos == len;
}

static int db_validate_ipv6_n(const char *text, size_t len) {
  size_t pos = 0;
  int groups = 0;
  int compressed = 0;
  if (text == NULL || len < 2) return 0;
  while (pos < len) {
    size_t start;
    int digits = 0;
    if (text[pos] == ':') {
      if (pos + 1 >= len || text[pos + 1] != ':' || compressed) return 0;
      compressed = 1;
      pos += 2;
      if (pos == len) break;
      continue;
    }
    start = pos;
    while (pos < len && db_is_hex_char(text[pos]) && digits < 4) {
      digits++;
      pos++;
    }
    if (pos < len && text[pos] == '.') {
      size_t ipv4_start = start;
      while (ipv4_start > 0 && text[ipv4_start - 1] != ':')
        ipv4_start--;
      if (!db_validate_ipv4_n(text + ipv4_start, len - ipv4_start)) return 0;
      groups += 2;
      pos = len;
      break;
    }
    if (digits == 0 || (pos < len && db_is_hex_char(text[pos]))) return 0;
    groups++;
    if (pos == len) break;
    if (text[pos] != ':') return 0;
    pos++;
    if (pos < len && text[pos] == ':') {
      if (compressed) return 0;
      compressed = 1;
      pos++;
      if (pos == len) break;
    }
    if (pos == len) return 0;
  }
  return compressed ? groups < 8 : groups == 8;
}

static int db_validate_ipaddr(const char *text) {
  size_t len;
  if (text == NULL) return 0;
  len = strlen(text);
  return db_validate_ipv4_n(text, len) || db_validate_ipv6_n(text, len);
}

static int db_parse_uint_n(const char *text, size_t len, int *out) {
  size_t i;
  int value = 0;
  if (text == NULL || len == 0 || out == NULL) return 0;
  for (i = 0; i < len; i++) {
    if (!isdigit((unsigned char)text[i])) return 0;
    value = value * 10 + (text[i] - '0');
    if (value > 1000) return 0;
  }
  *out = value;
  return 1;
}

static int db_validate_cidr(const char *text) {
  const char *slash;
  size_t addr_len;
  int prefix = 0;
  int is_v4;
  if (text == NULL) return 0;
  slash = strchr(text, '/');
  if (slash == NULL || slash == text || slash[1] == '\0') return 0;
  addr_len = (size_t)(slash - text);
  is_v4 = db_validate_ipv4_n(text, addr_len);
  if (!is_v4 && !db_validate_ipv6_n(text, addr_len)) return 0;
  if (!db_parse_uint_n(slash + 1, strlen(slash + 1), &prefix)) return 0;
  return prefix >= 0 && prefix <= (is_v4 ? 32 : 128);
}

static int db_validate_hostname_like(const char *text, int require_dot) {
  size_t len;
  size_t label_len = 0;
  int saw_dot = 0;
  char prev = '\0';
  size_t i;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 0 || len > 253) return 0;
  for (i = 0; i < len; i++) {
    char c = text[i];
    if (c == '.') {
      if (label_len == 0 || prev == '-') return 0;
      saw_dot = 1;
      label_len = 0;
    } else if (db_is_alpha_num(c) || c == '-') {
      if (label_len == 0 && c == '-') return 0;
      label_len++;
      if (label_len > 63) return 0;
    } else {
      return 0;
    }
    prev = c;
  }
  if (label_len == 0 || prev == '-') return 0;
  return !require_dot || saw_dot;
}

static int db_validate_email(const char *text) {
  const char *at;
  size_t local_len;
  size_t i;
  if (text == NULL) return 0;
  at = strchr(text, '@');
  if (at == NULL || strchr(at + 1, '@') != NULL) return 0;
  local_len = (size_t)(at - text);
  if (local_len == 0 || local_len > 64 || at[1] == '\0') return 0;
  if (text[0] == '.' || text[local_len - 1] == '.') return 0;
  for (i = 0; i < local_len; i++) {
    char c = text[i];
    if (c == '.' && i > 0 && text[i - 1] == '.') return 0;
    if (!(db_is_alpha_num(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-')) return 0;
  }
  return db_validate_hostname_like(at + 1, 1);
}

static int db_validate_scheme(const char *text, const char **after_colon) {
  const char *p;
  if (text == NULL || !isalpha((unsigned char)text[0])) return 0;
  p = text + 1;
  while (*p != '\0' && *p != ':') {
    if (!(db_is_alpha_num(*p) || *p == '+' || *p == '-' || *p == '.')) return 0;
    p++;
  }
  if (*p != ':') return 0;
  if (after_colon != NULL) *after_colon = p + 1;
  return 1;
}

static int db_validate_uri_text(const char *text, int require_authority) {
  const char *rest;
  const char *host_start;
  const char *host_end;
  const char *p;
  if (!db_validate_scheme(text, &rest) || rest[0] == '\0') return 0;
  for (p = rest; *p != '\0'; p++) {
    if ((unsigned char)*p <= 0x20 || (unsigned char)*p == 0x7f) return 0;
  }
  if (!require_authority) return 1;
  if (rest[0] != '/' || rest[1] != '/') return 0;
  host_start = rest + 2;
  if (*host_start == '\0') return 0;
  if (*host_start == '[') {
    host_end = strchr(host_start, ']');
    if (host_end == NULL ||
        !db_validate_ipv6_n(host_start + 1, (size_t)(host_end - host_start - 1)))
      return 0;
    return host_end[1] == '\0' || host_end[1] == ':' || host_end[1] == '/' || host_end[1] == '?' ||
           host_end[1] == '#';
  }
  host_end = host_start;
  while (*host_end != '\0' && *host_end != ':' && *host_end != '/' && *host_end != '?' &&
         *host_end != '#')
    host_end++;
  if (host_end == host_start) return 0;
  if (db_validate_ipv4_n(host_start, (size_t)(host_end - host_start))) return 1;
  {
    char host[256];
    size_t host_len = (size_t)(host_end - host_start);
    if (host_len >= sizeof(host)) return 0;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    return db_validate_hostname_like(host, 0);
  }
}

static int db_validate_macaddr(const char *text) {
  char sep;
  int i;
  if (text == NULL || strlen(text) != 17) return 0;
  sep = text[2];
  if (sep != ':' && sep != '-') return 0;
  for (i = 0; i < 17; i++) {
    if ((i + 1) % 3 == 0) {
      if (text[i] != sep) return 0;
    } else if (!db_is_hex_char(text[i])) {
      return 0;
    }
  }
  return 1;
}

static int db_validate_semver_ident(const char *text, size_t len, int numeric_core) {
  size_t i;
  if (len == 0) return 0;
  for (i = 0; i < len; i++) {
    if (numeric_core) {
      if (!isdigit((unsigned char)text[i])) return 0;
    } else if (!(db_is_alpha_num(text[i]) || text[i] == '-')) {
      return 0;
    }
  }
  if (numeric_core && len > 1 && text[0] == '0') return 0;
  return 1;
}

static int db_validate_semver_tail_n(const char *text, size_t len) {
  size_t part = 0;
  size_t i;
  if (text == NULL || len == 0) return 0;
  for (i = 0; i <= len; i++) {
    if (i == len || text[i] == '.') {
      if (!db_validate_semver_ident(text + part, i - part, 0)) return 0;
      part = i + 1;
    }
  }
  return 1;
}

static int db_validate_semver(const char *text) {
  const char *p = text;
  int part;
  if (text == NULL) return 0;
  for (part = 0; part < 3; part++) {
    const char *start = p;
    while (isdigit((unsigned char)*p))
      p++;
    if (!db_validate_semver_ident(start, (size_t)(p - start), 1)) return 0;
    if (part < 2) {
      if (*p != '.') return 0;
      p++;
    }
  }
  if (*p == '-') {
    const char *start = ++p;
    while (*p != '\0' && *p != '+')
      p++;
    if (!db_validate_semver_tail_n(start, (size_t)(p - start))) return 0;
  }
  if (*p == '+') {
    if (!db_validate_semver_tail_n(p + 1, strlen(p + 1))) return 0;
  } else if (*p != '\0') {
    return 0;
  }
  return 1;
}

static int db_validate_hex_text(const char *text) {
  size_t i, len;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 0) return 0;
  for (i = 0; i < len; i++)
    if (!db_is_hex_char(text[i])) return 0;
  return 1;
}

static int db_validate_base64_text(const char *text, int urlsafe) {
  size_t i, len;
  int padding = 0;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 0 || (!urlsafe && len % 4 != 0) || (urlsafe && len % 4 == 1)) return 0;
  for (i = 0; i < len; i++) {
    char c = text[i];
    int ok = db_is_alpha_num(c) || (!urlsafe && (c == '+' || c == '/')) ||
             (urlsafe && (c == '-' || c == '_'));
    if (c == '=') {
      padding++;
      if (padding > 2) return 0;
    } else {
      if (padding > 0 || !ok) return 0;
    }
  }
  return 1;
}

static int db_validate_currency(const char *text) {
  return text != NULL && strlen(text) == 3 && text[0] >= 'A' && text[0] <= 'Z' && text[1] >= 'A' &&
         text[1] <= 'Z' && text[2] >= 'A' && text[2] <= 'Z';
}

static int db_validate_json_pointer(const char *text) {
  const char *p;
  if (text == NULL) return 0;
  if (text[0] == '\0') return 1;
  if (text[0] != '/') return 0;
  for (p = text; *p != '\0'; p++) {
    if ((unsigned char)*p < 0x20) return 0;
    if (*p == '~' && p[1] != '0' && p[1] != '1') return 0;
  }
  return 1;
}

static int db_validate_balanced_expr(const char *text, int require_dollar) {
  int paren = 0, bracket = 0;
  char quote = '\0';
  const char *p;
  if (text == NULL || text[0] == '\0') return 0;
  if (require_dollar && text[0] != '$') return 0;
  for (p = text; *p != '\0'; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c == 0x7f) return 0;
    if (quote != '\0') {
      if (*p == '\\' && p[1] != '\0') {
        p++;
      } else if (*p == quote) {
        quote = '\0';
      }
      continue;
    }
    if (*p == '\'' || *p == '"') {
      quote = *p;
    } else if (*p == '(') {
      paren++;
    } else if (*p == ')') {
      if (paren == 0) return 0;
      paren--;
    } else if (*p == '[') {
      bracket++;
    } else if (*p == ']') {
      if (bracket == 0) return 0;
      bracket--;
    }
  }
  return quote == '\0' && paren == 0 && bracket == 0;
}

static int db_validate_cron_field(const char *text, size_t len) {
  size_t i;
  if (text == NULL || len == 0) return 0;
  for (i = 0; i < len; i++) {
    char c = text[i];
    if (!(db_is_alpha_num(c) || c == '*' || c == '/' || c == '?' || c == ',' || c == '-' ||
          c == '.' || c == '#' || c == 'L' || c == 'W'))
      return 0;
  }
  return 1;
}

static int db_validate_cron(const char *text) {
  const char *p;
  int fields = 0;
  if (text == NULL) return 0;
  p = text;
  while (*p != '\0') {
    const char *start;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '\0') break;
    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t')
      p++;
    if (!db_validate_cron_field(start, (size_t)(p - start))) return 0;
    fields++;
  }
  return fields == 5 || fields == 6 || fields == 7;
}

static int db_validate_color(const char *text) {
  size_t len;
  size_t i;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 4 || len == 7 || len == 9) {
    if (text[0] != '#') return 0;
    for (i = 1; i < len; i++)
      if (!db_is_hex_char(text[i])) return 0;
    return 1;
  }
  return 0;
}

static int db_is_mime_token_char(char c) {
  return db_is_alpha_num(c) || c == '!' || c == '#' || c == '$' || c == '&' || c == '^' ||
         c == '_' || c == '.' || c == '+' || c == '-';
}

static int db_validate_mime_token(const char *text, size_t len) {
  size_t i;
  if (text == NULL || len == 0) return 0;
  for (i = 0; i < len; i++)
    if (!db_is_mime_token_char(text[i])) return 0;
  return 1;
}

static int db_validate_mime(const char *text) {
  const char *slash;
  if (text == NULL) return 0;
  slash = strchr(text, '/');
  if (slash == NULL || slash == text || slash[1] == '\0' || strchr(slash + 1, '/') != NULL)
    return 0;
  return db_validate_mime_token(text, (size_t)(slash - text)) &&
         db_validate_mime_token(slash + 1, strlen(slash + 1));
}

static int db_validate_regex(const char *text) {
  return text != NULL && re_validate_n(text, strlen(text), NULL) == RE_STATUS_OK;
}

static int db_validate_string_format(const char *format, const char *text) {
  if (format == NULL || format[0] == '\0') return 1;
  if (strcmp(format, "ipaddr") == 0 || strcmp(format, "ip") == 0) return db_validate_ipaddr(text);
  if (strcmp(format, "cidr") == 0) return db_validate_cidr(text);
  if (strcmp(format, "hostname") == 0) return db_validate_hostname_like(text, 0);
  if (strcmp(format, "domain") == 0) return db_validate_hostname_like(text, 1);
  if (strcmp(format, "email") == 0) return db_validate_email(text);
  if (strcmp(format, "url") == 0) return db_validate_uri_text(text, 1);
  if (strcmp(format, "uri") == 0) return db_validate_uri_text(text, 0);
  if (strcmp(format, "macaddr") == 0 || strcmp(format, "mac") == 0)
    return db_validate_macaddr(text);
  if (strcmp(format, "semver") == 0) return db_validate_semver(text);
  if (strcmp(format, "hex") == 0) return db_validate_hex_text(text);
  if (strcmp(format, "base64") == 0) return db_validate_base64_text(text, 0);
  if (strcmp(format, "base64url") == 0) return db_validate_base64_text(text, 1);
  if (strcmp(format, "currency") == 0) return db_validate_currency(text);
  if (strcmp(format, "json_pointer") == 0 || strcmp(format, "json-pointer") == 0)
    return db_validate_json_pointer(text);
  if (strcmp(format, "jsonpath") == 0 || strcmp(format, "json_path") == 0)
    return db_validate_balanced_expr(text, 1);
  if (strcmp(format, "xpath") == 0) return db_validate_balanced_expr(text, 0);
  if (strcmp(format, "cron") == 0) return db_validate_cron(text);
  if (strcmp(format, "color") == 0) return db_validate_color(text);
  if (strcmp(format, "mime") == 0 || strcmp(format, "mime_type") == 0)
    return db_validate_mime(text);
  if (strcmp(format, "regex") == 0) return db_validate_regex(text);
  return 1;
}

static int db_value_matches_field_format(Node *field, const DataBindValue *value) {
  const char *format = field_format(field);
  if (format == NULL || format[0] == '\0') return 1;
  if (value == NULL || value->kind != DATA_BIND_VALUE_STRING) return 0;
  if (memchr(value->data.string_val.ptr, '\0', value->data.string_val.len) != NULL) return 0;
  return db_validate_string_format(format, value->data.string_val.ptr);
}

static int parse_positive_int(const char *text) {
  char *end = NULL;
  long value;
  if (text == NULL) return 0;
  value = strtol(text, &end, 10);
  if (end == text || value <= 0 || value > 0x7fffffffL) return 0;
  return (int)value;
}

static int parse_size_value(const char *text, size_t *out) {
  char *end = NULL;
  unsigned long long value;
  if (out != NULL) *out = 0;
  if (text == NULL || text[0] == '\0') return 0;
  value = strtoull(text, &end, 10);
  if (end == text || *end != '\0') return 0;
  if (out != NULL) *out = (size_t)value;
  return 1;
}

static Node *find_named_record(Node *schema_root, const char *list_name, const char *record_name) {
  Node *list = find_child(schema_root, list_name);
  size_t i;
  if (list == NULL || list->type != NODE_LIST || record_name == NULL) return NULL;
  for (i = 0; i < list->data.list.count; i++) {
    Node *record = list->data.list.items[i];
    const char *name = get_string_val(find_child(record, "name"));
    if (name != NULL && strcmp(name, record_name) == 0) return record;
  }
  return NULL;
}

static const type_meta_t *find_enum_meta(Node *schema_root, const char *enum_name) {
  Node *enum_node = find_named_record(schema_root, "enums", enum_name);
  const char *underlying =
      enum_node != NULL ? get_string_val(find_child(enum_node, "underlying_type")) : NULL;
  const type_meta_t *meta = find_type_meta(underlying != NULL ? underlying : "uint8");
  return meta != NULL ? meta : find_type_meta("uint8");
}

static const type_meta_t *find_scalar_meta(Node *schema_root, const char *type_name) {
  if (type_name == NULL) return NULL;
  if (find_named_record(schema_root, "enums", type_name) != NULL)
    return find_enum_meta(schema_root, type_name);
  return find_type_meta(type_name);
}

static Node *find_schema_record(Node *schema_root, const char *type_name) {
  Node *record;
  if (schema_root == NULL || type_name == NULL) return NULL;
  record = find_named_record(schema_root, "messages", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "composites", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "groups", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "unions", type_name);
  if (record != NULL) return record;
  return find_named_record(schema_root, "enums", type_name);
}

static Node *find_data_record(Node *schema_root, const char *type_name) {
  Node *record;
  if (schema_root == NULL || type_name == NULL) return NULL;
  record = find_named_record(schema_root, "messages", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "composites", type_name);
  if (record != NULL) return record;
  return find_named_record(schema_root, "groups", type_name);
}

static Node *find_union_record(Node *schema_root, const char *type_name) {
  return find_named_record(schema_root, "unions", type_name);
}

static Node *find_enum_record(Node *schema_root, const char *type_name) {
  return find_named_record(schema_root, "enums", type_name);
}

static int is_flags_type(Node *schema_root, const char *type_name) {
  Node *e = find_enum_record(schema_root, type_name);
  return e != NULL && record_flag(e, "is_flags");
}

static data_bind_text_kind_t bind_type_kind(Node *schema_root, const char *type) {
  const type_meta_t *meta;
  if (type == NULL) return DB_TEXT_UNSUPPORTED;
  if (strcmp(type, "uuid") == 0) return DB_TEXT_UUID;
  if (strcmp(type, "datetime") == 0) return DB_TEXT_DATETIME;
  if (strcmp(type, "date") == 0) return DB_TEXT_DATE;
  if (strcmp(type, "time") == 0) return DB_TEXT_TIME;
  if (strcmp(type, "duration") == 0) return DB_TEXT_DURATION;
  if (strcmp(type, "decimal") == 0) return DB_TEXT_DECIMAL;
  if (strcmp(type, "bigint") == 0) return DB_TEXT_BIGINT;
  if (strcmp(type, "money") == 0) return DB_TEXT_MONEY;
  if (strcmp(type, "bytes") == 0) return DB_TEXT_BYTES;
  if (strcmp(type, "string") == 0) return DB_TEXT_STRING;
  if (strcmp(type, "bool") == 0) return DB_TEXT_BOOL;
  meta = find_scalar_meta(schema_root, type);
  if (meta != NULL) return meta->is_float ? DB_TEXT_NUMBER : DB_TEXT_INTEGER;
  return DB_TEXT_UNSUPPORTED;
}

static data_bind_text_kind_t bind_field_kind(Node *schema_root, Node *field) {
  const char *type = get_string_val(find_child(field, "type"));
  if (type == NULL || field_flag(field, "is_collection") || field_flag(field, "is_composite_ref") ||
      field_flag(field, "is_group_field"))
    return DB_TEXT_UNSUPPORTED;
  return bind_type_kind(schema_root, type);
}

static int bind_type_supported(Node *schema_root, const char *type_name) {
  return find_data_record(schema_root, type_name) != NULL ||
         find_union_record(schema_root, type_name) != NULL ||
         bind_type_kind(schema_root, type_name) != DB_TEXT_UNSUPPORTED;
}

static int bind_field_missing_allowed(Node *field) {
  return field_flag(field, "is_optional") || field_flag(field, "has_default");
}

static Node *fields_node_for_record(Node *record);
static Node *items_node_for_enum(Node *record);

static const char *enum_item_value_n(Node *schema_root, const char *type_name,
                                     const char *item_name, size_t item_name_len) {
  Node *e = find_enum_record(schema_root, type_name);
  Node *items = items_node_for_enum(e);
  size_t i;
  if (items == NULL || item_name == NULL) return NULL;
  for (i = 0; i < items->data.list.count; i++) {
    Node *item = items->data.list.items[i];
    const char *name = get_string_val(find_child(item, "name"));
    if (name != NULL && strlen(name) == item_name_len &&
        memcmp(name, item_name, item_name_len) == 0)
      return get_string_val(find_child(item, "value"));
  }
  return NULL;
}

int data_bind_internal_parse_integer_magnitude(
    const char *text, size_t len, uint64_t max_value, int allow_negative,
    uint64_t *out, int *negative) {
  size_t pos = 0, exponent_pos = len, digits = 0, fraction_digits = 0, digit_index = 0;
  int seen_dot = 0, nonzero = 0, exponent_negative = 0;
  int64_t exponent = 0, effective_digits;
  uint64_t value = 0;
  if (text == NULL || out == NULL || negative == NULL || len == 0 || len > INT_MAX) return 0;
  *negative = 0;
  if (text[pos] == '-' || text[pos] == '+') {
    *negative = text[pos] == '-';
    if (*negative && !allow_negative) return 0;
    if (++pos == len) return 0;
  }
  for (; pos < len && text[pos] != 'e' && text[pos] != 'E'; ++pos) {
    if (text[pos] == '.') {
      if (seen_dot) return 0;
      seen_dot = 1;
      continue;
    }
    if (text[pos] < '0' || text[pos] > '9') return 0;
    nonzero |= text[pos] != '0';
    digits++;
    if (seen_dot) fraction_digits++;
  }
  if (digits == 0) return 0;
  if (pos < len) {
    int64_t cap = (int64_t)len + 64;
    exponent_pos = pos++;
    if (pos < len && (text[pos] == '-' || text[pos] == '+')) {
      exponent_negative = text[pos] == '-';
      pos++;
    }
    if (pos == len) return 0;
    for (; pos < len; ++pos) {
      int digit;
      if (text[pos] < '0' || text[pos] > '9') return 0;
      digit = text[pos] - '0';
      exponent = exponent > (cap - digit) / 10 ? cap : exponent * 10 + digit;
    }
    if (exponent_negative) exponent = -exponent;
  }
  if (!nonzero) {
    *out = 0;
    *negative = 0;
    return 1;
  }
  effective_digits = (int64_t)digits - (int64_t)fraction_digits + exponent;
  if (effective_digits <= 0) return 0;
  for (pos = (*negative || text[0] == '+') ? 1u : 0u; pos < exponent_pos; ++pos) {
    int digit;
    if (text[pos] == '.') continue;
    digit = text[pos] - '0';
    if ((int64_t)digit_index < effective_digits) {
      if (value > (max_value - (uint64_t)digit) / 10u) return 0;
      value = value * 10u + (uint64_t)digit;
    } else if (digit != 0) {
      return 0;
    }
    digit_index++;
  }
  while ((int64_t)digit_index < effective_digits) {
    if (value > max_value / 10u) return 0;
    value *= 10u;
    digit_index++;
  }
  *out = value;
  return 1;
}

static DataBindValue *dbv_scalar_integer_text(const type_meta_t *meta, const char *text,
                                              size_t len) {
  uint64_t max_value, magnitude;
  int negative;
  if (meta == NULL || meta->is_float || text == NULL) return NULL;
  switch (meta->wire_type) {
  case DB_WIRE_U8: max_value = UINT8_MAX; break;
  case DB_WIRE_U16: max_value = UINT16_MAX; break;
  case DB_WIRE_U32: max_value = UINT32_MAX; break;
  case DB_WIRE_U64: max_value = UINT64_MAX; break;
  case DB_WIRE_I8: max_value = (uint64_t)INT8_MAX + 1u; break;
  case DB_WIRE_I16: max_value = (uint64_t)INT16_MAX + 1u; break;
  case DB_WIRE_I32: max_value = (uint64_t)INT32_MAX + 1u; break;
  case DB_WIRE_I64: max_value = (uint64_t)INT64_MAX + 1u; break;
  default: return NULL;
  }
  if (!data_bind_internal_parse_integer_magnitude(
          text, len, max_value,
          meta->wire_type == DB_WIRE_I8 || meta->wire_type == DB_WIRE_I16 ||
              meta->wire_type == DB_WIRE_I32 || meta->wire_type == DB_WIRE_I64,
          &magnitude, &negative))
    return NULL;
  if (meta->wire_type == DB_WIRE_U64) return dbv_uint64(magnitude);
  if (negative) {
    int64_t signed_value = magnitude == (uint64_t)INT64_MAX + 1u
                               ? INT64_MIN
                               : -(int64_t)magnitude;
    if (meta->wire_type == DB_WIRE_I8 && signed_value < INT8_MIN) return NULL;
    if (meta->wire_type == DB_WIRE_I16 && signed_value < INT16_MIN) return NULL;
    if (meta->wire_type == DB_WIRE_I32 && signed_value < INT32_MIN) return NULL;
    return signed_value >= INT32_MIN ? dbv_int((int32_t)signed_value)
                                     : dbv_int64(signed_value);
  }
  if (meta->wire_type == DB_WIRE_I8 && magnitude > INT8_MAX) return NULL;
  if (meta->wire_type == DB_WIRE_I16 && magnitude > INT16_MAX) return NULL;
  if (meta->wire_type == DB_WIRE_I32 && magnitude > INT32_MAX) return NULL;
  if (meta->wire_type == DB_WIRE_I64 && magnitude > INT64_MAX) return NULL;
  return magnitude <= INT32_MAX ? dbv_int((int32_t)magnitude) : dbv_int64((int64_t)magnitude);
}

static int db_integer_unsigned_fits(const type_meta_t *meta, uint64_t value) {
  if (meta == NULL) return 0;
  switch (meta->wire_type) {
  case DB_WIRE_U8: return value <= UINT8_MAX;
  case DB_WIRE_I8: return value <= INT8_MAX;
  case DB_WIRE_U16: return value <= UINT16_MAX;
  case DB_WIRE_I16: return value <= INT16_MAX;
  case DB_WIRE_U32: return value <= UINT32_MAX;
  case DB_WIRE_I32: return value <= INT32_MAX;
  case DB_WIRE_U64: return 1;
  case DB_WIRE_I64: return value <= INT64_MAX;
  default: return 0;
  }
}

static DataBindValue *dbv_integer_from_unsigned(const type_meta_t *meta, uint64_t value) {
  if (!db_integer_unsigned_fits(meta, value)) return NULL;
  if (meta->wire_type == DB_WIRE_U64) return dbv_uint64(value);
  return value <= INT32_MAX ? dbv_int((int32_t)value) : dbv_int64((int64_t)value);
}

static DataBindValue *dbv_enum_item_text(Node *schema_root, const char *type_name,
                                         const type_meta_t *meta, const char *text, size_t len) {
  const char *value_text = enum_item_value_n(schema_root, type_name, text, len);
  if (value_text == NULL) return NULL;
  return dbv_scalar_integer_text(meta, value_text, strlen(value_text));
}

static DataBindValue *dbv_flags_text(Node *schema_root, const char *type_name,
                                     const type_meta_t *meta, const char *text, size_t len) {
  DataBindValue *direct = dbv_scalar_integer_text(meta, text, len);
  uint64_t acc = 0;
  size_t pos = 0;
  int any = 0;
  if (direct != NULL) return direct;
  direct = dbv_enum_item_text(schema_root, type_name, meta, text, len);
  if (direct != NULL) return direct;
  while (pos < len) {
    size_t start;
    DataBindValue *item;
    uint64_t bits;
    while (pos < len && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '|' ||
                         text[pos] == ',' || text[pos] == '+'))
      ++pos;
    if (pos == len) break;
    start = pos;
    while (pos < len && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '|' &&
           text[pos] != ',' && text[pos] != '+')
      ++pos;
    item = dbv_scalar_integer_text(meta, text + start, pos - start);
    if (item == NULL)
      item = dbv_enum_item_text(schema_root, type_name, meta, text + start, pos - start);
    if (item == NULL || data_bind_value_get_uint64(item, &bits) != DATA_BIND_OK) {
      data_bind_value_free(item);
      return NULL;
    }
    data_bind_value_free(item);
    acc |= bits;
    any = 1;
  }
  return any ? dbv_integer_from_unsigned(meta, acc) : NULL;
}

static DataBindValue *dbv_integer_text(Node *schema_root, const char *type_name, const char *text,
                                       size_t len) {
  const type_meta_t *meta;
  DataBindValue *value;
  if (text == NULL || type_name == NULL) return NULL;
  meta = find_scalar_meta(schema_root, type_name);
  if (meta == NULL || meta->is_float) return NULL;
  if (is_flags_type(schema_root, type_name))
    return dbv_flags_text(schema_root, type_name, meta, text, len);
  value = dbv_scalar_integer_text(meta, text, len);
  if (value != NULL || find_enum_record(schema_root, type_name) == NULL) return value;
  return dbv_enum_item_text(schema_root, type_name, meta, text, len);
}

static DataBindValue *bind_text_scalar_without_identity(
    Node *schema_root, const char *type_name, data_bind_text_kind_t kind,
    const char *text) {
  double dbl = 0.0;
  int b = 0;
  if (kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (kind == DB_TEXT_INTEGER)
    return dbv_integer_text(schema_root, type_name, text, text != NULL ? strlen(text) : 0u);
  switch (kind) {
  case DB_TEXT_STRING:
    return dbv_string(text != NULL ? text : "");
  case DB_TEXT_BYTES:
    return dbv_bytes((const uint8_t *)(text != NULL ? text : ""), text != NULL ? strlen(text) : 0);
  case DB_TEXT_UUID:
    return dbv_uuid_text(text);
  case DB_TEXT_DATETIME:
    return dbv_datetime_text(text);
  case DB_TEXT_DATE:
    return dbv_date_text(text);
  case DB_TEXT_TIME:
    return dbv_time_text(text);
  case DB_TEXT_DURATION:
    return dbv_duration_text(text);
  case DB_TEXT_DECIMAL:
    return dbv_decimal_text(text);
  case DB_TEXT_BIGINT:
    return dbv_bigint_text(text);
  case DB_TEXT_MONEY:
    return dbv_money_text(text);
  case DB_TEXT_BOOL:
    if (!db_parse_bool_text(text, &b)) return NULL;
    return dbv_bool(b);
  case DB_TEXT_INTEGER:
    return NULL;
  case DB_TEXT_NUMBER:
    if (!db_parse_double_text(text, &dbl)) return NULL;
    return dbv_double(dbl);
  default:
    return NULL;
  }
}

static DataBindValue *bind_text_scalar(Node *schema_root, const char *type_name,
                                       data_bind_text_kind_t kind, const char *text) {
  return dbv_attach_canonical_identity(
      type_name,
      bind_text_scalar_without_identity(schema_root, type_name, kind, text));
}

static DataBindValue *bind_field_default(Node *schema_root, Node *field) {
  const char *default_text = get_string_val(find_child(field, "default_value"));
  const char *type = get_string_val(find_child(field, "type"));
  data_bind_text_kind_t kind;
  if (default_text == NULL || type == NULL) return NULL;
  kind = bind_field_kind(schema_root, field);
  if (kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (kind == DB_TEXT_INTEGER) {
    if (strcmp(default_text, "true") == 0) return dbv_int(1);
    if (strcmp(default_text, "false") == 0) return dbv_int(0);
  }
  return bind_text_scalar(schema_root, type, kind, default_text);
}

static DataBindValue *json_integer_value(Node *schema_root, const char *type_name,
                                         json_value_t *value) {
  const char *text = NULL;
  size_t len = 0;
  if (value == NULL) return NULL;
  if (json_type(value) == JSON_NUMBER) {
    text = json_number_text(value, &len);
    if (text == NULL) return NULL;
  } else if (json_type(value) == JSON_STRING) {
    text = json_string(value);
    len = json_string_len(value);
  } else {
    return NULL;
  }
  return dbv_integer_text(schema_root, type_name, text, len);
}

static DataBindValue *json_flags_value(Node *schema_root, const char *type_name,
                                       json_value_t *value) {
  const type_meta_t *meta = find_scalar_meta(schema_root, type_name);
  uint64_t acc = 0;
  size_t i;
  DataBindValue *direct = json_integer_value(schema_root, type_name, value);
  if (direct != NULL) return direct;
  if (value == NULL || json_type(value) != JSON_ARRAY) return NULL;
  for (i = 0; i < json_array_size(value); i++) {
    uint64_t bits;
    DataBindValue *item =
        json_flags_value(schema_root, type_name, json_array_get(value, i));
    if (item == NULL || data_bind_value_get_uint64(item, &bits) != DATA_BIND_OK) {
      data_bind_value_free(item);
      return NULL;
    }
    data_bind_value_free(item);
    acc |= bits;
  }
  return dbv_integer_from_unsigned(meta, acc);
}

static DataBindValue *bind_json_value_without_identity(
    Node *schema_root, const char *type_name, data_bind_text_kind_t kind,
    json_value_t *value) {
  char number_buf[64];
  char decimal_buf[64];
  char bigint_buf[64];
  if (value == NULL || kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (is_flags_type(schema_root, type_name))
    return json_flags_value(schema_root, type_name, value);
  if (kind == DB_TEXT_INTEGER &&
      (json_type(value) == JSON_NUMBER ||
       (json_type(value) == JSON_STRING &&
        find_enum_record(schema_root, type_name) != NULL)))
    return json_integer_value(schema_root, type_name, value);
  switch (kind) {
  case DB_TEXT_STRING:
    if (json_type(value) == JSON_STRING) {
      return dbv_string_n(json_string(value), json_string_len(value));
    }
    if (json_type(value) == JSON_NUMBER) {
      snprintf(number_buf, sizeof(number_buf), "%.17g", json_number(value));
      return dbv_string(number_buf);
    }
    if (json_type(value) == JSON_BOOL)
      return dbv_string(json_bool(value) ? "true" : "false");
    return NULL;
  case DB_TEXT_BYTES:
    if (json_type(value) != JSON_STRING) return NULL;
    return dbv_bytes((const uint8_t *)json_string(value), json_string_len(value));
  case DB_TEXT_UUID:
    if (json_type(value) != JSON_STRING) return NULL;
    return dbv_uuid_text(json_string(value));
  case DB_TEXT_DATETIME:
    if (json_type(value) != JSON_STRING) return NULL;
    return dbv_datetime_text(json_string(value));
  case DB_TEXT_DATE:
    if (json_type(value) != JSON_STRING) return NULL;
    return dbv_date_text(json_string(value));
  case DB_TEXT_TIME:
    if (json_type(value) != JSON_STRING) return NULL;
    return dbv_time_text(json_string(value));
  case DB_TEXT_DURATION:
    if (json_type(value) == JSON_NUMBER)
      return dbv_duration((int64_t)json_number(value));
    if (json_type(value) != JSON_STRING) return NULL;
    return dbv_duration_text(json_string(value));
  case DB_TEXT_DECIMAL:
    if (json_type(value) == JSON_STRING)
      return dbv_decimal_text(json_string(value));
    if (json_type(value) == JSON_NUMBER) {
      snprintf(decimal_buf, sizeof(decimal_buf), "%.17g", json_number(value));
      return dbv_decimal_text(decimal_buf);
    }
    return NULL;
  case DB_TEXT_BIGINT:
    if (json_type(value) == JSON_STRING)
      return dbv_bigint_text(json_string(value));
    if (json_type(value) == JSON_NUMBER) {
      double n = json_number(value);
      if (!isfinite(n) || floor(n) != n || n < -9007199254740991.0 || n > 9007199254740991.0)
        return NULL;
      snprintf(bigint_buf, sizeof(bigint_buf), "%.0f", n);
      return dbv_bigint_text(bigint_buf);
    }
    return NULL;
  case DB_TEXT_MONEY:
    if (json_type(value) == JSON_STRING)
      return dbv_money_text(json_string(value));
    if (json_type(value) == JSON_OBJECT) {
      json_value_t *amount_value = json_object_get(value, "amount");
      json_value_t *currency_value = json_object_get(value, "currency");
      DataBindMoney money;
      char amount_buf[64];
      if (amount_value == NULL || currency_value == NULL ||
          json_type(currency_value) != JSON_STRING ||
          !db_validate_currency(json_string(currency_value)))
        return NULL;
      if (json_type(amount_value) == JSON_STRING) {
        if (!db_parse_decimal_text(json_string(amount_value), &money.amount)) return NULL;
      } else if (json_type(amount_value) == JSON_NUMBER) {
        snprintf(amount_buf, sizeof(amount_buf), "%.17g", json_number(amount_value));
        if (!db_parse_decimal_text(amount_buf, &money.amount)) return NULL;
      } else {
        return NULL;
      }
      memcpy(money.currency, json_string(currency_value), 3);
      money.currency[3] = '\0';
      return dbv_money(money);
    }
    return NULL;
  case DB_TEXT_BOOL:
    if (json_type(value) == JSON_BOOL) return dbv_bool(json_bool(value));
    if (json_type(value) == JSON_NUMBER)
      return dbv_bool(json_number(value) != 0.0);
    if (json_type(value) == JSON_STRING)
      return bind_text_scalar(schema_root, type_name, kind, json_string(value));
    return NULL;
  case DB_TEXT_INTEGER:
    if (json_type(value) == JSON_STRING)
      return bind_text_scalar(schema_root, type_name, kind, json_string(value));
    return NULL;
  case DB_TEXT_NUMBER:
    if (json_type(value) == JSON_NUMBER) return dbv_double(json_number(value));
    if (json_type(value) == JSON_BOOL)
      return dbv_double(json_bool(value) ? 1.0 : 0.0);
    if (json_type(value) == JSON_STRING)
      return bind_text_scalar(schema_root, type_name, kind, json_string(value));
    return NULL;
  default:
    return NULL;
  }
}

static DataBindValue *bind_json_value(Node *schema_root, const char *type_name,
                                      data_bind_text_kind_t kind, json_value_t *value) {
  return dbv_attach_canonical_identity(
      type_name,
      bind_json_value_without_identity(schema_root, type_name, kind, value));
}

static Node *union_variant(Node *union_node, const char *variant_name) {
  Node *fields = fields_node_for_record(union_node);
  size_t i;
  if (fields == NULL || variant_name == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    if (name != NULL && strcmp(name, variant_name) == 0) return field;
  }
  return NULL;
}

static Node *union_variant_binding(Node *union_node, const char *variant_name) {
  Node *fields = fields_node_for_record(union_node);
  size_t i;
  if (fields == NULL || variant_name == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; ++i) {
    Node *field = fields->data.list.items[i];
    if (field_accepts_name(field, variant_name)) return field;
  }
  return NULL;
}

static DataBindValue *bind_json_typed_value(Node *schema_root, const char *type_name,
                                            json_value_t *value);

static DataBindValue *bind_json_array(Node *schema_root, Node *field, json_value_t *value,
                                      DataBindValueKind list_kind) {
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  data_bind_text_kind_t scalar_kind;
  DataBindValue *list;
  size_t expected = 0;
  size_t i;
  if (value == NULL || json_type(value) != JSON_ARRAY || inner_type == NULL)
    return NULL;
  if (parse_size_value(get_string_val(find_child(field, "length_field")), &expected) &&
      json_array_size(value) != expected)
    return NULL;
  list = dbv_new(list_kind);
  if (list == NULL) return NULL;
  if (dbv_collection_reserve(list, json_array_size(value)) != DATA_BIND_OK) {
    data_bind_value_free(list);
    return NULL;
  }
  scalar_kind = bind_type_kind(schema_root, inner_type);
  for (i = 0; i < json_array_size(value); i++) {
    json_value_t *item = json_array_get(value, i);
    DataBindValue *bound;
    if (field_flag(field, "collection_element_is_composite") ||
        find_data_record(schema_root, inner_type) != NULL ||
        find_union_record(schema_root, inner_type) != NULL)
      bound = bind_json_typed_value(schema_root, inner_type, item);
    else bound = bind_json_value(schema_root, inner_type, scalar_kind, item);
    if (bound == NULL || dbv_collection_push(list, bound) != DATA_BIND_OK) {
      data_bind_value_free(bound);
      data_bind_value_free(list);
      return NULL;
    }
  }
  return list;
}

static DataBindValue *bind_json_record_array(Node *schema_root, const char *type_name,
                                             json_value_t *value) {
  DataBindValue *list;
  size_t i;
  if (value == NULL || json_type(value) != JSON_ARRAY || type_name == NULL) return NULL;
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list == NULL) return NULL;
  if (vec_reserve(&list->data.sequence.values, json_array_size(value)) != STL_OK) {
    data_bind_value_free(list);
    return NULL;
  }
  for (i = 0; i < json_array_size(value); i++) {
    DataBindValue *bound =
        bind_json_typed_value(schema_root, type_name, json_array_get(value, i));
    if (bound == NULL || dbv_collection_push(list, bound) != DATA_BIND_OK) {
      data_bind_value_free(bound);
      data_bind_value_free(list);
      return NULL;
    }
  }
  return list;
}

static DataBindValue *bind_json_map(Node *schema_root, Node *field, json_value_t *value) {
  const char *key_type = get_string_val(find_child(field, "key_type"));
  const char *value_type = get_string_val(find_child(field, "value_type"));
  data_bind_text_kind_t value_kind = bind_type_kind(schema_root, value_type);
  DataBindValue *map;
  size_t i;
  if (value == NULL || json_type(value) != JSON_OBJECT || key_type == NULL ||
      strcmp(key_type, "string") != 0 || value_type == NULL)
    return NULL;
  map = dbv_new(DATA_BIND_VALUE_MAP);
  if (map == NULL) return NULL;
  if (hash_map_reserve(&map->data.map.index, json_object_size(value)) != STL_OK ||
      vec_reserve(&map->data.map.ordered_entries, json_object_size(value)) !=
          STL_OK) {
    data_bind_value_free(map);
    return NULL;
  }
  for (i = 0; i < json_object_size(value); i++) {
    const char *key = json_object_key(value, i);
    json_value_t *item = json_object_value(value, i);
    DataBindValue *bound;
    if (key == NULL || item == NULL) continue;
    if (find_data_record(schema_root, value_type) != NULL ||
        find_union_record(schema_root, value_type) != NULL)
      bound = bind_json_typed_value(schema_root, value_type, item);
    else bound = bind_json_value(schema_root, value_type, value_kind, item);
    if (bound == NULL || !dbv_string_map_set(map, key, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(map);
      return NULL;
    }
  }
  return map;
}

static DataBindValue *bind_json_union(Node *schema_root, Node *union_node, json_value_t *object) {
  const char *variant_name;
  const char *variant_type;
  json_value_t *payload;
  Node *variant;
  DataBindValue *bound;
  DataBindValue *result;
  data_bind_text_kind_t scalar_kind;
  if (union_node == NULL || object == NULL || json_type(object) != JSON_OBJECT ||
      json_object_size(object) != 1)
    return NULL;
  variant_name = json_object_key(object, 0);
  payload = json_object_value(object, 0);
  variant = union_variant_binding(union_node, variant_name);
  if (variant == NULL || payload == NULL) return NULL;
  variant_name = get_string_val(find_child(variant, "name"));
  variant_type = get_string_val(find_child(variant, "type"));
  if (variant_name == NULL || variant_type == NULL) return NULL;
  if (find_data_record(schema_root, variant_type) != NULL ||
      find_union_record(schema_root, variant_type) != NULL)
    bound = bind_json_typed_value(schema_root, variant_type, payload);
  else {
    scalar_kind = bind_type_kind(schema_root, variant_type);
    bound = bind_json_value(schema_root, variant_type, scalar_kind, payload);
  }
  if (bound == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL || dbv_object_set(result, variant_name, bound) != DATA_BIND_OK) {
    data_bind_value_free(bound);
    data_bind_value_free(result);
    return NULL;
  }
  return result;
}

static DataBindValue *bind_json_object(Node *schema_root, Node *record, json_value_t *object) {
  DataBindValue *result;
  Node *fields;
  size_t i;
  if (record == NULL || object == NULL || json_type(object) != JSON_OBJECT) return NULL;
  fields = fields_node_for_record(record);
  if (fields == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    json_value_t *value;
    DataBindValue *bound = NULL;
    data_bind_text_kind_t kind;
    if (name == NULL) continue;
    value = json_field_value(field, object);
    if (value == NULL) {
      bound = bind_field_default(schema_root, field);
      if (bound != NULL && db_value_matches_field_format(field, bound)) {
        if (dbv_object_set(result, name, bound) != DATA_BIND_OK) {
          data_bind_value_free(bound);
          data_bind_value_free(result);
          return NULL;
        }
      } else if (bound != NULL) {
        data_bind_value_free(bound);
        data_bind_value_free(result);
        return NULL;
      } else if (!bind_field_missing_allowed(field)) {
        data_bind_value_free(result);
        return NULL;
      }
      continue;
    }
    if (field_flag(field, "is_group_field")) {
      bound = bind_json_record_array(schema_root, get_string_val(find_child(field, "group_type")),
                                     value);
    } else if (field_flag(field, "is_map")) {
      bound = bind_json_map(schema_root, field, value);
    } else if (field_flag(field, "is_collection")) {
      bound =
          bind_json_array(schema_root, field, value,
                          field_flag(field, "is_set") ? DATA_BIND_VALUE_SET : DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_composite_ref") && field_type != NULL) {
      bound = bind_json_typed_value(schema_root, field_type, value);
    } else if (field_type != NULL && find_union_record(schema_root, field_type) != NULL) {
      bound = bind_json_typed_value(schema_root, field_type, value);
    } else {
      kind = bind_field_kind(schema_root, field);
      bound = bind_json_value(schema_root, field_type, kind, value);
    }
    if (bound == NULL || !db_value_matches_field_format(field, bound) ||
        dbv_object_set(result, name, bound) != DATA_BIND_OK) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      return NULL;
    }
  }
  return result;
}

static DataBindValue *bind_json_typed_value(Node *schema_root, const char *type_name,
                                            json_value_t *value) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node;
  data_bind_text_kind_t scalar_kind;
  if (record != NULL) return bind_json_object(schema_root, record, value);
  union_node = find_union_record(schema_root, type_name);
  if (union_node != NULL) return bind_json_union(schema_root, union_node, value);
  scalar_kind = bind_type_kind(schema_root, type_name);
  return bind_json_value(schema_root, type_name, scalar_kind, value);
}

static int xml_join_path(char *out, size_t out_size, const char *prefix, const char *name) {
  int written;
  if (out == NULL || out_size == 0 || name == NULL) return 0;
  if (prefix != NULL && prefix[0] != '\0') written = snprintf(out, out_size, "%s/%s", prefix, name);
  else written = snprintf(out, out_size, "/*/%s", name);
  return written > 0 && (size_t)written < out_size;
}

static int xml_attr_path(char *out, size_t out_size, const char *prefix, const char *name) {
  int written;
  if (out == NULL || out_size == 0 || name == NULL) return 0;
  if (prefix != NULL && prefix[0] != '\0')
    written = snprintf(out, out_size, "%s/@%s", prefix, name);
  else written = snprintf(out, out_size, "/*/@%s", name);
  return written > 0 && (size_t)written < out_size;
}

static int xml_children_path(char *out, size_t out_size, const char *prefix) {
  int written;
  if (out == NULL || out_size == 0) return 0;
  written = snprintf(out, out_size, "%s/*", prefix != NULL && prefix[0] != '\0' ? prefix : "/*");
  return written > 0 && (size_t)written < out_size;
}

static int xml_path_exists(const salts_xml_document *doc, const char *path) {
  salts_xml_node_list nodes = {0};
  int found;
  if (!doc || !path) return 0;
  if (salts_xml_document_xpath_query(doc, path, &nodes, NULL, NULL) != QVM_STATUS_OK) {
    salts_xml_node_list_destroy(&nodes);
    return 0;
  }
  found = salts_xml_node_list_size(&nodes) != 0;
  salts_xml_node_list_destroy(&nodes);
  return found;
}

static const char *xml_path_text(const salts_xml_document *doc, const char *path) {
  salts_xml_node_list nodes = {0};
  const char *text = NULL;
  if (!doc || !path) return NULL;
  if (salts_xml_document_xpath_query(doc, path, &nodes, NULL, NULL) == QVM_STATUS_OK &&
      salts_xml_node_list_size(&nodes) != 0) {
    salts_xml_node node = salts_xml_node_list_at(&nodes, 0);
    text = salts_xml_node_text_view(node).data;
    /* An existing empty element binds as an empty string, not a missing field. */
    if (!text && salts_xml_node_type(node) == SALTS_XML_ELEMENT) text = "";
  }
  /* The selected list owns no node/text; text remains borrowed from doc. */
  salts_xml_node_list_destroy(&nodes);
  return text;
}

static int xml_field_path(const salts_xml_document *doc, const char *prefix, const char *name, char *out,
                          size_t out_size) {
  char child[256], attr[256];
  if (!xml_join_path(child, sizeof(child), prefix, name)) return 0;
  if (xml_path_exists(doc, child)) {
    snprintf(out, out_size, "%s", child);
    return out[0] != '\0' && strlen(out) < out_size;
  }
  if (!xml_attr_path(attr, sizeof(attr), prefix, name)) return 0;
  if (xml_path_exists(doc, attr)) {
    snprintf(out, out_size, "%s", attr);
    return out[0] != '\0' && strlen(out) < out_size;
  }
  return 0;
}

static int xml_field_binding_path(Node *field, const salts_xml_document *doc, const char *prefix, char *out,
                                  size_t out_size) {
  size_t i;
  for (i = 0; i < field_input_name_count(field); ++i) {
    const char *candidate = field_input_name_at(field, i);
    if (field_input_name_is_duplicate(field, i, candidate)) continue;
    if (xml_field_path(doc, prefix, candidate, out, out_size)) return 1;
  }
  return 0;
}

static DataBindValue *bind_xml_typed_value(Node *schema_root, const char *type_name,
                                           const salts_xml_document *doc, const char *path);

static DataBindValue *bind_xml_scalar_at_path(Node *schema_root, const char *type_name,
                                              data_bind_text_kind_t kind, const salts_xml_document *doc,
                                              const char *path) {
  const char *text;
  if (doc == NULL || path == NULL || kind == DB_TEXT_UNSUPPORTED) return NULL;
  text = xml_path_text(doc, path);
  if (text == NULL) return NULL;
  return bind_text_scalar(schema_root, type_name, kind, text);
}

static DataBindValue *bind_xml_list_at_path(Node *schema_root, Node *field, const salts_xml_document *doc,
                                            const char *path, DataBindValueKind list_kind) {
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  data_bind_text_kind_t scalar_kind = bind_type_kind(schema_root, inner_type);
  DataBindValue *list;
  salts_xml_node_list nodes = {0};
  size_t count = 0;
  int fixed_count;
  if (schema_root == NULL || field == NULL || doc == NULL || path == NULL || inner_type == NULL)
    return NULL;
  fixed_count = parse_size_value(get_string_val(find_child(field, "length_field")), &count);
  if (salts_xml_document_xpath_query(doc, path, &nodes, NULL, NULL) != QVM_STATUS_OK) {
    salts_xml_node_list_destroy(&nodes);
    return NULL;
  }
  if (fixed_count && (size_t)nodes.size != count) {
    salts_xml_node_list_destroy(&nodes);
    return NULL;
  }
  list = dbv_new(list_kind);
  if (list == NULL) {
    salts_xml_node_list_destroy(&nodes);
    return NULL;
  }
  for (size_t i = 0; i < nodes.size; i++) {
    char item_path[320];
    DataBindValue *item;
    if (snprintf(item_path, sizeof(item_path), "%s[%zu]", path, i + 1) >= (int)sizeof(item_path)) {
      data_bind_value_free(list);
      salts_xml_node_list_destroy(&nodes);
      return NULL;
    }
    if (field_flag(field, "collection_element_is_composite") ||
        find_data_record(schema_root, inner_type) != NULL ||
        find_union_record(schema_root, inner_type) != NULL)
      item = bind_xml_typed_value(schema_root, inner_type, doc, item_path);
    else item = bind_xml_scalar_at_path(schema_root, inner_type, scalar_kind, doc, item_path);
    if (item == NULL || dbv_collection_push(list, item) != DATA_BIND_OK) {
      data_bind_value_free(item);
      data_bind_value_free(list);
      salts_xml_node_list_destroy(&nodes);
      return NULL;
    }
  }
  salts_xml_node_list_destroy(&nodes);
  if (data_bind_value_count(list) == 0) {
    data_bind_value_free(list);
    return NULL;
  }
  return list;
}

static DataBindValue *bind_xml_map_at_path(Node *schema_root, Node *field, const salts_xml_document *doc,
                                           const char *path) {
  const char *key_type = get_string_val(find_child(field, "key_type"));
  const char *value_type = get_string_val(find_child(field, "value_type"));
  DataBindValue *map;
  salts_xml_node_list nodes = {0};
  char children[256];
  if (schema_root == NULL || field == NULL || doc == NULL || path == NULL ||
      key_type == NULL || strcmp(key_type, "string") != 0 || value_type == NULL)
    return NULL;
  if (!xml_children_path(children, sizeof(children), path)) return NULL;
  if (salts_xml_document_xpath_query(doc, children, &nodes, NULL, NULL) != QVM_STATUS_OK) {
    salts_xml_node_list_destroy(&nodes);
    return NULL;
  }
  map = dbv_new(DATA_BIND_VALUE_MAP);
  if (map == NULL) {
    salts_xml_node_list_destroy(&nodes);
    return NULL;
  }
  for (size_t i = 0; i < salts_xml_node_list_size(&nodes); ++i) {
    salts_xml_node node = salts_xml_node_list_at(&nodes, i);
    const char *key = salts_xml_node_display_name(node).data;
    DataBindValue *item;
    char item_path[320];
    if (key == NULL || dbv_map_has_key(map, key)) continue;
    if (!xml_join_path(item_path, sizeof(item_path), path, key)) {
      data_bind_value_free(map);
      salts_xml_node_list_destroy(&nodes);
      return NULL;
    }
    if (find_data_record(schema_root, value_type) != NULL ||
        find_union_record(schema_root, value_type) != NULL)
      item = bind_xml_typed_value(schema_root, value_type, doc, item_path);
    else
      item = bind_xml_scalar_at_path(schema_root, value_type,
                                     bind_type_kind(schema_root, value_type), doc, item_path);
    if (item != NULL) {
      if (!dbv_string_map_set(map, key, item)) {
        data_bind_value_free(item);
        data_bind_value_free(map);
        salts_xml_node_list_destroy(&nodes);
        return NULL;
      }
    } else if (!db_text_is_empty(salts_xml_node_text_view(node).data)) {
      data_bind_value_free(map);
      salts_xml_node_list_destroy(&nodes);
      return NULL;
    }
  }
  salts_xml_node_list_destroy(&nodes);
  if (data_bind_value_count(map) == 0) {
    data_bind_value_free(map);
    return NULL;
  }
  return map;
}

static DataBindValue *bind_xml_union_at_path(Node *schema_root, Node *union_node,
                                             const salts_xml_document *doc, const char *path) {
  Node *fields = fields_node_for_record(union_node);
  DataBindValue *result;
  int matches = 0;
  size_t i;
  if (fields == NULL || doc == NULL || path == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *variant = fields->data.list.items[i];
    const char *name = get_string_val(find_child(variant, "name"));
    const char *variant_type = get_string_val(find_child(variant, "type"));
    data_bind_text_kind_t scalar_kind = bind_type_kind(schema_root, variant_type);
    char item_path[256];
    DataBindValue *item = NULL;
    if (name == NULL || variant_type == NULL) {
      data_bind_value_free(result);
      return NULL;
    }
    if (!xml_field_binding_path(variant, doc, path, item_path, sizeof(item_path))) continue;
    if (find_data_record(schema_root, variant_type) != NULL ||
        find_union_record(schema_root, variant_type) != NULL)
      item = bind_xml_typed_value(schema_root, variant_type, doc, item_path);
    else item = bind_xml_scalar_at_path(schema_root, variant_type, scalar_kind, doc, item_path);
    if (item == NULL || dbv_object_set(result, name, item) != DATA_BIND_OK) {
      data_bind_value_free(item);
      data_bind_value_free(result);
      return NULL;
    }
    matches++;
  }
  if (matches != 1) {
    data_bind_value_free(result);
    return NULL;
  }
  return result;
}

static DataBindValue *bind_xml_record_at_path(Node *schema_root, Node *record, const salts_xml_document *doc,
                                              const char *path) {
  Node *fields = fields_node_for_record(record);
  DataBindValue *result;
  size_t i;
  if (fields == NULL || doc == NULL || path == NULL || !xml_path_exists(doc, path)) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    char field_path[256];
    DataBindValue *bound = NULL;
    int has_field_path;
    if (name == NULL) continue;
    has_field_path =
        xml_field_binding_path(field, doc, path, field_path, sizeof(field_path));
    if (field_flag(field, "is_group_field")) {
      if (has_field_path)
        bound = bind_xml_list_at_path(schema_root, field, doc, field_path, DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_map")) {
      if (has_field_path)
        bound = bind_xml_map_at_path(schema_root, field, doc, field_path);
    } else if (field_flag(field, "is_collection")) {
      if (has_field_path)
        bound = bind_xml_list_at_path(schema_root, field, doc, field_path,
                                      field_flag(field, "is_set") ? DATA_BIND_VALUE_SET
                                                                  : DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_composite_ref") ||
               find_union_record(schema_root, field_type)) {
      if (has_field_path)
        bound = bind_xml_typed_value(schema_root, field_type, doc, field_path);
    } else {
      if (has_field_path)
        bound = bind_xml_scalar_at_path(schema_root, field_type,
                                        bind_field_kind(schema_root, field), doc, field_path);
    }
    if (bound == NULL) bound = bind_field_default(schema_root, field);
    if (bound == NULL) {
      if (bind_field_missing_allowed(field)) continue;
      data_bind_value_free(result);
      return NULL;
    }
    if (!db_value_matches_field_format(field, bound) ||
        dbv_object_set(result, name, bound) != DATA_BIND_OK) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      return NULL;
    }
  }
  return result;
}

static DataBindValue *bind_xml_typed_value(Node *schema_root, const char *type_name,
                                           const salts_xml_document *doc, const char *path) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node = find_union_record(schema_root, type_name);
  data_bind_text_kind_t kind = bind_type_kind(schema_root, type_name);
  if (record != NULL) return bind_xml_record_at_path(schema_root, record, doc, path);
  if (union_node != NULL) return bind_xml_union_at_path(schema_root, union_node, doc, path);
  return bind_xml_scalar_at_path(schema_root, type_name, kind, doc, path);
}

static void csv_headers_free(data_bind_csv_headers_t *headers) {
  size_t i;
  if (headers == NULL) return;
  for (i = 0; i < headers->count; i++)
    free(headers->names[i]);
  free(headers->names);
  memset(headers, 0, sizeof(*headers));
}

static int csv_headers_push(data_bind_csv_headers_t *headers, const char *text, size_t len) {
  char **items;
  char *copy;
  size_t capacity;
  if (headers == NULL || text == NULL) return 0;
  if (headers->count == headers->capacity) {
    capacity = headers->capacity == 0 ? 8 : headers->capacity * 2;
    items = (char **)realloc(headers->names, capacity * sizeof(*items));
    if (items == NULL) return 0;
    headers->names = items;
    headers->capacity = capacity;
  }
  copy = (char *)malloc(len + 1);
  if (copy == NULL) return 0;
  memcpy(copy, text, len);
  copy[len] = '\0';
  headers->names[headers->count++] = copy;
  return 1;
}

static int csv_parse_header_names(const char *csv, size_t len, data_bind_csv_headers_t *headers) {
  size_t i = 0;
  char *cell = NULL;
  size_t cell_len = 0, cell_cap = 0;
  int in_quotes = 0;
  if (csv == NULL || headers == NULL) return 0;
  memset(headers, 0, sizeof(*headers));
  while (i < len) {
    char ch = csv[i++];
    if (in_quotes) {
      if (ch == '"') {
        if (i < len && csv[i] == '"') ch = csv[i++];
        else {
          in_quotes = 0;
          continue;
        }
      }
    } else if (ch == '"') {
      in_quotes = 1;
      continue;
    } else if (ch == ',' || ch == '\n' || ch == '\r') {
      if (!csv_headers_push(headers, cell != NULL ? cell : "", cell_len)) goto fail;
      cell_len = 0;
      if (ch == '\n' || ch == '\r') {
        free(cell);
        return headers->count > 0;
      }
      continue;
    }
    if (cell_len + 1 >= cell_cap) {
      size_t next_cap = cell_cap == 0 ? 32 : cell_cap * 2;
      char *next = (char *)realloc(cell, next_cap);
      if (next == NULL) goto fail;
      cell = next;
      cell_cap = next_cap;
    }
    cell[cell_len++] = ch;
  }
  if (!csv_headers_push(headers, cell != NULL ? cell : "", cell_len)) goto fail;
  free(cell);
  return headers->count > 0;
fail:
  free(cell);
  csv_headers_free(headers);
  return 0;
}

static void csv_sanitize_path(const char *path, char *out, size_t out_size) {
  size_t w = 0;
  int last_underscore = 0;
  size_t i;
  if (out == NULL || out_size == 0) return;
  if (path == NULL) {
    out[0] = '\0';
    return;
  }
  for (i = 0; path[i] != '\0' && w + 1 < out_size; i++) {
    char ch = path[i];
    if (ch == ']' || ch == ')') continue;
    if (ch == '.' || ch == '[' || ch == '(') {
      if (w > 0 && !last_underscore) {
        out[w++] = '_';
        last_underscore = 1;
      }
      continue;
    }
    out[w++] = ch;
    last_underscore = 0;
  }
  if (w > 0 && out[w - 1] == '_') w--;
  out[w] = '\0';
}

static int csv_find_named_column(csv_doc_t *doc, const char *name, size_t *out_col) {
  char typed_name[256];
  size_t col;
  if (doc == NULL || name == NULL || out_col == NULL) return 0;
  col = csv_find_column(doc, name);
  if (col < csv_column_count(doc)) {
    *out_col = col;
    return 1;
  }
  if (snprintf(typed_name, sizeof(typed_name), "%s_n", name) < (int)sizeof(typed_name)) {
    col = csv_find_column(doc, typed_name);
    if (col < csv_column_count(doc)) {
      *out_col = col;
      return 1;
    }
  }
  if (snprintf(typed_name, sizeof(typed_name), "%s_s", name) < (int)sizeof(typed_name)) {
    col = csv_find_column(doc, typed_name);
    if (col < csv_column_count(doc)) {
      *out_col = col;
      return 1;
    }
  }
  return 0;
}

static int csv_find_path_column(csv_doc_t *doc, const char *path, size_t *out_col) {
  char sanitized[256];
  if (csv_find_named_column(doc, path, out_col)) return 1;
  csv_sanitize_path(path, sanitized, sizeof(sanitized));
  if (sanitized[0] != '\0' && strcmp(sanitized, path) != 0)
    return csv_find_named_column(doc, sanitized, out_col);
  return 0;
}

static int csv_join_path(char *out, size_t out_size, const char *prefix, const char *name) {
  int written;
  if (out == NULL || out_size == 0 || name == NULL) return 0;
  if (prefix != NULL && prefix[0] != '\0') written = snprintf(out, out_size, "%s.%s", prefix, name);
  else written = snprintf(out, out_size, "%s", name);
  return written > 0 && (size_t)written < out_size;
}

static int csv_index_path(char *out, size_t out_size, const char *prefix, size_t index) {
  int written;
  if (out == NULL || out_size == 0 || prefix == NULL) return 0;
  written = snprintf(out, out_size, "%s[%zu]", prefix, index);
  return written > 0 && (size_t)written < out_size;
}

static int index_list_push(data_bind_index_list_t *indexes, size_t value) {
  size_t *items;
  size_t i, capacity;
  if (indexes == NULL) return 0;
  for (i = 0; i < indexes->count; i++)
    if (indexes->values[i] == value) return 1;
  if (indexes->count == indexes->capacity) {
    capacity = indexes->capacity == 0 ? 8 : indexes->capacity * 2;
    items = (size_t *)realloc(indexes->values, capacity * sizeof(*items));
    if (items == NULL) return 0;
    indexes->values = items;
    indexes->capacity = capacity;
  }
  indexes->values[indexes->count++] = value;
  return 1;
}

static int index_compare(const void *a, const void *b) {
  size_t lhs = *(const size_t *)a;
  size_t rhs = *(const size_t *)b;
  return (lhs > rhs) - (lhs < rhs);
}

static int csv_header_index(const char *header, const char *path, size_t *out_index) {
  char sanitized[256];
  const char *start = NULL;
  char *end = NULL;
  unsigned long value;
  size_t path_len;
  if (header == NULL || path == NULL || out_index == NULL) return 0;
  path_len = strlen(path);
  if (strncmp(header, path, path_len) == 0 && header[path_len] == '[') {
    start = header + path_len + 1;
  } else {
    csv_sanitize_path(path, sanitized, sizeof(sanitized));
    path_len = strlen(sanitized);
    if (path_len > 0 && strncmp(header, sanitized, path_len) == 0 && header[path_len] == '_')
      start = header + path_len + 1;
  }
  if (start == NULL || !isdigit((unsigned char)start[0])) return 0;
  errno = 0;
  value = strtoul(start, &end, 10);
  if (errno != 0 || end == NULL || end == start) return 0;
  if (*end != ']' && *end != '_' && *end != '.' && *end != '\0') return 0;
  *out_index = (size_t)value;
  return 1;
}

static int csv_collect_indexes(const data_bind_csv_headers_t *headers, const char *path,
                               data_bind_index_list_t *indexes) {
  size_t i;
  if (headers == NULL || path == NULL || indexes == NULL) return 0;
  for (i = 0; i < headers->count; i++) {
    size_t index = 0;
    if (csv_header_index(headers->names[i], path, &index) && !index_list_push(indexes, index))
      return 0;
  }
  if (indexes->count > 1) qsort(indexes->values, indexes->count, sizeof(size_t), index_compare);
  return indexes->count > 0;
}

static int csv_header_matches_path(const char *header, const char *path);

static int csv_headers_have_path(const data_bind_csv_headers_t *headers, const char *path) {
  size_t i;
  if (headers == NULL || path == NULL) return 0;
  for (i = 0; i < headers->count; i++) {
    const char *header = headers->names[i];
    if (csv_header_matches_path(header, path)) return 1;
  }
  return 0;
}

static int csv_field_binding_path(Node *field, const data_bind_csv_headers_t *headers,
                                  const char *prefix, char *out, size_t out_size) {
  size_t i;
  for (i = 0; i < field_input_name_count(field); ++i) {
    const char *candidate = field_input_name_at(field, i);
    if (field_input_name_is_duplicate(field, i, candidate)) continue;
    if (csv_join_path(out, out_size, prefix, candidate) &&
        csv_headers_have_path(headers, out))
      return 1;
  }
  if (out != NULL && out_size != 0) out[0] = '\0';
  return 0;
}

static int csv_header_has_typed_suffix(const char *suffix) {
  return suffix != NULL && suffix[0] == '_' && (suffix[1] == 'n' || suffix[1] == 's') &&
         suffix[2] == '\0';
}

static int csv_header_matches_path(const char *header, const char *path) {
  char sanitized[256];
  size_t path_len, sanitized_len;
  if (header == NULL || path == NULL) return 0;
  path_len = strlen(path);
  csv_sanitize_path(path, sanitized, sizeof(sanitized));
  sanitized_len = strlen(sanitized);
  if (strcmp(header, path) == 0) return 1;
  if (path_len > 0 && strncmp(header, path, path_len) == 0 &&
      csv_header_has_typed_suffix(header + path_len))
    return 1;
  if (path_len > 0 && strncmp(header, path, path_len) == 0 &&
      (header[path_len] == '.' || header[path_len] == '['))
    return 1;
  if (sanitized_len > 0 && strcmp(header, sanitized) == 0) return 1;
  if (sanitized_len > 0 && strncmp(header, sanitized, sanitized_len) == 0 &&
      csv_header_has_typed_suffix(header + sanitized_len))
    return 1;
  if (sanitized_len > 0 && strncmp(header, sanitized, sanitized_len) == 0 &&
      header[sanitized_len] == '_')
    return 1;
  return 0;
}

static int csv_row_has_nonempty_path(csv_doc_t *doc, size_t row,
                                     const data_bind_csv_headers_t *headers, const char *path) {
  size_t i;
  if (doc == NULL || headers == NULL || path == NULL || row >= csv_row_count(doc)) return 0;
  for (i = 0; i < headers->count; i++) {
    const char *text;
    if (!csv_header_matches_path(headers->names[i], path)) continue;
    text = csv_get(doc, row, i);
    if (!db_text_is_empty(text)) return 1;
  }
  return 0;
}

static int csv_header_map_key(const char *header, const char *path, char *key, size_t key_size) {
  size_t path_len, len;
  const char *start = NULL;
  const char *end;
  int sanitized_path = 0;
  if (header == NULL || path == NULL || key == NULL || key_size == 0) return 0;
  path_len = strlen(path);
  if (strncmp(header, path, path_len) == 0 && header[path_len] == '.')
    start = header + path_len + 1;
  else if (strncmp(header, path, path_len) == 0 && header[path_len] == '_') {
    start = header + path_len + 1;
    sanitized_path = 1;
  }
  if (start == NULL || start[0] == '\0') return 0;
  end = start;
  while (*end != '\0' && *end != '.' && *end != '[' && (!sanitized_path || *end != '_'))
    end++;
  len = (size_t)(end - start);
  if (len == 0 || len >= key_size) return 0;
  memcpy(key, start, len);
  key[len] = '\0';
  return 1;
}

static DataBindValue *bind_csv_typed_value(Node *schema_root, const char *type_name,
                                           csv_doc_t *doc, size_t row,
                                           const data_bind_csv_headers_t *headers,
                                           const char *path);

static DataBindValue *bind_csv_scalar_at_path(Node *schema_root, const char *type_name,
                                              data_bind_text_kind_t kind, csv_doc_t *doc,
                                              size_t row, const char *path) {
  size_t col = 0;
  const char *text;
  if (doc == NULL || path == NULL || kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (!csv_find_path_column(doc, path, &col)) return NULL;
  text = csv_get(doc, row, col);
  if (text == NULL) return NULL;
  return bind_text_scalar(schema_root, type_name, kind, text);
}

static DataBindValue *bind_csv_scalar_value(Node *schema_root, const char *type_name,
                                            data_bind_text_kind_t kind, csv_doc_t *doc,
                                            size_t row) {
  size_t col = 0;
  const char *text;
  if (doc == NULL || row >= csv_row_count(doc) || kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (!csv_find_path_column(doc, "value", &col)) col = 0;
  text = csv_get(doc, row, col);
  if (text == NULL) return NULL;
  return bind_text_scalar(schema_root, type_name, kind, text);
}

static DataBindValue *bind_csv_map_at_path(Node *schema_root, Node *field, csv_doc_t *doc,
                                           size_t row, const data_bind_csv_headers_t *headers,
                                           const char *path) {
  const char *key_type = get_string_val(find_child(field, "key_type"));
  const char *value_type = get_string_val(find_child(field, "value_type"));
  data_bind_text_kind_t value_kind = bind_type_kind(schema_root, value_type);
  DataBindValue *map;
  size_t i;
  if (key_type == NULL || strcmp(key_type, "string") != 0 || value_type == NULL ||
      headers == NULL || path == NULL)
    return NULL;
  map = dbv_new(DATA_BIND_VALUE_MAP);
  if (map == NULL) return NULL;
  for (i = 0; i < headers->count; i++) {
    char key[128], item_path[256];
    DataBindValue *item = NULL;
    if (!csv_header_map_key(headers->names[i], path, key, sizeof(key))) continue;
    if (dbv_map_has_key(map, key)) continue;
    if (!csv_join_path(item_path, sizeof(item_path), path, key)) {
      data_bind_value_free(map);
      return NULL;
    }
    if (find_data_record(schema_root, value_type) || find_union_record(schema_root, value_type))
      item = bind_csv_typed_value(schema_root, value_type, doc, row, headers, item_path);
    else item = bind_csv_scalar_at_path(schema_root, value_type, value_kind, doc, row, item_path);
    if (item != NULL) {
      if (!dbv_string_map_set(map, key, item)) {
        data_bind_value_free(item);
        data_bind_value_free(map);
        return NULL;
      }
    } else if (csv_row_has_nonempty_path(doc, row, headers, item_path)) {
      data_bind_value_free(map);
      return NULL;
    }
  }
  if (data_bind_value_count(map) == 0) {
    data_bind_value_free(map);
    return NULL;
  }
  return map;
}

static DataBindValue *bind_csv_list_at_path(Node *schema_root, Node *field, csv_doc_t *doc,
                                            size_t row, const data_bind_csv_headers_t *headers,
                                            const char *path, DataBindValueKind list_kind) {
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  data_bind_text_kind_t scalar_kind = bind_type_kind(schema_root, inner_type);
  data_bind_index_list_t indexes = {0};
  DataBindValue *list;
  size_t count = 0, i;
  int fixed_count;
  if (inner_type == NULL || path == NULL) return NULL;
  fixed_count = parse_size_value(get_string_val(find_child(field, "length_field")), &count);
  if (fixed_count) {
    for (i = 0; i < count; i++)
      if (!index_list_push(&indexes, i)) goto fail_indexes;
  } else if (!csv_collect_indexes(headers, path, &indexes)) {
    goto fail_indexes;
  }
  list = dbv_new(list_kind);
  if (list == NULL) goto fail_indexes;
  for (i = 0; i < indexes.count; i++) {
    char item_path[256];
    DataBindValue *item = NULL;
    if (!csv_index_path(item_path, sizeof(item_path), path, indexes.values[i])) {
      data_bind_value_free(list);
      goto fail_indexes;
    }
    if (field_flag(field, "collection_element_is_composite") ||
        find_data_record(schema_root, inner_type) || find_union_record(schema_root, inner_type))
      item = bind_csv_typed_value(schema_root, inner_type, doc, row, headers, item_path);
    else item = bind_csv_scalar_at_path(schema_root, inner_type, scalar_kind, doc, row, item_path);
    if (item != NULL) {
      if (dbv_collection_push(list, item) != DATA_BIND_OK) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        goto fail_indexes;
      }
    } else if (fixed_count || csv_row_has_nonempty_path(doc, row, headers, item_path)) {
      data_bind_value_free(list);
      goto fail_indexes;
    }
  }
  free(indexes.values);
  if (data_bind_value_count(list) == 0) {
    data_bind_value_free(list);
    return NULL;
  }
  return list;
fail_indexes:
  free(indexes.values);
  return NULL;
}

static DataBindValue *bind_csv_union_at_path(Node *schema_root, Node *union_node,
                                             csv_doc_t *doc, size_t row,
                                             const data_bind_csv_headers_t *headers,
                                             const char *path) {
  Node *fields = fields_node_for_record(union_node);
  DataBindValue *result;
  int matches = 0;
  size_t i;
  if (fields == NULL || path == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *variant = fields->data.list.items[i];
    const char *name = get_string_val(find_child(variant, "name"));
    const char *variant_type = get_string_val(find_child(variant, "type"));
    char item_path[256];
    DataBindValue *item;
    if (name == NULL || variant_type == NULL) {
      data_bind_value_free(result);
      return NULL;
    }
    if (!csv_field_binding_path(variant, headers, path, item_path, sizeof(item_path)) ||
        !csv_row_has_nonempty_path(doc, row, headers, item_path))
      continue;
    item = bind_csv_typed_value(schema_root, variant_type, doc, row, headers, item_path);
    if (item == NULL || dbv_object_set(result, name, item) != DATA_BIND_OK) {
      data_bind_value_free(item);
      data_bind_value_free(result);
      return NULL;
    }
    matches++;
  }
  if (matches != 1) {
    data_bind_value_free(result);
    return NULL;
  }
  return result;
}

static DataBindValue *bind_csv_record_at_path(Node *schema_root, Node *record, csv_doc_t *doc,
                                              size_t row, const data_bind_csv_headers_t *headers,
                                              const char *prefix) {
  Node *fields = fields_node_for_record(record);
  DataBindValue *result;
  size_t i;
  if (fields == NULL || doc == NULL || row >= csv_row_count(doc)) return NULL;
  if (prefix != NULL && prefix[0] != '\0' && !csv_headers_have_path(headers, prefix)) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    char path[256];
    DataBindValue *bound = NULL;
    int has_field_path;
    if (name == NULL) continue;
    has_field_path =
        csv_field_binding_path(field, headers, prefix, path, sizeof(path));
    if (field_flag(field, "is_group_field")) {
      if (has_field_path)
        bound =
            bind_csv_list_at_path(schema_root, field, doc, row, headers, path, DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_map")) {
      if (has_field_path) bound = bind_csv_map_at_path(schema_root, field, doc, row, headers, path);
    } else if (field_flag(field, "is_collection")) {
      if (has_field_path)
        bound = bind_csv_list_at_path(schema_root, field, doc, row, headers, path,
                                      field_flag(field, "is_set") ? DATA_BIND_VALUE_SET
                                                                  : DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_composite_ref") ||
               find_union_record(schema_root, field_type)) {
      if (has_field_path)
        bound = bind_csv_typed_value(schema_root, field_type, doc, row, headers, path);
    } else {
      if (has_field_path)
        bound = bind_csv_scalar_at_path(schema_root, field_type,
                                        bind_field_kind(schema_root, field), doc, row, path);
    }
    if (bound == NULL) bound = bind_field_default(schema_root, field);
    if (bound == NULL) {
      if (bind_field_missing_allowed(field) &&
          (!has_field_path || !csv_row_has_nonempty_path(doc, row, headers, path)))
        continue;
      data_bind_value_free(result);
      return NULL;
    }
    if (!db_value_matches_field_format(field, bound) ||
        dbv_object_set(result, name, bound) != DATA_BIND_OK) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      return NULL;
    }
  }
  return result;
}

static DataBindValue *bind_csv_typed_value(Node *schema_root, const char *type_name,
                                           csv_doc_t *doc, size_t row,
                                           const data_bind_csv_headers_t *headers,
                                           const char *path) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node = find_union_record(schema_root, type_name);
  data_bind_text_kind_t kind = bind_type_kind(schema_root, type_name);
  if (record != NULL) return bind_csv_record_at_path(schema_root, record, doc, row, headers, path);
  if (union_node != NULL)
    return bind_csv_union_at_path(schema_root, union_node, doc, row, headers, path);
  if (path != NULL && path[0] != '\0')
    return bind_csv_scalar_at_path(schema_root, type_name, kind, doc, row, path);
  return bind_csv_scalar_value(schema_root, type_name, kind, doc, row);
}

static DataBindSchemaKind schema_record_kind(const char *list_name, Node *record) {
  if (record != NULL && field_flag(record, "is_flags")) return DATA_BIND_SCHEMA_FLAGS;
  if (list_name != NULL) {
    if (strcmp(list_name, "messages") == 0) return DATA_BIND_SCHEMA_MESSAGE;
    if (strcmp(list_name, "composites") == 0) return DATA_BIND_SCHEMA_COMPOSITE;
    if (strcmp(list_name, "groups") == 0) return DATA_BIND_SCHEMA_GROUP;
    if (strcmp(list_name, "enums") == 0) return DATA_BIND_SCHEMA_ENUM;
    if (strcmp(list_name, "unions") == 0) return DATA_BIND_SCHEMA_UNION;
  }
  if (field_flag(record, "is_message_decl")) return DATA_BIND_SCHEMA_MESSAGE;
  if (field_flag(record, "is_composite_decl")) return DATA_BIND_SCHEMA_COMPOSITE;
  if (field_flag(record, "is_group_decl")) return DATA_BIND_SCHEMA_GROUP;
  if (field_flag(record, "is_union_decl")) return DATA_BIND_SCHEMA_UNION;
  return DATA_BIND_SCHEMA_UNKNOWN;
}

static Node *fields_node_for_record(Node *record) {
  Node *fields = find_child(record, "fields");
  return fields != NULL && fields->type == NODE_LIST ? fields : NULL;
}

static int binding_name_is_portable(const char *name) {
  size_t i;
  if (name == NULL ||
      !((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') ||
        name[0] == '_'))
    return 0;
  for (i = 1; name[i] != '\0'; ++i)
    if (!((name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-'))
      return 0;
  return 1;
}

static DataBindStatus validate_field_binding_attributes(const char *record_name, Node *field,
                                                        DataBindError *error) {
  static const char *const attributes[] = {"name", "alias"};
  const char *field_name = get_string_val(find_child(field, "name"));
  size_t attribute_index;
  if (node_attribute_count(field, "name") > 1u) {
    char path[260];
    snprintf(path, sizeof(path), "%s.%s", record_name != NULL ? record_name : "",
             field_name != NULL ? field_name : "");
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, path, -1, -1,
                        "Field has multiple name mappings");
  }
  for (attribute_index = 0;
       attribute_index < sizeof(attributes) / sizeof(attributes[0]); ++attribute_index) {
    const char *attribute = attributes[attribute_index];
    size_t value_index;
    for (value_index = 0; value_index < node_attribute_count(field, attribute); ++value_index) {
      const char *value = node_attribute_value_at(field, attribute, value_index);
      if (!binding_name_is_portable(value)) {
        char path[260];
        snprintf(path, sizeof(path), "%s.%s", record_name != NULL ? record_name : "",
                 field_name != NULL ? field_name : "");
        return db_error_set(error, DATA_BIND_ERR_SCHEMA, path, -1, -1,
                            "Binding %s '%s' is not portable across JSON/YAML/XML/CSV",
                            attribute, value != NULL ? value : "");
      }
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus validate_record_binding_names(Node *record, DataBindError *error) {
  Node *fields = fields_node_for_record(record);
  const char *record_name = get_string_val(find_child(record, "name"));
  size_t i;
  if (fields == NULL) return DATA_BIND_OK;
  for (i = 0; i < fields->data.list.count; ++i) {
    Node *left = fields->data.list.items[i];
    const char *left_canonical = get_string_val(find_child(left, "name"));
    size_t j;
    DataBindStatus attribute_status =
        validate_field_binding_attributes(record_name, left, error);
    if (attribute_status != DATA_BIND_OK) return attribute_status;
    for (j = i + 1; j < fields->data.list.count; ++j) {
      Node *right = fields->data.list.items[j];
      size_t k;
      for (k = 0; k < field_input_name_count(left); ++k) {
        const char *accepted = field_input_name_at(left, k);
        if (!field_input_name_is_duplicate(left, k, accepted) &&
            field_accepts_name(right, accepted)) {
          char path[260];
          snprintf(path, sizeof(path), "%s.%s", record_name != NULL ? record_name : "",
                   left_canonical != NULL ? left_canonical : "");
          return db_error_set(error, DATA_BIND_ERR_SCHEMA, path, -1, -1,
                              "Binding name '%s' is shared by multiple fields",
                              accepted);
        }
      }
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus validate_schema_binding_names(Node *schema_root, DataBindError *error) {
  static const char *const record_lists[] = {"messages", "composites", "groups", "unions"};
  size_t i;
  for (i = 0; i < sizeof(record_lists) / sizeof(record_lists[0]); ++i) {
    Node *records = find_child(schema_root, record_lists[i]);
    size_t j;
    if (records == NULL || records->type != NODE_LIST) continue;
    for (j = 0; j < records->data.list.count; ++j) {
      DataBindStatus status = validate_record_binding_names(records->data.list.items[j], error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static Node *items_node_for_enum(Node *record) {
  Node *items = find_child(record, "items");
  return items != NULL && items->type == NODE_LIST ? items : NULL;
}

static atomic_size_t g_dynamic_graph_fail_after = SIZE_MAX;

static void *db_dynamic_graph_calloc(size_t count, size_t size) {
  size_t remaining = atomic_load_explicit(&g_dynamic_graph_fail_after,
                                          memory_order_relaxed);
  while (remaining != SIZE_MAX) {
    if (remaining == 0u) return NULL;
    if (atomic_compare_exchange_weak_explicit(
            &g_dynamic_graph_fail_after, &remaining, remaining - 1u,
            memory_order_relaxed, memory_order_relaxed))
      break;
  }
  return calloc(count, size);
}

static char *db_dynamic_graph_strdup(const char *text) {
  size_t len;
  char *copy;
  if (text == NULL) return NULL;
  len = strlen(text);
  if (len == SIZE_MAX) return NULL;
  copy = (char *)db_dynamic_graph_calloc(len + 1u, 1u);
  if (copy != NULL) memcpy(copy, text, len + 1u);
  return copy;
}

static int db_dynamic_schema_node_count(const Node *node, unsigned depth,
                                        size_t *count) {
  size_t child_count = 0u;
  size_t i;
  if (node == NULL || count == NULL ||
      depth > DATA_BIND_SEMANTIC_MAX_DEPTH || *count == SIZE_MAX)
    return 0;
  ++*count;
  if (node->type == NODE_LIST) child_count = node->data.list.count;
  else if (node->type == NODE_ROOT || node->type == NODE_MAP)
    child_count = node->data.map.count;
  for (i = 0u; i < child_count; ++i) {
    const Node *child = node->type == NODE_LIST ? node->data.list.items[i]
                                                : node->data.map.items[i];
    if (!db_dynamic_schema_node_count(child, depth + 1u, count)) return 0;
  }
  return 1;
}

static char *db_dynamic_join_type(const char *constructor,
                                  const char *first,
                                  const char *second) {
  size_t constructor_len;
  size_t first_len;
  size_t second_len = second != NULL ? strlen(second) : 0u;
  size_t len;
  char *text;
  if (constructor == NULL || first == NULL) return NULL;
  constructor_len = strlen(constructor);
  first_len = strlen(first);
  if (constructor_len > SIZE_MAX - first_len - second_len - 5u) return NULL;
  len = constructor_len + first_len + second_len + (second != NULL ? 3u : 2u);
  text = (char *)db_dynamic_graph_calloc(len + 1u, 1u);
  if (text == NULL) return NULL;
  if (second != NULL)
    snprintf(text, len + 1u, "%s<%s,%s>", constructor, first, second);
  else
    snprintf(text, len + 1u, "%s<%s>", constructor, first);
  return text;
}

static db_dynamic_type_t *db_dynamic_graph_find(db_dynamic_graph_t *graph,
                                                const char *semantic_key) {
  size_t i;
  if (graph == NULL || semantic_key == NULL) return NULL;
  for (i = 0u; i < graph->node_count; ++i)
    if (graph->nodes[i].semantic_key != NULL &&
        strcmp(graph->nodes[i].semantic_key, semantic_key) == 0)
      return &graph->nodes[i];
  return NULL;
}

static char *db_dynamic_stable_id(
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    const char *semantic_key) {
  static const char prefix[] = "salts-utils.databind.dynamic.v2:";
  static const char hex[] = "0123456789abcdef";
  size_t prefix_len = sizeof(prefix) - 1u;
  size_t key_len;
  size_t len;
  size_t i;
  char *text;
  char *cursor;
  if (schema_fingerprint == NULL || semantic_key == NULL) return NULL;
  key_len = strlen(semantic_key);
  if (key_len > SIZE_MAX - prefix_len - DATA_BIND_SCHEMA_FINGERPRINT_SIZE * 2u - 2u)
    return NULL;
  len = prefix_len + DATA_BIND_SCHEMA_FINGERPRINT_SIZE * 2u + 1u + key_len;
  text = (char *)db_dynamic_graph_calloc(len + 1u, 1u);
  if (text == NULL) return NULL;
  cursor = text;
  memcpy(cursor, prefix, prefix_len);
  cursor += prefix_len;
  for (i = 0u; i < DATA_BIND_SCHEMA_FINGERPRINT_SIZE; ++i) {
    *cursor++ = hex[(schema_fingerprint[i] >> 4u) & 0x0fu];
    *cursor++ = hex[schema_fingerprint[i] & 0x0fu];
  }
  *cursor++ = ':';
  memcpy(cursor, semantic_key, key_len + 1u);
  return text;
}

static db_dynamic_type_t *db_dynamic_graph_add(
    db_dynamic_graph_t *graph,
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    const char *semantic_key, cmeta_data_kind kind,
    const cmeta_data_desc *canonical_data) {
  db_dynamic_type_t *type;
  if (graph == NULL || semantic_key == NULL ||
      graph->node_count >= graph->node_capacity)
    return NULL;
  type = &graph->nodes[graph->node_count++];
  type->semantic_key = db_dynamic_graph_strdup(semantic_key);
  if (type->semantic_key == NULL) return NULL;
  type->kind = kind;
  type->canonical_data = canonical_data;
  if (canonical_data != NULL && canonical_data->storage_type != NULL &&
      canonical_data->storage_type->identity != NULL) {
    type->identity = canonical_data->storage_type->identity;
  } else {
    type->owned_stable_id = db_dynamic_stable_id(schema_fingerprint,
                                                 semantic_key);
    if (type->owned_stable_id == NULL) return NULL;
    type->owned_identity.form = CMETA_TYPE_ATOM;
    type->owned_identity.stable_atom_id = type->owned_stable_id;
    type->identity = &type->owned_identity;
  }
  return type;
}

static DataBindStatus db_dynamic_build_named(
    db_dynamic_graph_t *graph, Node *schema_root,
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    const char *type_name, unsigned depth, db_dynamic_type_t **out_type);

static DataBindStatus db_dynamic_build_container(
    db_dynamic_graph_t *graph, Node *schema_root,
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    Node *field, const schema_cmeta_field_type *semantic, unsigned depth,
    db_dynamic_type_t **out_type) {
  const char *constructor = semantic->schema_kind;
  const char *element_name = get_string_val(find_child(field, "inner_type"));
  const char *key_name = get_string_val(find_child(field, "key_type"));
  const char *value_name = get_string_val(find_child(field, "value_type"));
  char *semantic_key;
  db_dynamic_type_t *type;
  DataBindStatus status;
  if (depth > DATA_BIND_SEMANTIC_MAX_DEPTH) return DATA_BIND_ERR_LIMIT;
  if (semantic->kind == CMETA_DATA_MAP) {
    if (key_name == NULL || value_name == NULL) return DATA_BIND_ERR_SCHEMA;
    semantic_key = db_dynamic_join_type("map", key_name, value_name);
  } else {
    if (element_name == NULL) return DATA_BIND_ERR_SCHEMA;
    semantic_key = db_dynamic_join_type(constructor != NULL ? constructor : "list",
                                        element_name, NULL);
  }
  if (semantic_key == NULL) return DATA_BIND_ERR_OOM;
  type = db_dynamic_graph_find(graph, semantic_key);
  if (type == NULL)
    type = db_dynamic_graph_add(graph, schema_fingerprint, semantic_key,
                                semantic->kind, semantic->data);
  free(semantic_key);
  if (type == NULL) return DATA_BIND_ERR_OOM;
  if (type->complete || type->building) {
    *out_type = type;
    return DATA_BIND_OK;
  }
  type->building = 1;
  if (semantic->kind == CMETA_DATA_MAP) {
    db_dynamic_type_t *key_type = NULL;
    db_dynamic_type_t *value_type = NULL;
    status = db_dynamic_build_named(graph, schema_root, schema_fingerprint,
                                    key_name, depth + 1u, &key_type);
    if (status == DATA_BIND_OK)
      status = db_dynamic_build_named(graph, schema_root, schema_fingerprint,
                                      value_name, depth + 1u, &value_type);
    if (status != DATA_BIND_OK) return status;
    type->key_type = key_type;
    type->value_type = value_type;
  } else {
    db_dynamic_type_t *element_type = NULL;
    status = db_dynamic_build_named(graph, schema_root, schema_fingerprint,
                                    element_name, depth + 1u, &element_type);
    if (status != DATA_BIND_OK) return status;
    type->element_type = element_type;
  }
  type->building = 0;
  type->complete = 1;
  *out_type = type;
  return DATA_BIND_OK;
}

static DataBindStatus db_dynamic_build_named(
    db_dynamic_graph_t *graph, Node *schema_root,
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    const char *type_name, unsigned depth, db_dynamic_type_t **out_type) {
  Node *record;
  Node *fields;
  const cmeta_data_desc *canonical;
  cmeta_data_kind kind;
  db_dynamic_type_t *type;
  size_t i;
  if (out_type != NULL) *out_type = NULL;
  if (graph == NULL || schema_root == NULL || type_name == NULL ||
      out_type == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (depth > DATA_BIND_SEMANTIC_MAX_DEPTH) return DATA_BIND_ERR_LIMIT;
  type = db_dynamic_graph_find(graph, type_name);
  if (type != NULL) {
    *out_type = type;
    return DATA_BIND_OK;
  }
  canonical = schema_cmeta_builtin_data(type_name);
  record = find_data_record(schema_root, type_name);
  if (record != NULL) kind = CMETA_DATA_STRUCT;
  else if (find_enum_record(schema_root, type_name) != NULL) kind = CMETA_DATA_ENUM;
  else if (find_union_record(schema_root, type_name) != NULL) kind = CMETA_DATA_VARIANT;
  else if (!schema_cmeta_data_kind(type_name, &kind)) return DATA_BIND_ERR_SCHEMA;
  type = db_dynamic_graph_add(graph, schema_fingerprint, type_name, kind,
                              canonical);
  if (type == NULL) return DATA_BIND_ERR_OOM;
  type->building = 1;
  if (kind == CMETA_DATA_STRUCT || kind == CMETA_DATA_VARIANT) {
    if (record == NULL) record = find_union_record(schema_root, type_name);
    fields = fields_node_for_record(record);
    if (fields == NULL) return DATA_BIND_ERR_SCHEMA;
    if (fields->data.list.count != 0u) {
      type->fields = (db_dynamic_field_type_t *)db_dynamic_graph_calloc(
          fields->data.list.count, sizeof(*type->fields));
      if (type->fields == NULL) return DATA_BIND_ERR_OOM;
    }
    type->field_count = fields->data.list.count;
    for (i = 0u; i < type->field_count; ++i) {
      Node *field = fields->data.list.items[i];
      const char *name = get_string_val(find_child(field, "name"));
      const char *field_type = get_string_val(find_child(field, "type"));
      schema_cmeta_field_type semantic;
      db_dynamic_type_t *child_type = NULL;
      DataBindStatus status;
      if (name == NULL || !schema_cmeta_field_resolve(schema_root, field,
                                                       &semantic))
        return DATA_BIND_ERR_SCHEMA;
      type->fields[i].stable_name = db_dynamic_graph_strdup(name);
      if (type->fields[i].stable_name == NULL) return DATA_BIND_ERR_OOM;
      if (cmeta_data_kind_is_container(semantic.kind)) {
        status = db_dynamic_build_container(graph, schema_root,
                                            schema_fingerprint, field,
                                            &semantic, depth + 1u,
                                            &child_type);
      } else {
        if (field_flag(field, "is_group_field"))
          field_type = get_string_val(find_child(field, "group_type"));
        status = db_dynamic_build_named(graph, schema_root, schema_fingerprint,
                                        field_type, depth + 1u, &child_type);
      }
      if (status != DATA_BIND_OK) return status;
      type->fields[i].value_type = child_type;
    }
  }
  type->building = 0;
  type->complete = 1;
  *out_type = type;
  return DATA_BIND_OK;
}

static DataBindStatus db_dynamic_build_synthetic_sequence(
    db_dynamic_graph_t *graph,
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    const char *root_type, db_dynamic_type_t *element_type,
    db_dynamic_type_t **out_type) {
  char *key = db_dynamic_join_type("result", root_type, NULL);
  db_dynamic_type_t *type;
  if (key == NULL) return DATA_BIND_ERR_OOM;
  type = db_dynamic_graph_find(graph, key);
  if (type == NULL)
    type = db_dynamic_graph_add(graph, schema_fingerprint, key,
                                CMETA_DATA_SEQUENCE, &cmeta_data_sequence);
  free(key);
  if (type == NULL) return DATA_BIND_ERR_OOM;
  type->element_type = element_type;
  type->complete = 1;
  *out_type = type;
  return DATA_BIND_OK;
}

static const db_dynamic_type_t *db_dynamic_field_type_find(
    const db_dynamic_type_t *type, const char *name) {
  size_t i;
  if (type == NULL || name == NULL) return NULL;
  for (i = 0u; i < type->field_count; ++i)
    if (type->fields[i].stable_name != NULL &&
        strcmp(type->fields[i].stable_name, name) == 0)
      return type->fields[i].value_type;
  return NULL;
}

static int db_dynamic_type_matches_value(const db_dynamic_type_t *type,
                                         const DataBindValue *value) {
  if (type == NULL || value == NULL || type->identity == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_NULL) return 1;
  switch (type->kind) {
  case CMETA_DATA_STRUCT:
  case CMETA_DATA_VARIANT:
    return value->kind == DATA_BIND_VALUE_OBJECT;
  case CMETA_DATA_SEQUENCE:
    return value->kind == DATA_BIND_VALUE_LIST;
  case CMETA_DATA_SET:
    return value->kind == DATA_BIND_VALUE_SET;
  case CMETA_DATA_MAP:
    return value->kind == DATA_BIND_VALUE_MAP;
  default:
    return value->kind != DATA_BIND_VALUE_OBJECT &&
           value->kind != DATA_BIND_VALUE_LIST &&
           value->kind != DATA_BIND_VALUE_SET &&
           value->kind != DATA_BIND_VALUE_MAP;
  }
}

static DataBindStatus db_dynamic_assign_value(DataBindValue *value,
                                              const db_dynamic_type_t *type,
                                              unsigned depth) {
  size_t i;
  if (value == NULL || type == NULL) return DATA_BIND_ERR_SCHEMA;
  if (depth > DATA_BIND_SEMANTIC_MAX_DEPTH) return DATA_BIND_ERR_LIMIT;
  if (!db_dynamic_type_matches_value(type, value)) return DATA_BIND_ERR_SCHEMA;
  value->type_identity = type->identity;
  if (value->kind == DATA_BIND_VALUE_OBJECT) {
    for (i = 0u; i < vec_size(&value->data.object.fields); ++i) {
      db_field_slot_t *field =
          (db_field_slot_t *)vec_at(&value->data.object.fields, i);
      const db_dynamic_type_t *field_type = field != NULL
                                                ? db_dynamic_field_type_find(type,
                                                                             field->name)
                                                : NULL;
      DataBindStatus status = field != NULL
                                  ? db_dynamic_assign_value(field->value, field_type,
                                                            depth + 1u)
                                  : DATA_BIND_ERR_SCHEMA;
      if (status != DATA_BIND_OK) return status;
    }
  } else if (value->kind == DATA_BIND_VALUE_LIST ||
             value->kind == DATA_BIND_VALUE_SET) {
    vec_t *values = value->kind == DATA_BIND_VALUE_LIST
                        ? &value->data.sequence.values
                        : &value->data.set.ordered_values;
    for (i = 0u; i < vec_size(values); ++i) {
      db_owned_value_slot_t *slot = (db_owned_value_slot_t *)vec_at(values, i);
      DataBindStatus status = slot != NULL
                                  ? db_dynamic_assign_value(slot->value,
                                                            type->element_type,
                                                            depth + 1u)
                                  : DATA_BIND_ERR_SCHEMA;
      if (status != DATA_BIND_OK) return status;
    }
  } else if (value->kind == DATA_BIND_VALUE_MAP) {
    for (i = 0u; i < vec_size(&value->data.map.ordered_entries); ++i) {
      db_map_entry_slot_t *entry = (db_map_entry_slot_t *)vec_at(
          &value->data.map.ordered_entries, i);
      DataBindStatus status;
      if (entry == NULL) return DATA_BIND_ERR_SCHEMA;
      status = db_dynamic_assign_value(entry->key_value, type->key_type,
                                       depth + 1u);
      if (status == DATA_BIND_OK)
        status = db_dynamic_assign_value(entry->value, type->value_type,
                                         depth + 1u);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_dynamic_assign_sequence_item(
    DataBindValue *sequence, DataBindValue *item) {
  db_dynamic_graph_t *graph;
  size_t i;
  if (sequence == NULL || item == NULL || sequence->owned_graph == NULL ||
      sequence->type_identity == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  graph = sequence->owned_graph;
  for (i = 0u; i < graph->node_count; ++i) {
    const db_dynamic_type_t *type = &graph->nodes[i];
    if (type->identity == sequence->type_identity && type->element_type != NULL)
      return db_dynamic_assign_value(item, type->element_type, 0u);
  }
  return DATA_BIND_ERR_SCHEMA;
}

static DataBindStatus db_dynamic_rehome_sequence_item(
    DataBindValue *sequence, DataBindValue *item) {
  db_dynamic_graph_t *previous_graph;
  DataBindStatus status;
  if (sequence == NULL || item == NULL) return DATA_BIND_ERR_INVALID_ARG;
  previous_graph = item->owned_graph;
  item->owned_graph = NULL;
  status = db_dynamic_assign_sequence_item(sequence, item);
  db_dynamic_graph_release(previous_graph);
  return status;
}

static DataBindStatus db_dynamic_attach_root(DataBind *codec,
                                             const char *root_type,
                                             int synthetic_sequence,
                                             DataBindValue *value) {
  db_dynamic_graph_t *graph;
  db_dynamic_type_t *semantic_root = NULL;
  db_dynamic_type_t *value_root = NULL;
  size_t capacity = 0u;
  DataBindStatus status;
  if (codec == NULL || codec->schema_root == NULL || root_type == NULL ||
      value == NULL || value->owned_graph != NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (!db_dynamic_schema_node_count(codec->schema_root, 0u, &capacity) ||
      capacity == SIZE_MAX)
    return DATA_BIND_ERR_LIMIT;
  ++capacity;
  graph = (db_dynamic_graph_t *)db_dynamic_graph_calloc(1u, sizeof(*graph));
  if (graph == NULL) return DATA_BIND_ERR_OOM;
  graph->nodes = (db_dynamic_type_t *)db_dynamic_graph_calloc(
      capacity, sizeof(*graph->nodes));
  if (graph->nodes == NULL) {
    free(graph);
    return DATA_BIND_ERR_OOM;
  }
  graph->references = 1u;
  graph->node_capacity = capacity;
  status = db_dynamic_build_named(graph, codec->schema_root,
                                  codec->schema_fingerprint, root_type, 0u,
                                  &semantic_root);
  value_root = semantic_root;
  if (status == DATA_BIND_OK && synthetic_sequence)
    status = db_dynamic_build_synthetic_sequence(
        graph, codec->schema_fingerprint, root_type, semantic_root, &value_root);
  if (status == DATA_BIND_OK) {
    value->owned_graph = graph;
    status = db_dynamic_assign_value(value, value_root, 0u);
  }
  if (status != DATA_BIND_OK) {
    if (value->owned_graph == graph) value->owned_graph = NULL;
    db_dynamic_graph_release(graph);
    return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_dynamic_publish_result(
    DataBind *codec, const char *root_type, int synthetic_sequence,
    DataBindValue *value, DataBindValue **out_value, DataBindError *error,
    const char *path) {
  DataBindStatus status;
  if (out_value == NULL) {
    data_bind_value_free(value);
    return DATA_BIND_ERR_INVALID_ARG;
  }
  status = db_dynamic_attach_root(codec, root_type, synthetic_sequence, value);
  if (status != DATA_BIND_OK) {
    data_bind_value_free(value);
    *out_value = NULL;
    return db_error_set(error, status, path, -1, -1,
                        "Failed to build dynamic CMeta identity graph for type: %s",
                        root_type != NULL ? root_type : "");
  }
  *out_value = value;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_internal_test_set_dynamic_graph_allocation_failure(
    size_t successful_allocations) {
  atomic_store_explicit(&g_dynamic_graph_fail_after, successful_allocations,
                        memory_order_relaxed);
  return DATA_BIND_OK;
}

static size_t db_reflect_out_size(size_t requested, size_t full_size) {
  return requested != 0 && requested < full_size ? requested : full_size;
}

static int db_reflect_has_field(size_t out_size, size_t offset, size_t field_size) {
  return offset <= out_size && field_size <= out_size - offset;
}

static void db_reflect_clear(void *out, size_t requested, size_t full_size) {
  size_t out_size;
  if (out == NULL) return;
  out_size = db_reflect_out_size(requested, full_size);
  memset(out, 0, out_size);
  if (out_size >= sizeof(size_t)) *(size_t *)out = out_size;
}

#define DB_REFLECT_SET(type, out, out_size, field, value)                                          \
  do {                                                                                             \
    if (db_reflect_has_field((out_size), offsetof(type, field), sizeof((out)->field)))             \
      (out)->field = (value);                                                                      \
  } while (0)

static int fill_schema_type(Node *record, const char *list_name, DataBindSchemaType *out) {
  Node *fields;
  Node *items;
  size_t out_size;
  const char *name;
  if (record == NULL || out == NULL) return 0;
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  name = get_string_val(find_child(record, "name"));
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, name, name);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, kind, schema_record_kind(list_name, record));
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, underlying_type,
                 get_string_val(find_child(record, "underlying_type")));
  fields = fields_node_for_record(record);
  items = items_node_for_enum(record);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, field_count,
                 fields != NULL ? fields->data.list.count : 0);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, item_count,
                 items != NULL ? items->data.list.count : 0);
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaType, fixed_block_size),
                           sizeof(out->fixed_block_size)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaType, has_fixed_block_size),
                           sizeof(out->has_fixed_block_size))) {
    out->has_fixed_block_size = parse_size_value(
        get_string_val(find_child(record, "fixed_block_size")), &out->fixed_block_size);
  }
  return name != NULL;
}

static int fill_schema_field(Node *schema_root, Node *field, DataBindSchemaField *out) {
  size_t out_size;
  const char *name;
  schema_cmeta_field_type semantic;
  int resolved;
  if (schema_root == NULL || field == NULL || out == NULL) return 0;
  resolved = schema_cmeta_field_resolve(schema_root, field, &semantic);
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  name = get_string_val(find_child(field, "name"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, name, name);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, type,
                 get_string_val(find_child(field, "type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, kind,
                 resolved ? semantic.schema_kind : "unknown");
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, inner_type,
                 get_string_val(find_child(field, "inner_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, group_type,
                 get_string_val(find_child(field, "group_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, key_type,
                 get_string_val(find_child(field, "key_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, value_type,
                 get_string_val(find_child(field, "value_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, collection_kind,
                 get_string_val(find_child(field, "collection_kind")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, length,
                 get_string_val(find_child(field, "length_field")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_optional, field_flag(field, "is_optional"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, has_default, field_flag(field, "has_default"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, default_value,
                 get_string_val(find_child(field, "default_value")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_collection,
                 resolved && cmeta_data_kind_is_container(semantic.kind) &&
                 strcmp(semantic.schema_kind, "group") != 0);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_composite,
                 resolved && semantic.kind == CMETA_DATA_STRUCT &&
                 strcmp(semantic.schema_kind, "composite") == 0);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_group,
                 resolved && semantic.kind == CMETA_DATA_SEQUENCE &&
                 strcmp(semantic.schema_kind, "group") == 0);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_map,
                 resolved && semantic.kind == CMETA_DATA_MAP);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_enum,
                 resolved && semantic.kind == CMETA_DATA_ENUM);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_variable_size,
                 field_flag(field, "is_variable_size"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_fixed_size,
                 field_flag(field, "is_fixed_size"));
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, offset), sizeof(out->offset)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaField, has_offset),
                           sizeof(out->has_offset))) {
    out->has_offset = parse_size_value(get_string_val(find_child(field, "offset")), &out->offset);
  }
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, size_bytes),
                           sizeof(out->size_bytes)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaField, has_size_bytes),
                           sizeof(out->has_size_bytes))) {
    out->has_size_bytes =
        parse_size_value(get_string_val(find_child(field, "size_bytes")), &out->size_bytes);
  }
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, field_size_bytes),
                           sizeof(out->field_size_bytes)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaField, has_field_size_bytes),
                           sizeof(out->has_field_size_bytes))) {
    out->has_field_size_bytes = parse_size_value(
        get_string_val(find_child(field, "field_size_bytes")), &out->field_size_bytes);
  }
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, format, field_format(field));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, has_cmeta_kind, resolved);
  if (resolved) {
    DB_REFLECT_SET(DataBindSchemaField, out, out_size, cmeta_kind, semantic.kind);
    DB_REFLECT_SET(DataBindSchemaField, out, out_size, cmeta_data, semantic.data);
  }
  return name != NULL;
}

static int emit_field_array_push(emit_field_array_t *fields, emit_field_t field) {
  emit_field_t *new_items;
  size_t new_capacity;
  if (fields->count == fields->capacity) {
    new_capacity = fields->capacity == 0 ? 8 : fields->capacity * 2;
    new_items = (emit_field_t *)realloc(fields->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) return 0;
    fields->items = new_items;
    fields->capacity = new_capacity;
  }
  fields->items[fields->count++] = field;
  return 1;
}

static void emit_field_array_free(emit_field_array_t *fields) {
  size_t i;
  if (fields == NULL) return;
  for (i = 0; i < fields->count; i++) {
    free(fields->items[i].name);
    emit_field_array_free(&fields->items[i].children);
  }
  free(fields->items);
  fields->items = NULL;
  fields->count = 0;
  fields->capacity = 0;
}

static int append_emit_field(emit_field_array_t *fields, const char *name, emit_kind_t kind,
                             const type_meta_t *meta, int size, int has_set_bytes,
                             size_t fixed_count, int group_dim, emit_field_array_t *children) {
  emit_field_t field;
  size_t name_len = strlen(name);
  memset(&field, 0, sizeof(field));
  field.name = (char *)malloc(name_len + 1);
  if (field.name == NULL) return 0;
  memcpy(field.name, name, name_len + 1);
  field.kind = kind;
  field.size = size;
  field.wire_type = meta != NULL ? meta->wire_type : DB_WIRE_UNDEFINED;
  field.is_float = meta != NULL ? meta->is_float : 0;
  field.is_64 = meta != NULL ? meta->is_64 : 0;
  field.has_set_bytes = (unsigned char)has_set_bytes;
  field.fixed_count = fixed_count;
  field.group_dim = group_dim;
  if (children != NULL) {
    field.children = *children;
    memset(children, 0, sizeof(*children));
  }
  if (!emit_field_array_push(fields, field)) {
    free(field.name);
    emit_field_array_free(&field.children);
    return 0;
  }
  return 1;
}

static int build_fields(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                         const char *prefix, int has_set_bytes);
static int build_record_fields_v1(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                                  const char *prefix, int has_set_bytes);

/* Schema validation limits to prevent malicious schemas */
#define MAX_FIELD_NESTING_DEPTH 32
#define MAX_FIELD_OFFSET_BYTES (1024 * 1024 * 1024) /* 1GB */
#define MAX_TOTAL_FIELDS 10000

typedef struct schema_validation_context {
  int nesting_depth;
  size_t total_fields;
  size_t max_offset;
  char visited_types[256][128]; /* Track visited types to detect cycles */
  size_t visited_count;
  char error[256];
} schema_validation_context_t;

static int schema_validation_type_visited(schema_validation_context_t *ctx, const char *type_name) {
  size_t i;
  if (type_name == NULL) return 0;
  for (i = 0; i < ctx->visited_count; i++) {
    if (strcmp(ctx->visited_types[i], type_name) == 0) return 1;
  }
  return 0;
}

static int schema_validation_mark_visited(schema_validation_context_t *ctx, const char *type_name) {
  if (type_name == NULL) return 0;
  if (ctx->visited_count >= 256) {
    snprintf(ctx->error, sizeof(ctx->error), "Too many nested types (max 256)");
    return 0;
  }
  snprintf(ctx->visited_types[ctx->visited_count], 128, "%s", type_name);
  ctx->visited_count++;
  return 1;
}

static void schema_validation_unmark_visited(schema_validation_context_t *ctx) {
  if (ctx->visited_count > 0) ctx->visited_count--;
}

static int validate_field_offset_safe(schema_validation_context_t *ctx, size_t offset,
                                      size_t size) {
  if (offset > MAX_FIELD_OFFSET_BYTES) {
    snprintf(ctx->error, sizeof(ctx->error), "Field offset %zu exceeds maximum %d bytes", offset,
             MAX_FIELD_OFFSET_BYTES);
    return 0;
  }
  if (size > 0 && offset + size > MAX_FIELD_OFFSET_BYTES) {
    snprintf(ctx->error, sizeof(ctx->error), "Field range [%zu, %zu) exceeds maximum", offset,
             offset + size);
    return 0;
  }
  if (offset + size > ctx->max_offset) {
    ctx->max_offset = offset + size;
  }
  return 1;
}

static int validate_schema_fields(schema_validation_context_t *ctx, Node *src_fields,
                                  Node *schema_root, const char *parent_type);

static int validate_composite_or_group_type(schema_validation_context_t *ctx, Node *schema_root,
                                            const char *type_name, const char *list_name) {
  Node *record;
  int saved_depth;
  int result;

  if (type_name == NULL) return 1;

  /* Check for circular reference */
  if (schema_validation_type_visited(ctx, type_name)) {
    snprintf(ctx->error, sizeof(ctx->error), "Circular type reference detected: %s", type_name);
    return 0;
  }

  record = find_named_record(schema_root, list_name, type_name);
  if (record == NULL) return 1; /* Not found is not a validation error */

  /* Check nesting depth */
  if (ctx->nesting_depth >= MAX_FIELD_NESTING_DEPTH) {
    snprintf(ctx->error, sizeof(ctx->error), "Field nesting depth exceeds maximum %d (in type %s)",
             MAX_FIELD_NESTING_DEPTH, type_name);
    return 0;
  }

  /* Mark as visited and recurse */
  if (!schema_validation_mark_visited(ctx, type_name)) return 0;

  saved_depth = ctx->nesting_depth;
  ctx->nesting_depth++;
  result = validate_schema_fields(ctx, find_child(record, "fields"), schema_root, type_name);
  ctx->nesting_depth = saved_depth;

  schema_validation_unmark_visited(ctx);
  return result;
}

static int validate_schema_fields(schema_validation_context_t *ctx, Node *src_fields,
                                  Node *schema_root, const char *parent_type) {
  size_t i;
  if (src_fields == NULL || src_fields->type != NODE_LIST) return 1;

  for (i = 0; i < src_fields->data.list.count; i++) {
    Node *field = src_fields->data.list.items[i];
    const char *field_name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    Node *offset_node = find_child(field, "offset");
    Node *size_node = find_child(field, "size");

    if (field_name == NULL) continue;

    /* Count total fields */
    ctx->total_fields++;
    if (ctx->total_fields > MAX_TOTAL_FIELDS) {
      snprintf(ctx->error, sizeof(ctx->error), "Total field count exceeds maximum %d",
               MAX_TOTAL_FIELDS);
      return 0;
    }

    /* Validate offset if present */
    if (offset_node != NULL && offset_node->type == NODE_STRING) {
      const char *offset_str = get_string_val(offset_node);
      const char *size_str = size_node != NULL ? get_string_val(size_node) : NULL;
      if (offset_str != NULL) {
        int offset_int = parse_positive_int(offset_str);
        int size_int = size_str != NULL ? parse_positive_int(size_str) : 0;
        if (offset_int >= 0 && size_int >= 0) {
          if (!validate_field_offset_safe(ctx, (size_t)offset_int, (size_t)size_int)) return 0;
        }
      }
    }

    /* Validate composite references */
    if (field_flag(field, "is_composite_ref")) {
      if (!validate_composite_or_group_type(ctx, schema_root, field_type, "composites")) return 0;
      continue;
    }

    /* Validate group references */
    if (field_flag(field, "is_group_field")) {
      const char *group_type = get_string_val(find_child(field, "group_type"));
      if (!validate_composite_or_group_type(ctx, schema_root, group_type, "groups")) return 0;
      continue;
    }

    /* Validate collection inner types */
    if (field_flag(field, "is_collection")) {
      const char *inner_type = get_string_val(find_child(field, "inner_type"));
      if (inner_type != NULL) {
        if (!validate_composite_or_group_type(ctx, schema_root, inner_type, "composites")) return 0;
      }
    }
  }
  return 1;
}

static void build_full_field_name(const char *prefix, const char *field_name, char *out,
                                  size_t out_size) {
  if (prefix != NULL && prefix[0] != '\0') snprintf(out, out_size, "%s.%s", prefix, field_name);
  else snprintf(out, out_size, "%s", field_name);
}

static int build_composite_emit_fields(emit_field_array_t *fields, Node *field, Node *schema_root,
                                        const char *full_name, int has_set_bytes,
                                        int record_mode) {
  const char *field_type = get_string_val(find_child(field, "type"));
  Node *composite = find_named_record(schema_root, "composites", field_type);
  if (composite == NULL) return 1;
  if (record_mode) {
    emit_field_array_t child_fields = {0};
    if (!build_record_fields_v1(&child_fields, find_child(composite, "fields"), schema_root, NULL,
                                has_set_bytes)) {
      emit_field_array_free(&child_fields);
      return 0;
    }
    if (!append_emit_field(fields, full_name, EF_OBJECT, NULL, 0, 0, 0, 0, &child_fields)) {
      emit_field_array_free(&child_fields);
      return 0;
    }
    return 1;
  }
  return build_fields(fields, find_child(composite, "fields"), schema_root, full_name,
                      has_set_bytes);
}

static int build_group_emit_field(emit_field_array_t *fields, Node *field, Node *schema_root,
                                   const char *full_name, int has_set_bytes, int record_mode) {
  emit_field_array_t child_fields = {0};
  Node *group =
      find_named_record(schema_root, "groups", get_string_val(find_child(field, "group_type")));
  int entry_size =
      group != NULL ? parse_positive_int(get_string_val(find_child(group, "fixed_block_size"))) : 0;
  int group_dim = parse_positive_int(get_string_val(find_child(field, "group_dimension_size")));

  if (group == NULL || entry_size <= 0) return 1;
  if (group_dim <= 0) group_dim = 4;

  if (!(record_mode ? build_record_fields_v1(&child_fields, find_child(group, "fields"),
                                             schema_root, NULL, has_set_bytes)
                    : build_fields(&child_fields, find_child(group, "fields"), schema_root, NULL,
                                   has_set_bytes))) {
    emit_field_array_free(&child_fields);
    return 0;
  }
  if (!append_emit_field(fields, full_name, EF_GROUP, NULL, entry_size, 0, 0, group_dim,
                         &child_fields)) {
    emit_field_array_free(&child_fields);
    return 0;
  }
  return 1;
}

static int build_map_collection_emit_field(emit_field_array_t *fields, Node *field,
                                           Node *schema_root, const char *full_name) {
  const char *key_type = get_string_val(find_child(field, "key_type"));
  const char *value_type = get_string_val(find_child(field, "value_type"));
  const type_meta_t *meta = NULL;

  if (key_type == NULL || value_type == NULL) return 1;
  if (strcmp(key_type, "string") != 0) return 1;
  if (strcmp(value_type, "string") == 0)
    return append_emit_field(fields, full_name, EF_MAP_STR_STR, NULL, 0, 0, 0, 0, NULL);
  if (strcmp(value_type, "bool") == 0)
    return append_emit_field(fields, full_name, EF_MAP_STR_BOOL, find_type_meta("bool"), 1, 0, 0, 0,
                             NULL);

  meta = find_scalar_meta(schema_root, value_type);
  if (meta == NULL) return 1;
  if (meta->is_float)
    return append_emit_field(fields, full_name, EF_MAP_STR_DBL, meta, meta->size, 0, 0, 0, NULL);
  return append_emit_field(
      fields, full_name,
      meta->wire_type == DB_WIRE_U64
          ? EF_MAP_STR_U64
          : (meta->wire_type == DB_WIRE_U32 ? EF_MAP_STR_U32
                                         : (meta->is_64 ? EF_MAP_STR_I64 : EF_MAP_STR_INT)),
      meta, meta->size, 0, 0, 0, NULL);
}

static int build_list_or_set_collection_emit_field(emit_field_array_t *fields, Node *field,
                                                    Node *schema_root, const char *full_name,
                                                    const char *collection_kind,
                                                    const char *inner_type, int count,
                                                    int record_mode) {
  const type_meta_t *meta = NULL;

  if (strcmp(collection_kind, "list") != 0 && strcmp(collection_kind, "set") != 0 &&
      strcmp(collection_kind, "array") != 0)
    return 1;
  if (inner_type == NULL) return 1;
  if (field_flag(field, "is_fixed_size") && count <= 0) return 1;

  if (strcmp(collection_kind, "set") != 0) {
    Node *composite = find_named_record(schema_root, "composites", inner_type);
    if (composite != NULL) {
      emit_field_array_t child_fields = {0};
      int element_size =
          parse_positive_int(get_string_val(find_child(composite, "fixed_block_size")));
      if (element_size <= 0) return 1;
      if (!(record_mode ? build_record_fields_v1(&child_fields, find_child(composite, "fields"),
                                                 schema_root, NULL, 0)
                        : build_fields(&child_fields, find_child(composite, "fields"), schema_root,
                                       NULL, 0))) {
        emit_field_array_free(&child_fields);
        return 0;
      }
      if (!append_emit_field(fields, full_name, EF_LIST_OBJ, NULL, element_size, 0,
                             field_flag(field, "is_fixed_size") ? (size_t)count : 0, 0,
                             &child_fields)) {
        emit_field_array_free(&child_fields);
        return 0;
      }
      return 1;
    }
  }

  if (strcmp(inner_type, "string") == 0) {
    emit_kind_t kind = strcmp(collection_kind, "set") == 0 ? EF_SET_STR : EF_LIST_STR;
    return append_emit_field(fields, full_name, kind, NULL, 0, 0,
                             field_flag(field, "is_fixed_size") ? (size_t)count : 0, 0, NULL);
  }

  meta = find_scalar_meta(schema_root, inner_type);
  if (meta == NULL) return 1;
  if (strcmp(collection_kind, "set") == 0) {
    emit_kind_t kind;
    if (meta->is_float) kind = EF_SET_DBL;
    else if (strcmp(inner_type, "bool") == 0) kind = EF_SET_BOOL;
    else if (meta->wire_type == DB_WIRE_U64) kind = EF_SET_U64;
    else if (meta->wire_type == DB_WIRE_U32) kind = EF_SET_U32;
    else if (meta->is_64) kind = EF_SET_I64;
    else kind = EF_SET_INT;
    return append_emit_field(fields, full_name, kind, meta, meta->size, 0, 0, 0, NULL);
  }

  return append_emit_field(
      fields, full_name,
      strcmp(inner_type, "bool") == 0
          ? EF_LIST_BOOL
          : (meta->is_float
                 ? EF_LIST_DBL
                 : (meta->wire_type == DB_WIRE_U64 ? EF_LIST_U64
                                                   : (meta->wire_type == DB_WIRE_U32
                                                       ? EF_LIST_U32
                                                       : (meta->is_64 ? EF_LIST_I64
                                                                      : EF_LIST_INT)))),
      meta, meta->size, 0, field_flag(field, "is_fixed_size") ? (size_t)count : 0, 0, NULL);
}

static int build_collection_emit_field(emit_field_array_t *fields, Node *field, Node *schema_root,
                                        const char *full_name, int record_mode) {
  const char *collection_kind = get_string_val(find_child(field, "collection_kind"));
  const char *field_type = get_string_val(find_child(field, "type"));
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  int count = parse_positive_int(get_string_val(find_child(field, "length_field")));

  if (collection_kind == NULL) collection_kind = field_type;
  if (strcmp(collection_kind, "map") == 0)
    return build_map_collection_emit_field(fields, field, schema_root, full_name);

  return build_list_or_set_collection_emit_field(fields, field, schema_root, full_name,
                                                  collection_kind, inner_type, count, record_mode);
}

static int build_var_or_scalar_emit_field(emit_field_array_t *fields, Node *field,
                                          Node *schema_root, const char *full_name,
                                          const char *field_type, int has_set_bytes) {
  if (field_flag(field, "is_var_data")) {
    emit_kind_t kind = field_flag(field, "is_bytes") ? EF_VAR_BYTES : EF_STR;
    return append_emit_field(fields, full_name, kind, NULL, 0, has_set_bytes, 0, 0, NULL);
  }

  if (field_flag(field, "is_bytes")) {
    int size = parse_positive_int(get_string_val(find_child(field, "size_bytes")));
    if (size <= 0) return 1;
    return append_emit_field(fields, full_name, EF_FIX_BYTES, NULL, size, has_set_bytes, 0, 0,
                             NULL);
  }

  if (field_flag(field, "is_uuid") || strcmp(field_type, "uuid") == 0) {
    return append_emit_field(fields, full_name, EF_UUID, NULL, 16, 0, 0, 0, NULL);
  }

  if (field_flag(field, "is_enum_ref")) {
    const type_meta_t *meta = find_enum_meta(schema_root, field_type);
    if (meta == NULL) return 1;
    return append_emit_field(fields, full_name,
                             meta->wire_type == DB_WIRE_U64 ? EF_U64
                                                           : (meta->wire_type == DB_WIRE_U32
                                                               ? EF_U32
                                                               : (meta->is_64 ? EF_I64 : EF_INT)),
                             meta, meta->size, 0, 0, 0, NULL);
  }

  {
    const type_meta_t *meta = find_type_meta(field_type);
    if (meta == NULL) return 1;
    return append_emit_field(fields, full_name,
                             strcmp(field_type, "bool") == 0
                                 ? EF_BOOL
                                 : (meta->is_float
                                        ? EF_DBL
                                        : (meta->wire_type == DB_WIRE_U64
                                               ? EF_U64
                                               : (meta->wire_type == DB_WIRE_U32
                                                      ? EF_U32
                                                      : (meta->is_64 ? EF_I64 : EF_INT)))),
                             meta, meta->size, 0, 0, 0, NULL);
  }
}

static int build_fields_mode_v1(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                                const char *prefix, int has_set_bytes, int record_mode) {
  size_t i;
  if (src_fields == NULL || src_fields->type != NODE_LIST) return 1;
  for (i = 0; i < src_fields->data.list.count; i++) {
    Node *field = src_fields->data.list.items[i];
    const char *field_name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    char full_name[512];
    if (field_name == NULL || field_type == NULL) continue;
    build_full_field_name(prefix, field_name, full_name, sizeof(full_name));

    if (field_flag(field, "is_composite_ref")) {
      if (!build_composite_emit_fields(fields, field, schema_root, full_name, has_set_bytes,
                                       record_mode))
        return 0;
      continue;
    }

    if (field_flag(field, "is_group_field")) {
      if (!build_group_emit_field(fields, field, schema_root, full_name, has_set_bytes, record_mode))
        return 0;
      continue;
    }

    if (field_flag(field, "is_collection")) {
      if (!build_collection_emit_field(fields, field, schema_root, full_name, record_mode)) return 0;
      continue;
    }

    if (!build_var_or_scalar_emit_field(fields, field, schema_root, full_name, field_type,
                                        has_set_bytes))
      return 0;
  }
  return 1;
}

static int build_fields(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                        const char *prefix, int has_set_bytes) {
  return build_fields_mode_v1(fields, src_fields, schema_root, prefix, has_set_bytes, 0);
}

static int build_record_fields_v1(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                                  const char *prefix, int has_set_bytes) {
  return build_fields_mode_v1(fields, src_fields, schema_root, prefix, has_set_bytes, 1);
}

typedef struct data_bind_binary_writer {
  uint8_t *data;
  size_t capacity;
  size_t offset;
  DataBindError *error;
} data_bind_binary_writer_t;

static const DataBindValue *db_binary_object_get_n(const DataBindValue *object, const char *name,
                                                   size_t name_len) {
  size_t i;
  if (object == NULL || object->kind != DATA_BIND_VALUE_OBJECT || name == NULL) return NULL;
  for (i = 0; i < vec_size(&object->data.object.fields); ++i) {
    const db_field_slot_t *field =
        (const db_field_slot_t *)vec_at_const(&object->data.object.fields, i);
    if (field == NULL || field->name == NULL) return NULL;
    if (strlen(field->name) == name_len && memcmp(field->name, name, name_len) == 0)
      return field->value;
  }
  return NULL;
}

static const DataBindValue *db_binary_value_at_path(const DataBindValue *object, const char *path) {
  const DataBindValue *value;
  const char *segment;
  const char *dot;
  if (object == NULL || path == NULL) return NULL;
  value = db_binary_object_get_n(object, path, strlen(path));
  if (value != NULL) return value;
  value = object;
  segment = path;
  while (segment[0] != '\0') {
    dot = strchr(segment, '.');
    value = db_binary_object_get_n(value, segment,
                                   dot != NULL ? (size_t)(dot - segment) : strlen(segment));
    if (value == NULL || dot == NULL) return value;
    segment = dot + 1;
  }
  return NULL;
}

static DataBindStatus db_binary_writer_reserve(data_bind_binary_writer_t *writer, size_t size,
                                               const char *path, uint8_t **out) {
  size_t start;
  if (writer == NULL || size > SIZE_MAX - writer->offset)
    return db_error_set(writer != NULL ? writer->error : NULL, DATA_BIND_ERR_RUNTIME, path, -1, -1,
                        "Binary output size overflow");
  start = writer->offset;
  writer->offset += size;
  if (out != NULL) *out = writer->data != NULL ? writer->data + start : NULL;
  if (writer->data != NULL && writer->offset > writer->capacity)
    return db_error_set(writer->error, DATA_BIND_ERR_INVALID_ARG, path, -1, -1,
                        "Binary output buffer is too small");
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_bytes(data_bind_binary_writer_t *writer, const void *data,
                                            size_t size, const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status;
  if (size != 0 && data == NULL)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                        "Binary value has no data");
  status = db_binary_writer_reserve(writer, size, path, &dst);
  if (status == DATA_BIND_OK && dst != NULL && size != 0) memcpy(dst, data, size);
  return status;
}

static DataBindStatus db_binary_write_zeros(data_bind_binary_writer_t *writer, size_t size,
                                            const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status = db_binary_writer_reserve(writer, size, path, &dst);
  if (status == DATA_BIND_OK && dst != NULL && size != 0) memset(dst, 0, size);
  return status;
}

static DataBindStatus db_binary_write_u16(data_bind_binary_writer_t *writer, uint16_t value,
                                          const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status = db_binary_writer_reserve(writer, sizeof(value), path, &dst);
  if (status == DATA_BIND_OK && dst != NULL) tbe_wire_write_u16(dst, 0, value);
  return status;
}

static DataBindStatus db_binary_write_u32(data_bind_binary_writer_t *writer, uint32_t value,
                                          const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status = db_binary_writer_reserve(writer, sizeof(value), path, &dst);
  if (status == DATA_BIND_OK && dst != NULL) tbe_wire_write_u32(dst, 0, value);
  return status;
}

static int db_binary_integer_value(const DataBindValue *value, int64_t *out) {
  if (value == NULL || out == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = value->data.int_val;
    return 1;
  }
  if (value->kind == DATA_BIND_VALUE_INT64) {
    *out = value->data.int64_val;
    return 1;
  }
  return 0;
}

static int db_binary_integer_fits(data_bind_wire_type_t type, int64_t value) {
  switch (type) {
  case DB_WIRE_U8:
    return value >= 0 && (uint64_t)value <= UINT8_MAX;
  case DB_WIRE_I8:
    return value >= INT8_MIN && value <= INT8_MAX;
  case DB_WIRE_U16:
    return value >= 0 && (uint64_t)value <= UINT16_MAX;
  case DB_WIRE_I16:
    return value >= INT16_MIN && value <= INT16_MAX;
  case DB_WIRE_U32:
    return value >= 0 && (uint64_t)value <= UINT32_MAX;
  case DB_WIRE_I32:
    return value >= INT32_MIN && value <= INT32_MAX;
  case DB_WIRE_U64:
    return value >= 0;
  case DB_WIRE_I64:
    return 1;
  default:
    return 0;
  }
}

static DataBindStatus db_binary_write_integer(data_bind_binary_writer_t *writer,
                                              data_bind_wire_type_t type,
                                              int size, const DataBindValue *value,
                                              const char *path) {
  uint8_t *dst = NULL;
  int64_t integer;
  DataBindStatus status;
  if (type == DB_WIRE_U64 && value != NULL && value->kind == DATA_BIND_VALUE_UINT64) {
    status = db_binary_writer_reserve(writer, (size_t)size, path, &dst);
    if (status == DATA_BIND_OK && dst != NULL)
      tbe_wire_write_u64(dst, 0, value->data.uint64_val);
    return status;
  }
  if (!db_binary_integer_value(value, &integer) || !db_binary_integer_fits(type, integer))
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                        "Integer value does not fit the schema wire type");
  status = db_binary_writer_reserve(writer, (size_t)size, path, &dst);
  if (status != DATA_BIND_OK || dst == NULL) return status;
  switch (type) {
  case DB_WIRE_U8:
    tbe_wire_write_u8(dst, 0, (uint8_t)integer);
    break;
  case DB_WIRE_I8:
    tbe_wire_write_i8(dst, 0, (int8_t)integer);
    break;
  case DB_WIRE_U16:
    tbe_wire_write_u16(dst, 0, (uint16_t)integer);
    break;
  case DB_WIRE_I16:
    tbe_wire_write_i16(dst, 0, (int16_t)integer);
    break;
  case DB_WIRE_U32:
    tbe_wire_write_u32(dst, 0, (uint32_t)integer);
    break;
  case DB_WIRE_I32:
    tbe_wire_write_i32(dst, 0, (int32_t)integer);
    break;
  case DB_WIRE_U64:
    tbe_wire_write_u64(dst, 0, (uint64_t)integer);
    break;
  case DB_WIRE_I64:
    tbe_wire_write_i64(dst, 0, integer);
    break;
  default:
    return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, path, -1, -1,
                        "Unsupported integer wire type");
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_number(data_bind_binary_writer_t *writer,
                                             const emit_field_t *field,
                                             const DataBindValue *value) {
  uint8_t *dst = NULL;
  double number;
  DataBindStatus status;
  if (value == NULL || value->kind != DATA_BIND_VALUE_DOUBLE || !isfinite(value->data.double_val))
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Floating-point field requires a finite number");
  number = value->data.double_val;
  if (field->wire_type == DB_WIRE_F32 && (number < -FLT_MAX || number > FLT_MAX))
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Floating-point value does not fit float32");
  status = db_binary_writer_reserve(writer, (size_t)field->size, field->name, &dst);
  if (status != DATA_BIND_OK || dst == NULL) return status;
  if (field->wire_type == DB_WIRE_F32) tbe_wire_write_f32(dst, 0, (float)number);
  else tbe_wire_write_f64(dst, 0, number);
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_var_data(data_bind_binary_writer_t *writer, const void *data,
                                               size_t size, const char *path) {
  DataBindStatus status;
  if (size > UINT32_MAX)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                        "Variable binary value exceeds uint32 length");
  status = db_binary_write_u32(writer, (uint32_t)size, path);
  return status == DATA_BIND_OK ? db_binary_write_bytes(writer, data, size, path) : status;
}

static DataBindStatus db_binary_write_fields(data_bind_binary_writer_t *writer,
                                             const emit_field_array_t *fields,
                                             const DataBindValue *object);

static DataBindStatus db_binary_write_scalar(data_bind_binary_writer_t *writer,
                                             const emit_field_t *field,
                                             const DataBindValue *value) {
  if (value == NULL)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Required binary field is missing");
  switch (field->kind) {
  case EF_INT:
  case EF_U32:
  case EF_I64:
  case EF_U64:
    return db_binary_write_integer(writer, field->wire_type, field->size, value, field->name);
  case EF_BOOL: {
    uint8_t boolean;
    if (value->kind != DATA_BIND_VALUE_BOOL)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Boolean field has the wrong value type");
    boolean = value->data.bool_val != 0;
    return db_binary_write_bytes(writer, &boolean, sizeof(boolean), field->name);
  }
  case EF_DBL:
    return db_binary_write_number(writer, field, value);
  case EF_UUID:
    if (value->kind != DATA_BIND_VALUE_UUID)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "UUID field has the wrong value type");
    return db_binary_write_bytes(writer, value->data.uuid_val.bytes, SALTS_UUID_SIZE, field->name);
  case EF_FIX_BYTES:
    if (value->kind != DATA_BIND_VALUE_BYTES || value->data.bytes_val.len != (size_t)field->size)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Fixed bytes field length does not match the schema");
    return db_binary_write_bytes(writer, value->data.bytes_val.ptr, value->data.bytes_val.len,
                                 field->name);
  case EF_STR:
    if (value->kind != DATA_BIND_VALUE_STRING)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "String field has the wrong value type");
    return db_binary_write_var_data(writer, value->data.string_val.ptr,
                                    value->data.string_val.len, field->name);
  case EF_VAR_BYTES:
    if (value->kind != DATA_BIND_VALUE_BYTES)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Bytes field has the wrong value type");
    return db_binary_write_var_data(writer, value->data.bytes_val.ptr, value->data.bytes_val.len,
                                    field->name);
  default:
    return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                        "Unsupported scalar binary field");
  }
}

static int db_binary_is_list_kind(emit_kind_t kind) {
  return kind >= EF_LIST_INT && kind <= EF_LIST_OBJ;
}

static int db_binary_is_set_kind(emit_kind_t kind) {
  return kind >= EF_SET_INT && kind <= EF_SET_STR;
}

static DataBindStatus db_binary_write_collection_item(data_bind_binary_writer_t *writer,
                                                      const emit_field_t *field,
                                                      const DataBindValue *item) {
  emit_field_t scalar = *field;
  switch (field->kind) {
  case EF_LIST_INT:
  case EF_SET_INT:
    scalar.kind = EF_INT;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_U32:
  case EF_SET_U32:
    scalar.kind = EF_U32;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_I64:
  case EF_SET_I64:
    scalar.kind = EF_I64;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_U64:
  case EF_SET_U64:
    scalar.kind = EF_U64;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_DBL:
  case EF_SET_DBL:
    scalar.kind = EF_DBL;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_BOOL:
  case EF_SET_BOOL:
    scalar.kind = EF_BOOL;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_STR:
  case EF_SET_STR:
    scalar.kind = EF_STR;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_OBJ:
    if (item == NULL || item->kind != DATA_BIND_VALUE_OBJECT)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Composite collection item has the wrong value type");
    return db_binary_write_fields(writer, &field->children, item);
  default:
    return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                        "Unsupported binary collection item");
  }
}

static DataBindStatus db_binary_write_collection(data_bind_binary_writer_t *writer,
                                                 const emit_field_t *field,
                                                 const DataBindValue *value) {
  const vec_t *values;
  size_t i;
  size_t count;
  DataBindStatus status;
  DataBindValueKind expected =
      db_binary_is_set_kind(field->kind) ? DATA_BIND_VALUE_SET : DATA_BIND_VALUE_LIST;
  if (value == NULL || value->kind != expected)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Collection field has the wrong value type");
  values = dbv_ordered_values_const(value);
  count = vec_size(values);
  if (field->fixed_count != 0) {
    if (count != field->fixed_count)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Fixed collection length does not match the schema");
  } else {
    if (count > UINT32_MAX)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Collection exceeds uint32 item count");
    status = db_binary_write_u32(writer, (uint32_t)count, field->name);
    if (status != DATA_BIND_OK) return status;
  }
  for (i = 0; i < count; ++i) {
    const db_owned_value_slot_t *slot =
        (const db_owned_value_slot_t *)vec_at_const(values, i);
    status = db_binary_write_collection_item(writer, field,
                                             slot != NULL ? slot->value : NULL);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_map(data_bind_binary_writer_t *writer,
                                          const emit_field_t *field, const DataBindValue *value) {
  const vec_t *entries;
  size_t i;
  DataBindStatus status;
  emit_field_t scalar = *field;
  if (value == NULL || value->kind != DATA_BIND_VALUE_MAP)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Map field has the wrong value type or too many entries");
  entries = &value->data.map.ordered_entries;
  if (vec_size(entries) > UINT32_MAX)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Map field has the wrong value type or too many entries");
  status = db_binary_write_u32(writer, (uint32_t)vec_size(entries), field->name);
  if (status != DATA_BIND_OK) return status;
  for (i = 0; i < vec_size(entries); ++i) {
    const db_map_entry_slot_t *entry =
        (const db_map_entry_slot_t *)vec_at_const(entries, i);
    if (entry == NULL || entry->public_key_text == NULL || entry->value == NULL)
      return db_error_set(writer->error, DATA_BIND_ERR_RUNTIME, field->name,
                          -1, -1, "Map entry storage is invalid");
    status = db_binary_write_var_data(writer, entry->public_key_text,
                                      strlen(entry->public_key_text), field->name);
    if (status != DATA_BIND_OK) return status;
    if (field->kind == EF_MAP_STR_STR) scalar.kind = EF_STR;
    else if (field->kind == EF_MAP_STR_INT) scalar.kind = EF_INT;
    else if (field->kind == EF_MAP_STR_U32) scalar.kind = EF_U32;
    else if (field->kind == EF_MAP_STR_I64) scalar.kind = EF_I64;
    else if (field->kind == EF_MAP_STR_U64) scalar.kind = EF_U64;
    else if (field->kind == EF_MAP_STR_DBL) scalar.kind = EF_DBL;
    else scalar.kind = EF_BOOL;
    status = db_binary_write_scalar(writer, &scalar, entry->value);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_group(data_bind_binary_writer_t *writer,
                                            const emit_field_t *field, const DataBindValue *value) {
  size_t i;
  size_t count;
  DataBindStatus status;
  count = value != NULL ? vec_size(&value->data.sequence.values) : 0u;
  if (value == NULL || value->kind != DATA_BIND_VALUE_LIST || field->group_dim < 4 ||
      field->size <= 0 || field->size > UINT16_MAX || count > UINT16_MAX)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Group value does not fit the schema dimensions");
  status = db_binary_write_u16(writer, (uint16_t)field->size, field->name);
  if (status == DATA_BIND_OK)
    status = db_binary_write_u16(writer, (uint16_t)count, field->name);
  if (status == DATA_BIND_OK && field->group_dim > 4)
    status = db_binary_write_zeros(writer, (size_t)field->group_dim - 4u, field->name);
  if (status != DATA_BIND_OK) return status;
  for (i = 0; i < count; ++i) {
    const db_owned_value_slot_t *slot =
        (const db_owned_value_slot_t *)vec_at_const(&value->data.sequence.values, i);
    const DataBindValue *entry = slot != NULL ? slot->value : NULL;
    size_t start = writer->offset;
    if (entry == NULL || entry->kind != DATA_BIND_VALUE_OBJECT)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Group entry has the wrong value type");
    status = db_binary_write_fields(writer, &field->children, entry);
    if (status != DATA_BIND_OK) return status;
    if (writer->offset - start > (size_t)field->size)
      return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                          "Group fields exceed the declared block length");
    status =
        db_binary_write_zeros(writer, (size_t)field->size - (writer->offset - start), field->name);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_fields(data_bind_binary_writer_t *writer,
                                             const emit_field_array_t *fields,
                                             const DataBindValue *object) {
  size_t i;
  if (object == NULL || object->kind != DATA_BIND_VALUE_OBJECT)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, "binary", -1, -1,
                        "Binary root value must be an object");
  for (i = 0; i < fields->count; ++i) {
    const emit_field_t *field = &fields->items[i];
    const DataBindValue *value = db_binary_value_at_path(object, field->name);
    DataBindStatus status;
    if (field->kind <= EF_VAR_BYTES) status = db_binary_write_scalar(writer, field, value);
    else if (db_binary_is_list_kind(field->kind) || db_binary_is_set_kind(field->kind))
      status = db_binary_write_collection(writer, field, value);
    else if (field->kind >= EF_MAP_STR_STR && field->kind <= EF_MAP_STR_BOOL)
      status = db_binary_write_map(writer, field, value);
    else if (field->kind == EF_GROUP) status = db_binary_write_group(writer, field, value);
    else
      status = db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                            "Unsupported binary field kind");
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

typedef struct data_bind_binary_reader {
  const uint8_t *data;
  size_t length;
  size_t offset;
  DataBindError *error;
} data_bind_binary_reader_t;

static DataBindStatus db_binary_reader_take(data_bind_binary_reader_t *reader, size_t size,
                                            const char *path, const uint8_t **out) {
  if (out != NULL) *out = NULL;
  if (reader == NULL || size > reader->length - (reader->offset <= reader->length
                                                     ? reader->offset
                                                     : reader->length))
    return db_error_set(reader != NULL ? reader->error : NULL, DATA_BIND_ERR_PARSE, path, -1, -1,
                        "Binary input is truncated");
  if (out != NULL) *out = reader->data + reader->offset;
  reader->offset += size;
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_read_u32(data_bind_binary_reader_t *reader, const char *path,
                                         uint32_t *out) {
  const uint8_t *data;
  DataBindStatus status;
  if (out == NULL)
    return db_error_set(reader != NULL ? reader->error : NULL, DATA_BIND_ERR_INVALID_ARG, path, -1,
                        -1, "Invalid binary integer output");
  status = db_binary_reader_take(reader, sizeof(uint32_t), path, &data);
  if (status == DATA_BIND_OK) *out = tbe_wire_read_u32(data, 0);
  return status;
}

static DataBindStatus db_binary_read_scalar(data_bind_binary_reader_t *reader,
                                            const emit_field_t *field, DataBindValue **out_value) {
  const uint8_t *data = NULL;
  DataBindValue *value = NULL;
  DataBindStatus status;
  uint32_t length;
  if (out_value != NULL) *out_value = NULL;
  if (reader == NULL || field == NULL || out_value == NULL)
    return db_error_set(reader != NULL ? reader->error : NULL, DATA_BIND_ERR_INVALID_ARG, NULL, -1,
                        -1, "Invalid binary scalar arguments");

  if (field->kind == EF_STR || field->kind == EF_VAR_BYTES) {
    status = db_binary_read_u32(reader, field->name, &length);
    if (status != DATA_BIND_OK) return status;
    status = db_binary_reader_take(reader, length, field->name, &data);
    if (status != DATA_BIND_OK) return status;
    if (field->kind == EF_VAR_BYTES)
      value = dbv_bytes(data, length);
    else {
      if (!vstr_utf8_valid(vstr_from_buf((const char *)data, length)))
        return db_error_set(reader->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                            "Binary string field is not valid UTF-8");
      value = dbv_string_n((const char *)data, length);
    }
  } else {
    status = db_binary_reader_take(reader, (size_t)field->size, field->name, &data);
    if (status != DATA_BIND_OK) return status;
    switch (field->kind) {
    case EF_INT:
    case EF_U32:
    case EF_I64:
    case EF_U64:
      switch (field->wire_type) {
      case DB_WIRE_U8: value = dbv_int((int32_t)tbe_wire_read_u8(data, 0)); break;
      case DB_WIRE_I8: value = dbv_int((int32_t)tbe_wire_read_i8(data, 0)); break;
      case DB_WIRE_U16: value = dbv_int((int32_t)tbe_wire_read_u16(data, 0)); break;
      case DB_WIRE_I16: value = dbv_int((int32_t)tbe_wire_read_i16(data, 0)); break;
      case DB_WIRE_U32: value = dbv_uint32_compat(tbe_wire_read_u32(data, 0)); break;
      case DB_WIRE_I32: value = dbv_int(tbe_wire_read_i32(data, 0)); break;
      case DB_WIRE_U64: value = dbv_uint64(tbe_wire_read_u64(data, 0)); break;
      case DB_WIRE_I64: value = dbv_int64(tbe_wire_read_i64(data, 0)); break;
      default:
        return db_error_set(reader->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                            "Unsupported integer wire type");
      }
      break;
    case EF_BOOL: value = dbv_bool(tbe_wire_read_u8(data, 0) != 0); break;
    case EF_DBL:
      value = dbv_double(field->wire_type == DB_WIRE_F32
                             ? (double)tbe_wire_read_f32(data, 0)
                             : tbe_wire_read_f64(data, 0));
      break;
    case EF_UUID: value = dbv_uuid_bytes(data); break;
    case EF_FIX_BYTES: value = dbv_bytes(data, (size_t)field->size); break;
    default:
      return db_error_set(reader->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                          "Unsupported scalar binary field");
    }
  }
  if (value == NULL)
    return db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                        "Out of memory parsing binary field");
  *out_value = value;
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_read_fields(data_bind_binary_reader_t *reader,
                                            const emit_field_array_t *fields,
                                            DataBindValue **out_object);

static DataBindStatus db_binary_read_collection_item(data_bind_binary_reader_t *reader,
                                                     const emit_field_t *field,
                                                     DataBindValue **out_value) {
  emit_field_t scalar = *field;
  switch (field->kind) {
  case EF_LIST_INT:
  case EF_SET_INT: scalar.kind = EF_INT; break;
  case EF_LIST_U32:
  case EF_SET_U32: scalar.kind = EF_U32; break;
  case EF_LIST_I64:
  case EF_SET_I64: scalar.kind = EF_I64; break;
  case EF_LIST_U64:
  case EF_SET_U64: scalar.kind = EF_U64; break;
  case EF_LIST_DBL:
  case EF_SET_DBL: scalar.kind = EF_DBL; break;
  case EF_LIST_BOOL:
  case EF_SET_BOOL: scalar.kind = EF_BOOL; break;
  case EF_LIST_STR:
  case EF_SET_STR: scalar.kind = EF_STR; break;
  case EF_LIST_OBJ: return db_binary_read_fields(reader, &field->children, out_value);
  default:
    return db_error_set(reader->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                        "Unsupported binary collection item");
  }
  return db_binary_read_scalar(reader, &scalar, out_value);
}

static DataBindStatus db_binary_read_collection(data_bind_binary_reader_t *reader,
                                                const emit_field_t *field,
                                                DataBindValue **out_value) {
  DataBindValue *collection;
  DataBindStatus status;
  uint32_t encoded_count = 0;
  size_t count;
  size_t i;
  if (field->fixed_count != 0)
    count = field->fixed_count;
  else {
    status = db_binary_read_u32(reader, field->name, &encoded_count);
    if (status != DATA_BIND_OK) return status;
    count = encoded_count;
  }
  collection =
      dbv_new(db_binary_is_set_kind(field->kind) ? DATA_BIND_VALUE_SET : DATA_BIND_VALUE_LIST);
  if (collection == NULL)
    return db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                        "Out of memory parsing binary collection");
  for (i = 0; i < count; ++i) {
    DataBindValue *item = NULL;
    status = db_binary_read_collection_item(reader, field, &item);
    if (status != DATA_BIND_OK || dbv_collection_push(collection, item) != DATA_BIND_OK) {
      if (status == DATA_BIND_OK)
        status = db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                              "Out of memory storing binary collection item");
      data_bind_value_free(item);
      data_bind_value_free(collection);
      return status;
    }
  }
  *out_value = collection;
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_read_map(data_bind_binary_reader_t *reader,
                                         const emit_field_t *field, DataBindValue **out_value) {
  DataBindValue *map = dbv_new(DATA_BIND_VALUE_MAP);
  DataBindStatus status;
  uint32_t count;
  uint32_t i;
  emit_field_t scalar = *field;
  if (map == NULL)
    return db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                        "Out of memory parsing binary map");
  status = db_binary_read_u32(reader, field->name, &count);
  for (i = 0; status == DATA_BIND_OK && i < count; ++i) {
    const uint8_t *key_data = NULL;
    uint32_t key_length;
    char *key = NULL;
    DataBindValue *item = NULL;
    status = db_binary_read_u32(reader, field->name, &key_length);
    if (status == DATA_BIND_OK)
      status = db_binary_reader_take(reader, key_length, field->name, &key_data);
    if (status != DATA_BIND_OK) break;
    key = (char *)malloc((size_t)key_length + 1u);
    if (key == NULL) {
      status = db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                            "Out of memory parsing binary map key");
      break;
    }
    memcpy(key, key_data, key_length);
    key[key_length] = '\0';
    if (field->kind == EF_MAP_STR_STR) scalar.kind = EF_STR;
    else if (field->kind == EF_MAP_STR_INT) scalar.kind = EF_INT;
    else if (field->kind == EF_MAP_STR_U32) scalar.kind = EF_U32;
    else if (field->kind == EF_MAP_STR_I64) scalar.kind = EF_I64;
    else if (field->kind == EF_MAP_STR_U64) scalar.kind = EF_U64;
    else if (field->kind == EF_MAP_STR_DBL) scalar.kind = EF_DBL;
    else scalar.kind = EF_BOOL;
    status = db_binary_read_scalar(reader, &scalar, &item);
    if (status == DATA_BIND_OK && !dbv_string_map_set(map, key, item)) {
      data_bind_value_free(item);
      status = db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                            "Out of memory storing binary map item");
    }
    free(key);
  }
  if (status != DATA_BIND_OK) {
    data_bind_value_free(map);
    return status;
  }
  *out_value = map;
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_read_group(data_bind_binary_reader_t *reader,
                                           const emit_field_t *field, DataBindValue **out_value) {
  const uint8_t *dimension;
  DataBindValue *list = NULL;
  DataBindStatus status;
  uint16_t block_length;
  uint16_t count;
  size_t i;
  if (field->group_dim < 4)
    return db_error_set(reader->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                        "Invalid group dimension size");
  status = db_binary_reader_take(reader, (size_t)field->group_dim, field->name, &dimension);
  if (status != DATA_BIND_OK) return status;
  block_length = tbe_wire_read_u16(dimension, 0);
  count = tbe_wire_read_u16(dimension + 2u, 0);
  if (block_length < (uint16_t)field->size)
    return db_error_set(reader->error, DATA_BIND_ERR_PARSE, field->name, -1, -1,
                        "Group block length is smaller than the schema layout");
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list == NULL)
    return db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                        "Out of memory parsing binary group");
  for (i = 0; i < count; ++i) {
    size_t start = reader->offset;
    DataBindValue *entry = NULL;
    status = db_binary_read_fields(reader, &field->children, &entry);
    if (status == DATA_BIND_OK && reader->offset - start > block_length)
      status = db_error_set(reader->error, DATA_BIND_ERR_PARSE, field->name, -1, -1,
                            "Group fields exceed the encoded block length");
    if (status == DATA_BIND_OK)
      status = db_binary_reader_take(reader, block_length - (reader->offset - start), field->name,
                                     NULL);
    if (status != DATA_BIND_OK || dbv_collection_push(list, entry) != DATA_BIND_OK) {
      if (status == DATA_BIND_OK)
        status = db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                              "Out of memory storing binary group entry");
      data_bind_value_free(entry);
      data_bind_value_free(list);
      return status;
    }
  }
  *out_value = list;
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_read_fields(data_bind_binary_reader_t *reader,
                                            const emit_field_array_t *fields,
                                            DataBindValue **out_object) {
  DataBindValue *object = dbv_new(DATA_BIND_VALUE_OBJECT);
  DataBindStatus status = DATA_BIND_OK;
  size_t i;
  if (out_object != NULL) *out_object = NULL;
  if (object == NULL)
    return db_error_set(reader->error, DATA_BIND_ERR_OOM, "binary", -1, -1,
                        "Out of memory parsing binary object");
  for (i = 0; i < fields->count; ++i) {
    const emit_field_t *field = &fields->items[i];
    DataBindValue *value = NULL;
    if (field->kind <= EF_VAR_BYTES)
      status = db_binary_read_scalar(reader, field, &value);
    else if (field->kind == EF_OBJECT)
      status = db_binary_read_fields(reader, &field->children, &value);
    else if (db_binary_is_list_kind(field->kind) || db_binary_is_set_kind(field->kind))
      status = db_binary_read_collection(reader, field, &value);
    else if (field->kind >= EF_MAP_STR_STR && field->kind <= EF_MAP_STR_BOOL)
      status = db_binary_read_map(reader, field, &value);
    else if (field->kind == EF_GROUP)
      status = db_binary_read_group(reader, field, &value);
    else
      status = db_error_set(reader->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                            "Unsupported binary field kind");
    if (status != DATA_BIND_OK ||
        dbv_object_set(object, field->name, value) != DATA_BIND_OK) {
      if (status == DATA_BIND_OK)
        status = db_error_set(reader->error, DATA_BIND_ERR_OOM, field->name, -1, -1,
                              "Out of memory storing binary field");
      data_bind_value_free(value);
      data_bind_value_free(object);
      return status;
    }
  }
  *out_object = object;
  return DATA_BIND_OK;
}

static int db_binary_field_supported(Node *schema_root, Node *field) {
  const char *type = get_string_val(find_child(field, "type"));
  if (type == NULL || field_flag(field, "is_optional")) return 0;
  if (field_flag(field, "is_composite_ref")) {
    Node *record = find_named_record(schema_root, "composites", type);
    Node *children = record != NULL ? find_child(record, "fields") : NULL;
    size_t i;
    if (children == NULL || children->type != NODE_LIST) return 0;
    for (i = 0; i < children->data.list.count; ++i)
      if (!db_binary_field_supported(schema_root, children->data.list.items[i])) return 0;
    return 1;
  }
  if (field_flag(field, "is_group_field")) {
    Node *record =
        find_named_record(schema_root, "groups", get_string_val(find_child(field, "group_type")));
    Node *children = record != NULL ? find_child(record, "fields") : NULL;
    size_t i;
    if (children == NULL || children->type != NODE_LIST ||
        parse_positive_int(get_string_val(find_child(record, "fixed_block_size"))) <= 0)
      return 0;
    for (i = 0; i < children->data.list.count; ++i)
      if (!db_binary_field_supported(schema_root, children->data.list.items[i])) return 0;
    return 1;
  }
  if (field_flag(field, "is_collection")) {
    const char *kind = get_string_val(find_child(field, "collection_kind"));
    const char *inner = get_string_val(find_child(field, "inner_type"));
    const char *key = get_string_val(find_child(field, "key_type"));
    const char *mapped = get_string_val(find_child(field, "value_type"));
    const type_meta_t *meta;
    if (kind == NULL) kind = type;
    if (strcmp(kind, "map") == 0) {
      if (key == NULL || mapped == NULL || strcmp(key, "string") != 0) return 0;
      if (strcmp(mapped, "string") == 0 || strcmp(mapped, "bool") == 0) return 1;
      meta = find_scalar_meta(schema_root, mapped);
      return meta != NULL;
    }
    if (inner == NULL ||
        (strcmp(kind, "list") != 0 && strcmp(kind, "set") != 0 && strcmp(kind, "array") != 0))
      return 0;
    if (strcmp(kind, "set") != 0) {
      Node *record = find_named_record(schema_root, "composites", inner);
      if (record != NULL) {
        Node *children = find_child(record, "fields");
        size_t i;
        if (children == NULL || children->type != NODE_LIST) return 0;
        for (i = 0; i < children->data.list.count; ++i)
          if (!db_binary_field_supported(schema_root, children->data.list.items[i])) return 0;
        return 1;
      }
    }
    if (strcmp(inner, "string") == 0) return 1;
    meta = find_scalar_meta(schema_root, inner);
    return meta != NULL;
  }
  if (field_flag(field, "is_var_data"))
    return field_flag(field, "is_string") || field_flag(field, "is_bytes");
  if (field_flag(field, "is_bytes"))
    return parse_positive_int(get_string_val(find_child(field, "size_bytes"))) > 0;
  if (field_flag(field, "is_uuid") || strcmp(type, "uuid") == 0) return 1;
  if (field_flag(field, "is_enum_ref")) return find_enum_meta(schema_root, type) != NULL;
  return find_type_meta(type) != NULL;
}

static DataBindStatus data_bind_object_check_schema(const DataBind *codec,
                                                    const DataBindObject *object,
                                                    const char *format,
                                                    DataBindError *error) {
  if (codec == NULL || object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, format, -1, -1,
                        "Invalid schema-bound object arguments");
  if (memcmp(codec->schema_fingerprint, object->schema_fingerprint,
             DATA_BIND_SCHEMA_FINGERPRINT_SIZE) != 0)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA,
                        object->type_name != NULL ? object->type_name : format, -1, -1,
                        "Object schema fingerprint does not match the codec");
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_plan(DataBind *codec, const DataBindObject *object,
                                     emit_field_array_t *fields, DataBindError *error) {
  Node *message;
  Node *schema_fields;
  const char *byte_order;
  size_t i;
  if (codec == NULL || object == NULL || object->type_name == NULL || object->value == NULL ||
      fields == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary serialize arguments");
  {
    DataBindStatus status = data_bind_object_check_schema(codec, object, "binary", error);
    if (status != DATA_BIND_OK) return status;
  }
  message = find_named_record(codec->schema_root, "messages", object->type_name);
  if (message == NULL)
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, object->type_name, -1, -1,
                        "Binary schema message was not found");
  byte_order = get_string_val(find_child(codec->schema_root, "wire_byte_order"));
  if (byte_order != NULL && strcmp(byte_order, "little") != 0)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, object->type_name, -1, -1,
                        "Dynamic binary codec currently requires little-endian schema order");
  schema_fields = find_child(message, "fields");
  if (schema_fields == NULL || schema_fields->type != NODE_LIST)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, object->type_name, -1, -1,
                        "Binary schema message has no field list");
  for (i = 0; i < schema_fields->data.list.count; ++i) {
    Node *field = schema_fields->data.list.items[i];
    if (!db_binary_field_supported(codec->schema_root, field))
      return db_error_set(error, DATA_BIND_ERR_SCHEMA, get_string_val(find_child(field, "name")),
                          -1, -1, "Schema field has no supported dynamic binary representation");
  }
  if (!build_fields(fields, schema_fields, codec->schema_root, NULL, 1))
    return db_error_set(error, DATA_BIND_ERR_OOM, object->type_name, -1, -1,
                        "Out of memory building binary serialization plan");
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_read_plan(DataBind *codec, const char *type_name,
                                          emit_field_array_t *fields, DataBindError *error) {
  Node *message;
  Node *schema_fields;
  const char *byte_order;
  size_t i;
  if (codec == NULL || type_name == NULL || fields == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary parse arguments");
  message = find_named_record(codec->schema_root, "messages", type_name);
  if (message == NULL)
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name, -1, -1,
                        "Binary schema message '%s' was not found", type_name);
  byte_order = get_string_val(find_child(codec->schema_root, "wire_byte_order"));
  if (byte_order != NULL && strcmp(byte_order, "little") != 0)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, type_name, -1, -1,
                        "Dynamic binary codec currently requires little-endian schema order");
  schema_fields = find_child(message, "fields");
  if (schema_fields == NULL || schema_fields->type != NODE_LIST)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, type_name, -1, -1,
                        "Binary schema message has no field list");
  for (i = 0; i < schema_fields->data.list.count; ++i) {
    Node *field = schema_fields->data.list.items[i];
    if (!db_binary_field_supported(codec->schema_root, field))
      return db_error_set(error, DATA_BIND_ERR_SCHEMA, get_string_val(find_child(field, "name")),
                          -1, -1, "Schema field has no supported dynamic binary representation");
  }
  if (!build_record_fields_v1(fields, schema_fields, codec->schema_root, NULL, 1))
    return db_error_set(error, DATA_BIND_ERR_OOM, type_name, -1, -1,
                        "Out of memory building binary parse plan");
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_measure(const emit_field_array_t *fields,
                                        const DataBindValue *value, size_t *out_len,
                                        DataBindError *error) {
  data_bind_binary_writer_t writer = {NULL, 0, 0, error};
  DataBindStatus status = db_binary_write_fields(&writer, fields, value);
  if (out_len != NULL) *out_len = status == DATA_BIND_OK ? writer.offset : 0;
  return status;
}

static Node *parse_schema_text_to_root(const char *schema_text, size_t len, const char *path,
                                       char *error_buf, size_t error_size, DataBindError *error) {
  Node *root;
  tbe_error_t err = {0};
  if (schema_text == NULL) {
    if (error_buf != NULL && error_size > 0) snprintf(error_buf, error_size, "Invalid schema text");
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, path, -1, -1, "Invalid schema text");
    return NULL;
  }
  root = create_node_map(NULL);
  if (root == NULL) {
    if (error_buf != NULL && error_size > 0) snprintf(error_buf, error_size, "Out of memory");
    db_error_set(error, DATA_BIND_ERR_OOM, path, -1, -1, "Out of memory");
    return NULL;
  }
  if (parse_schema(schema_text, len, root, &err) != 0) {
    if (error_buf != NULL && error_size > 0)
      snprintf(error_buf, error_size, "Parse error: %s", err.message);
    db_error_set(error, DATA_BIND_ERR_PARSE, path, err.line, err.column, "Parse error: %s",
                 err.message);
    node_free(root);
    return NULL;
  }
  db_error_clear(error);
  return root;
}

static DataBindStatus data_bind_create_from_root(Node *schema_root, DataBind **out_codec,
                                                 DataBindError *error) {
  DataBind *codec;
  DataBindStatus status;
  if (out_codec != NULL) *out_codec = NULL;
  if (schema_root == NULL || out_codec == NULL) {
    node_free(schema_root);
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid codec create arguments");
  }
  codec = (DataBind *)calloc(1, sizeof(*codec));
  if (codec == NULL) {
    node_free(schema_root);
    return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1, "Out of memory");
  }
  codec->schema_root = schema_root;
  status = validate_schema_binding_names(schema_root, error);
  if (status != DATA_BIND_OK) {
    node_free(schema_root);
    free(codec);
    return status;
  }
  if (!data_bind_schema_fingerprint(schema_root, codec->schema_fingerprint)) {
    node_free(schema_root);
    free(codec);
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, NULL, -1, -1,
                        "Failed to fingerprint parsed schema");
  }
  db_error_clear(error);
  *out_codec = codec;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_create(const char *schema_path, DataBind **out_codec,
                                DataBindError *error) {
  salts_fs_buf_t schema = {NULL, 0};
  Node *schema_root;
  DataBindStatus status;
  if (out_codec != NULL) *out_codec = NULL;
  if (schema_path == NULL || out_codec == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, schema_path, -1, -1,
                        "Invalid codec create arguments");
  if (salts_fs_read_file(schema_path, &schema) != 0)
    return db_error_set(error, DATA_BIND_ERR_IO, schema_path, -1, -1,
                        "Cannot read schema: %s", schema_path);
  schema_root =
      parse_schema_text_to_root(schema.base, schema.len, schema_path, NULL, 0, error);
  status = schema_root != NULL ? data_bind_create_from_root(schema_root, out_codec, error)
                               : db_error_code_or(error, DATA_BIND_ERR_SCHEMA);
  salts_fs_buf_free(&schema);
  return status;
}

DataBindStatus data_bind_create_from_text(const char *schema_text, size_t len,
                                          DataBind **out_codec, DataBindError *error) {
  Node *schema_root;
  if (out_codec != NULL) *out_codec = NULL;
  if (schema_text == NULL || out_codec == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid codec create arguments");
  schema_root = parse_schema_text_to_root(schema_text, len, NULL, NULL, 0, error);
  if (schema_root == NULL) return db_error_code_or(error, DATA_BIND_ERR_SCHEMA);
  return data_bind_create_from_root(schema_root, out_codec, error);
}

void data_bind_free(DataBind *codec) {
  if (codec == NULL) return;
  node_free(codec->schema_root);
  free(codec);
}

void data_bind_set_value_pool_enabled(int enabled) {
  DataBindValue *nodes[VALUE_POOL_SIZE];
  size_t node_count = 0;
  size_t i;
  salts_once(&g_value_pool_once, value_pool_init_once);
  salts_mutex_lock(&g_value_pool_control_mutex);

  if (enabled) {
    int state = atomic_load_explicit(&g_value_pool_state, memory_order_relaxed);
    if (state != VALUE_POOL_ENABLED) {
      atomic_store_explicit(&g_value_pool_ready_mask, 0, memory_order_relaxed);
      for (i = 0; i < VALUE_POOL_SIZE; ++i) {
        DataBindValue *expected = VALUE_POOL_CLOSED_SLOT;
        atomic_compare_exchange_strong_explicit(&g_value_pool_slots[i], &expected, NULL,
                                                memory_order_release, memory_order_relaxed);
      }
      atomic_store_explicit(&g_value_pool_state, VALUE_POOL_ENABLED, memory_order_release);
    }
  } else {
    atomic_store_explicit(&g_value_pool_state, VALUE_POOL_DISABLED, memory_order_release);
    for (i = 0; i < VALUE_POOL_SIZE; ++i) {
      DataBindValue *node = atomic_exchange_explicit(&g_value_pool_slots[i], VALUE_POOL_CLOSED_SLOT,
                                                     memory_order_acquire);
      if (node != NULL && node != VALUE_POOL_CLOSED_SLOT) nodes[node_count++] = node;
    }
    atomic_store_explicit(&g_value_pool_ready_mask, 0, memory_order_relaxed);
  }
  salts_mutex_unlock(&g_value_pool_control_mutex);

  for (i = 0; i < node_count; ++i)
    free(nodes[i]);
}

void data_bind_get_value_pool_stats(size_t *allocated, size_t *reused) {
  if (allocated != NULL)
    *allocated = atomic_load_explicit(&g_value_pool_allocated_count, memory_order_relaxed);
  if (reused != NULL)
    *reused = atomic_load_explicit(&g_value_pool_reused_count, memory_order_relaxed);
}

DataBindStatus data_bind_parse(DataBind *codec, const char *type_name, const uint8_t *buf,
                               size_t len, DataBindValue **out_value, DataBindError *error) {
  emit_field_array_t fields = {0};
  data_bind_binary_reader_t reader;
  DataBindValue *result = NULL;
  DataBindStatus status;
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || type_name == NULL || buf == NULL || out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid binary bind arguments");
  status = db_binary_read_plan(codec, type_name, &fields, error);
  if (status != DATA_BIND_OK) return status;
  reader.data = buf;
  reader.length = len;
  reader.offset = 0;
  reader.error = error;
  status = db_binary_read_fields(&reader, &fields, &result);
  if (status == DATA_BIND_OK && reader.offset != len)
    status = db_error_set(error, DATA_BIND_ERR_PARSE, "binary", -1, -1,
                          "Binary input has trailing bytes");
  if (status == DATA_BIND_OK) {
    status = db_dynamic_publish_result(codec, type_name, 0, result, out_value,
                                       error, "binary");
    result = NULL;
  }
  data_bind_value_free(result);
  emit_field_array_free(&fields);
  return status;
}

static DataBindStatus data_bind_parse_record_v1(DataBind *codec, const char *type_name,
                                                 const uint8_t *buf, size_t len,
                                                 DataBindValue **out_value,
                                                 DataBindError *error) {
  return data_bind_parse(codec, type_name, buf, len, out_value, error);
}

static int data_bind_stream_xml_name_char(char ch) {
  return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == ':' || ch == '.';
}

/* Unbounded wildcards can produce one temporary DOM per match; keep those all-match
 * paths on the existing buffered path until a direct schema SAX binder is available. */
static int data_bind_stream_json_path_is_bounded(const char *path) {
  return path != NULL && strchr(path, '*') == NULL;
}

static int data_bind_stream_xml_path_is_simple_descendant(const char *path) {
  const char *name;
  size_t len;
  size_t i;
  if (path == NULL || path[0] != '/' || path[1] != '/' || path[2] == '\0') return 0;
  name = path + 2;
  len = strlen(name);
  for (i = 0; i < len; ++i) {
    if (!data_bind_stream_xml_name_char(name[i])) return 0;
  }
  return 1;
}

static char *data_bind_stream_xml_target_from_path(const char *path) {
  const char *name;
  size_t len;
  char *target;
  if (!data_bind_stream_xml_path_is_simple_descendant(path)) return NULL;
  name = path + 2;
  len = strlen(name);
  target = (char *)malloc(len + 1);
  if (target == NULL) return NULL;
  memcpy(target, name, len + 1);
  return target;
}

static int data_bind_stream_xml_can_bind_incrementally(const char *path) {
  return data_bind_stream_xml_path_is_simple_descendant(path);
}

static void data_bind_stream_error_msg(data_bind_stream_t *parser, const char *message) {
  if (parser == NULL || message == NULL) return;
  snprintf(parser->format_state->stream_error, sizeof(parser->format_state->stream_error), "%s", message);
}

static int data_bind_stream_limit_exceeded(size_t current, size_t added, size_t limit) {
  return current > limit || added > limit - current;
}

static int data_bind_stream_emit_record(data_bind_stream_t *parser, const DataBindValue *record) {
  DataBindRecordAction action;
  if (parser == NULL || record == NULL || parser->record_callback == NULL ||
      parser->record_callback_stopped) {
    return 0;
  }
  action = parser->record_callback(parser->record_callback_user, record,
                                   parser->record_callback_index++);
  if (action == DATA_BIND_RECORD_CONTINUE) return 0;
  if (action == DATA_BIND_RECORD_STOP) {
    parser->record_callback_stopped = 1;
    return 0;
  }
  if (action == DATA_BIND_RECORD_CANCEL) {
    parser->canceled = 1;
    data_bind_stream_error_msg(parser, "Record callback canceled the stream");
    return -1;
  }
  parser->record_callback_failed = 1;
  data_bind_stream_error_msg(parser, "Record callback failed");
  return -1;
}

static DataBindStatus data_bind_stream_emit_result(data_bind_stream_t *parser,
                                                   const DataBindValue *value,
                                                   DataBindError *error) {
  size_t count;
  size_t i;
  if (parser == NULL || value == NULL) return DATA_BIND_OK;
  count = data_bind_value_kind(value) == DATA_BIND_VALUE_LIST ? data_bind_value_count(value) : 1u;
  if (data_bind_stream_limit_exceeded(parser->result_count, count,
                                      parser->limits.max_result_count)) {
    parser->limit_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_LIMIT, "stream.results", -1, -1,
                        "Stream result count exceeds limit of %zu",
                        parser->limits.max_result_count);
  }
  if (parser->record_callback != NULL && !parser->record_callback_stopped &&
      data_bind_value_kind(value) == DATA_BIND_VALUE_LIST) {
    for (i = 0; i < data_bind_value_count(value); ++i) {
      if (data_bind_stream_emit_record(parser, data_bind_value_at(value, i)) != 0) {
        if (parser->canceled)
          return db_error_set(error, DATA_BIND_ERR_CANCELED, "record_callback", -1, -1,
                              "Record callback canceled the stream at index %llu",
                              (unsigned long long)(parser->record_callback_index - 1));
        return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1,
                            "Record callback failed at index %llu",
                            (unsigned long long)(parser->record_callback_index - 1));
      }
      if (parser->record_callback_stopped) break;
    }
  } else if (parser->record_callback != NULL && !parser->record_callback_stopped &&
             data_bind_stream_emit_record(parser, value) != 0) {
    if (parser->canceled)
      return db_error_set(error, DATA_BIND_ERR_CANCELED, "record_callback", -1, -1,
                          "Record callback canceled the stream at index %llu",
                          (unsigned long long)(parser->record_callback_index - 1));
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1,
                        "Record callback failed at index %llu",
                        (unsigned long long)(parser->record_callback_index - 1));
  }
  parser->result_count += count;
  return DATA_BIND_OK;
}

static char *data_bind_stream_copy_slice(const char *text, size_t len) {
  char *copy = (char *)malloc(len + 1);
  if (copy == NULL) return NULL;
  if (len > 0) memcpy(copy, text, len);
  copy[len] = '\0';
  return copy;
}

static int data_bind_stream_values_push(data_bind_stream_t *parser, DataBindValue *item,
                                        const char *message) {
  DataBindStatus push_status;
  if (parser == NULL || item == NULL ||
      (parser->output_mode == DATA_BIND_STREAM_OUTPUT_RETAIN && parser->stream_values == NULL)) {
    data_bind_value_free(item);
    data_bind_stream_error_msg(parser, message);
    return -1;
  }
  push_status = db_dynamic_assign_sequence_item(parser->stream_values, item);
  if (push_status != DATA_BIND_OK) {
    data_bind_value_free(item);
    data_bind_stream_error_msg(parser,
                               "Stream item dynamic identity assignment failed");
    return -1;
  }
  if (parser->result_count >= parser->limits.max_result_count) {
    data_bind_value_free(item);
    parser->limit_failed = 1;
    data_bind_stream_error_msg(parser, "Stream result count limit exceeded");
    return -1;
  }
  if (data_bind_stream_emit_record(parser, item) != 0) {
    data_bind_value_free(item);
    return -1;
  }
  if (parser->output_mode == DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY) {
    data_bind_value_free(item);
  } else {
    push_status = dbv_collection_push(parser->stream_values, item);
    if (push_status != DATA_BIND_OK) {
      data_bind_value_free(item);
      if (push_status == DATA_BIND_ERR_LIMIT) {
        parser->limit_failed = 1;
        data_bind_stream_error_msg(parser, "Stream result count limit exceeded");
      } else {
        data_bind_stream_error_msg(parser, message);
      }
      return -1;
    }
  }
  parser->result_count++;
  return 0;
}

static int data_bind_stream_json_bind_value(data_bind_stream_t *parser, json_value_t *value) {
  DataBindValue *item;
  if (parser == NULL || value == NULL) return -1;
  item = bind_json_typed_value(parser->codec->schema_root, parser->type_name, value);
  (json_free(value), value = NULL);
  if (item == NULL) {
    data_bind_stream_error_msg(parser, "JSON stream item bind failed");
    return -1;
  }
  if (parser->format_state->json_path_stream_mode == DATA_BIND_JSON_PATH_STREAM_FIRST) {
    DataBindStatus identity_status = db_dynamic_attach_root(
        parser->codec, parser->type_name, 0, item);
    if (identity_status != DATA_BIND_OK) {
      data_bind_value_free(item);
      data_bind_stream_error_msg(
          parser, "JSON stream item dynamic identity attachment failed");
      return -1;
    }
    if (parser->result_count >= parser->limits.max_result_count) {
      data_bind_value_free(item);
      parser->limit_failed = 1;
      data_bind_stream_error_msg(parser, "Stream result count limit exceeded");
      return -1;
    }
    if (parser->result_count != 0 || data_bind_stream_emit_record(parser, item) != 0) {
      data_bind_value_free(item);
      if (parser->result_count != 0)
        data_bind_stream_error_msg(parser, "JSONPath stream selected multiple first values");
      return -1;
    }
    if (parser->output_mode == DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY) {
      data_bind_value_free(item);
    } else {
      parser->stream_values = item;
    }
    parser->result_count++;
    parser->format_state->json_stream_done = 1;
    return 0;
  }
  return data_bind_stream_values_push(parser, item, "JSON stream item append failed");
}

static int data_bind_stream_json_frame_reserve(data_bind_stream_t *parser) {
  data_bind_json_stream_frame_t *grown;
  size_t next_capacity;
  if (parser->format_state->json_frame_count < parser->format_state->json_frame_capacity) return 0;
  if (parser->format_state->json_frame_count >= DATA_BIND_JSON_STREAM_MAX_DEPTH) {
    data_bind_stream_error_msg(parser, "JSON stream nesting too deep");
    return -1;
  }
  next_capacity = parser->format_state->json_frame_capacity == 0 ? 8 : parser->format_state->json_frame_capacity * 2;
  if (next_capacity > DATA_BIND_JSON_STREAM_MAX_DEPTH)
    next_capacity = DATA_BIND_JSON_STREAM_MAX_DEPTH;
  if (next_capacity <= parser->format_state->json_frame_capacity) {
    data_bind_stream_error_msg(parser, "JSON stream nesting too deep");
    return -1;
  }
  grown =
      (data_bind_json_stream_frame_t *)realloc(parser->format_state->json_frames, next_capacity * sizeof(*grown));
  if (grown == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory growing JSON stream stack");
    return -1;
  }
  parser->format_state->json_frames = grown;
  parser->format_state->json_frame_capacity = next_capacity;
  return 0;
}

static int data_bind_stream_json_attach_value(data_bind_stream_t *parser, json_value_t *value) {
  data_bind_json_stream_frame_t *parent;
  if (parser->format_state->json_frame_count == 0) return 0;
  parent = &parser->format_state->json_frames[parser->format_state->json_frame_count - 1];
  if (parent->is_object) {
    if (parent->pending_key == NULL) {
      data_bind_stream_error_msg(parser, "JSON stream object value without key");
      return -1;
    }
    if (!json_object_add_checked(parent->value, parent->pending_key, value)) {
      data_bind_stream_error_msg(parser, "Out of memory appending JSON stream object value");
      return -1;
    }
    free(parent->pending_key);
    parent->pending_key = NULL;
  } else {
    if (!json_array_add_checked(parent->value, value)) {
      data_bind_stream_error_msg(parser, "Out of memory appending JSON stream array value");
      return -1;
    }
  }
  return 0;
}

static int data_bind_stream_json_scalar(data_bind_stream_t *parser, json_value_t *value) {
  if (value == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory creating JSON stream value");
    return -1;
  }
  if (!parser->format_state->json_stream_active || parser->format_state->json_sax_depth == 0) {
    (json_free(value), value = NULL);
    return 0;
  }
  if (parser->format_state->json_sax_depth == 1 || parser->format_state->json_frame_count > 0) {
    if (data_bind_stream_json_attach_value(parser, value) != 0) {
      (json_free(value), value = NULL);
      return -1;
    }
    if (parser->format_state->json_frame_count == 0) {
      return data_bind_stream_json_bind_value(parser, value);
    }
  } else {
    (json_free(value), value = NULL);
  }
  return 0;
}

static int data_bind_stream_json_container_start(data_bind_stream_t *parser, json_value_t *value,
                                                 int is_object) {
  data_bind_json_stream_frame_t *frame;
  if (value == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory creating JSON stream value");
    return -1;
  }
  if (!parser->format_state->json_stream_active || parser->format_state->json_sax_depth == 0) {
    (json_free(value), value = NULL);
    return 0;
  }
  if (parser->format_state->json_sax_depth != 1 && parser->format_state->json_frame_count == 0) {
    (json_free(value), value = NULL);
    return 0;
  }
  if (data_bind_stream_json_frame_reserve(parser) != 0 ||
      data_bind_stream_json_attach_value(parser, value) != 0) {
    (json_free(value), value = NULL);
    return -1;
  }
  frame = &parser->format_state->json_frames[parser->format_state->json_frame_count++];
  frame->value = value;
  frame->pending_key = NULL;
  frame->is_object = is_object;
  return 0;
}

static int data_bind_stream_json_container_end(data_bind_stream_t *parser, int is_object) {
  data_bind_json_stream_frame_t frame;
  if (parser == NULL || !parser->format_state->json_stream_active || parser->format_state->json_frame_count == 0) return 0;
  frame = parser->format_state->json_frames[parser->format_state->json_frame_count - 1];
  if (frame.is_object != is_object) {
    data_bind_stream_error_msg(parser, "JSON stream container mismatch");
    return -1;
  }
  free(frame.pending_key);
  parser->format_state->json_frames[--parser->format_state->json_frame_count].pending_key = NULL;
  if (parser->format_state->json_frame_count == 0) {
    return data_bind_stream_json_bind_value(parser, frame.value);
  }
  return 0;
}

static int data_bind_stream_json_on_null(void *ctx) {
  return data_bind_stream_json_scalar((data_bind_stream_t *)ctx, json_create_null());
}

static int data_bind_stream_json_on_bool(void *ctx, bool val) {
  return data_bind_stream_json_scalar((data_bind_stream_t *)ctx, json_create_bool(val));
}

static int data_bind_stream_json_on_number_raw(void *ctx, const char *val, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  json_value_t *value = NULL;
  if (val == NULL || len == 0 ||
      (value = json_parse(val, len)) == NULL ||
      json_type(value) != JSON_NUMBER) {
    (json_free(value), value = NULL);
    data_bind_stream_error_msg(parser, "Failed to preserve exact JSON stream number");
    return -1;
  }
  return data_bind_stream_json_scalar(parser, value);
}

static int data_bind_stream_json_on_string(void *ctx, const char *val, size_t len) {
  char *copy = data_bind_stream_copy_slice(val, len);
  json_value_t *value;
  if (copy == NULL) {
    data_bind_stream_error_msg((data_bind_stream_t *)ctx, "Out of memory copying JSON string");
    return -1;
  }
  value = json_create_string(copy);
  free(copy);
  return data_bind_stream_json_scalar((data_bind_stream_t *)ctx, value);
}

static int data_bind_stream_json_on_object_key(void *ctx, const char *key, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  data_bind_json_stream_frame_t *frame;
  if (parser == NULL || !parser->format_state->json_stream_active || parser->format_state->json_frame_count == 0) return 0;
  frame = &parser->format_state->json_frames[parser->format_state->json_frame_count - 1];
  if (!frame->is_object) return 0;
  free(frame->pending_key);
  frame->pending_key = data_bind_stream_copy_slice(key, len);
  if (frame->pending_key == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory copying JSON object key");
    return -1;
  }
  return 0;
}

static int data_bind_stream_json_on_object_start(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc = 0;
  if (parser == NULL) return -1;
  if (parser->format_state->json_sax_depth == 0) {
    parser->format_state->json_root_seen = 1;
  } else {
    rc = data_bind_stream_json_container_start(parser, json_create_object(), 1);
  }
  parser->format_state->json_sax_depth++;
  return rc;
}

static int data_bind_stream_json_on_object_end(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc;
  if (parser == NULL || parser->format_state->json_sax_depth == 0) return -1;
  rc = data_bind_stream_json_container_end(parser, 1);
  parser->format_state->json_sax_depth--;
  return rc;
}

static int data_bind_stream_json_on_array_start(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc = 0;
  if (parser == NULL) return -1;
  if (parser->format_state->json_sax_depth == 0) {
    parser->format_state->json_root_seen = 1;
    if (parser->format_state->json_stream_candidate) {
      parser->format_state->json_stream_active = 1;
      free(parser->buffer);
      parser->buffer = NULL;
      parser->size = 0;
      parser->capacity = 0;
    }
  } else {
    rc = data_bind_stream_json_container_start(parser, json_create_array(), 0);
  }
  parser->format_state->json_sax_depth++;
  return rc;
}

static int data_bind_stream_json_on_array_end(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc = 0;
  if (parser == NULL || parser->format_state->json_sax_depth == 0) return -1;
  if (parser->format_state->json_stream_active && parser->format_state->json_sax_depth == 1) {
    parser->format_state->json_stream_done = 1;
  } else {
    rc = data_bind_stream_json_container_end(parser, 0);
  }
  parser->format_state->json_sax_depth--;
  return rc;
}

static int data_bind_stream_json_path_match_start(void *ctx, json_type_t type) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  (void)type;
  if (parser == NULL) return -1;
  if (parser->format_state->json_path_stream_mode == DATA_BIND_JSON_PATH_STREAM_FIRST &&
      parser->format_state->json_stream_done) {
    parser->format_state->json_stream_active = 0;
    return 0;
  }
  if (parser->format_state->json_stream_active || parser->format_state->json_frame_count != 0 ||
      parser->format_state->json_match_value != NULL) {
    data_bind_stream_error_msg(parser, "Overlapping JSONPath stream matches are unsupported");
    return -1;
  }
  parser->format_state->json_stream_active = 1;
  return 0;
}

static int data_bind_stream_json_path_scalar(data_bind_stream_t *parser, json_value_t *value) {
  if (value == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory creating JSONPath stream value");
    return -1;
  }
  if (parser == NULL || !parser->format_state->json_stream_active) {
    (json_free(value), value = NULL);
    return 0;
  }
  if (parser->format_state->json_frame_count != 0) {
    if (data_bind_stream_json_attach_value(parser, value) != 0) {
      (json_free(value), value = NULL);
      return -1;
    }
  } else if (parser->format_state->json_match_value == NULL) {
    parser->format_state->json_match_value = value;
  } else {
    (json_free(value), value = NULL);
    data_bind_stream_error_msg(parser, "JSONPath stream match has multiple root values");
    return -1;
  }
  return 0;
}

static int data_bind_stream_json_path_container_start(data_bind_stream_t *parser,
                                                      json_value_t *value, int is_object) {
  data_bind_json_stream_frame_t *frame;
  if (value == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory creating JSONPath stream container");
    return -1;
  }
  if (parser == NULL || !parser->format_state->json_stream_active) {
    (json_free(value), value = NULL);
    return 0;
  }
  if (data_bind_stream_json_frame_reserve(parser) != 0) {
    (json_free(value), value = NULL);
    return -1;
  }
  if (parser->format_state->json_frame_count != 0) {
    if (data_bind_stream_json_attach_value(parser, value) != 0) {
      (json_free(value), value = NULL);
      return -1;
    }
  } else if (parser->format_state->json_match_value == NULL) {
    parser->format_state->json_match_value = value;
  } else {
    (json_free(value), value = NULL);
    data_bind_stream_error_msg(parser, "JSONPath stream match has multiple root containers");
    return -1;
  }
  frame = &parser->format_state->json_frames[parser->format_state->json_frame_count++];
  frame->value = value;
  frame->pending_key = NULL;
  frame->is_object = is_object;
  return 0;
}

static int data_bind_stream_json_path_container_end(data_bind_stream_t *parser, int is_object) {
  data_bind_json_stream_frame_t *frame;
  if (parser == NULL || !parser->format_state->json_stream_active) return 0;
  if (parser->format_state->json_frame_count == 0) {
    data_bind_stream_error_msg(parser, "JSONPath stream container stack underflow");
    return -1;
  }
  frame = &parser->format_state->json_frames[parser->format_state->json_frame_count - 1U];
  if (frame->is_object != is_object) {
    data_bind_stream_error_msg(parser, "JSONPath stream container mismatch");
    return -1;
  }
  free(frame->pending_key);
  frame->pending_key = NULL;
  parser->format_state->json_frame_count--;
  return 0;
}

static int data_bind_stream_json_path_match_end(void *ctx, json_type_t type) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  json_value_t *matched;
  (void)type;
  if (parser == NULL) return -1;
  if (!parser->format_state->json_stream_active) return 0;
  if (parser->format_state->json_frame_count != 0 || parser->format_state->json_match_value == NULL) {
    data_bind_stream_error_msg(parser, "Incomplete JSONPath stream match");
    return -1;
  }
  matched = parser->format_state->json_match_value;
  parser->format_state->json_match_value = NULL;
  parser->format_state->json_stream_active = 0;
  return data_bind_stream_json_bind_value(parser, matched);
}

static int data_bind_stream_json_path_on_null(void *ctx) {
  return data_bind_stream_json_path_scalar((data_bind_stream_t *)ctx,
                                           json_create_null());
}

static int data_bind_stream_json_path_on_bool(void *ctx, bool value) {
  return data_bind_stream_json_path_scalar((data_bind_stream_t *)ctx,
                                           json_create_bool(value));
}

static int data_bind_stream_json_path_on_number(void *ctx, const char *value, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  json_value_t *number = NULL;
  if (value == NULL || len == 0 ||
      (number = json_parse(value, len)) == NULL ||
      json_type(number) != JSON_NUMBER) {
    (json_free(number), number = NULL);
    data_bind_stream_error_msg(parser, "Failed to preserve exact JSONPath stream number");
    return -1;
  }
  return data_bind_stream_json_path_scalar(parser, number);
}

static int data_bind_stream_json_path_on_string(void *ctx, const char *value, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  char *copy = data_bind_stream_copy_slice(value, len);
  json_value_t *string;
  if (copy == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory copying JSONPath stream string");
    return -1;
  }
  string = json_create_string(copy);
  free(copy);
  return data_bind_stream_json_path_scalar(parser, string);
}

static int data_bind_stream_json_path_on_object_start(void *ctx) {
  return data_bind_stream_json_path_container_start((data_bind_stream_t *)ctx,
                                                    json_create_object(), 1);
}

static int data_bind_stream_json_path_on_array_start(void *ctx) {
  return data_bind_stream_json_path_container_start((data_bind_stream_t *)ctx,
                                                    json_create_array(), 0);
}

static int data_bind_stream_json_path_on_object_end(void *ctx) {
  return data_bind_stream_json_path_container_end((data_bind_stream_t *)ctx, 1);
}

static int data_bind_stream_json_path_on_array_end(void *ctx) {
  return data_bind_stream_json_path_container_end((data_bind_stream_t *)ctx, 0);
}

static int data_bind_stream_xml_append(data_bind_stream_t *parser, const char *text, size_t len) {
  tstr next;
  if (parser == NULL || !parser->format_state->xml_capture_active || len == 0) return 0;
  next = tstr_cat_len(parser->format_state->xml_capture, text, len);
  if (next == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory extending XML stream item");
    return -1;
  }
  parser->format_state->xml_capture = next;
  return 0;
}

static int data_bind_stream_xml_append_char(data_bind_stream_t *parser, char ch) {
  return data_bind_stream_xml_append(parser, &ch, 1);
}

static int data_bind_stream_xml_close_start(data_bind_stream_t *parser) {
  if (parser != NULL && parser->format_state->xml_capture_active && parser->format_state->xml_open_start) {
    if (data_bind_stream_xml_append_char(parser, '>') != 0) return -1;
    parser->format_state->xml_open_start = 0;
  }
  return 0;
}

static int data_bind_stream_xml_bind_capture(data_bind_stream_t *parser) {
  salts_xml_document doc = {0};
  DataBindValue *item;
  if (parser == NULL || parser->format_state->xml_capture == NULL) return -1;
  if (salts_xml_parse(&doc, parser->format_state->xml_capture, tstr_len(parser->format_state->xml_capture),
                      NULL, NULL) != SALTS_XML_OK) {
    data_bind_stream_error_msg(parser, "XML stream item parse failed");
    return -1;
  }
  item = bind_xml_typed_value(parser->codec->schema_root, parser->type_name, &doc, "/*");
  salts_xml_document_destroy(&doc);
  if (item == NULL) {
    data_bind_stream_error_msg(parser, "XML stream item bind failed");
    return -1;
  }
  return data_bind_stream_values_push(parser, item, "XML stream item append failed");
}

static int data_bind_stream_xml_name_eq(const char *left, size_t left_len, const char *right) {
  return right != NULL && strlen(right) == left_len && memcmp(left, right, left_len) == 0;
}

static int data_bind_stream_xml_on_element_start(void *ctx, const char *name, size_t name_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->format_state->xml_stream_candidate) return 0;
  if (!parser->format_state->xml_capture_active &&
      !data_bind_stream_xml_name_eq(name, name_len, parser->format_state->xml_stream_target)) {
    return 0;
  }
  if (!parser->format_state->xml_capture_active) {
    tstr_clear(parser->format_state->xml_capture);
    parser->format_state->xml_capture_active = 1;
    parser->format_state->xml_capture_depth = 0;
  } else if (data_bind_stream_xml_close_start(parser) != 0) {
    return -1;
  }
  if (data_bind_stream_xml_append_char(parser, '<') != 0 ||
      data_bind_stream_xml_append(parser, name, name_len) != 0) {
    return -1;
  }
  parser->format_state->xml_open_start = 1;
  parser->format_state->xml_capture_depth++;
  return 0;
}

static int data_bind_stream_xml_on_attribute(void *ctx, const char *name, size_t name_len,
                                             const char *value, size_t value_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->format_state->xml_capture_active || !parser->format_state->xml_open_start) return 0;
  if (data_bind_stream_xml_append_char(parser, ' ') != 0 ||
      data_bind_stream_xml_append(parser, name, name_len) != 0 ||
      data_bind_stream_xml_append(parser, "=\"", 2) != 0 ||
      data_bind_stream_xml_append(parser, value, value_len) != 0 ||
      data_bind_stream_xml_append_char(parser, '"') != 0) {
    return -1;
  }
  return 0;
}

static int data_bind_stream_xml_on_element_end(void *ctx, const char *name, size_t name_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc;
  if (parser == NULL || !parser->format_state->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0 ||
      data_bind_stream_xml_append(parser, "</", 2) != 0 ||
      data_bind_stream_xml_append(parser, name, name_len) != 0 ||
      data_bind_stream_xml_append_char(parser, '>') != 0) {
    return -1;
  }
  if (parser->format_state->xml_capture_depth > 0) parser->format_state->xml_capture_depth--;
  if (parser->format_state->xml_capture_depth == 0) {
    rc = data_bind_stream_xml_bind_capture(parser);
    parser->format_state->xml_capture_active = 0;
    parser->format_state->xml_open_start = 0;
    tstr_clear(parser->format_state->xml_capture);
    return rc;
  }
  return 0;
}

static int data_bind_stream_xml_on_text(void *ctx, const char *text, size_t text_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->format_state->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0) return -1;
  return data_bind_stream_xml_append(parser, text, text_len);
}

static int data_bind_stream_xml_on_comment(void *ctx, const char *text, size_t text_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->format_state->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0 ||
      data_bind_stream_xml_append(parser, "<!--", 4) != 0 ||
      data_bind_stream_xml_append(parser, text, text_len) != 0 ||
      data_bind_stream_xml_append(parser, "-->", 3) != 0) {
    return -1;
  }
  return 0;
}

static int data_bind_stream_xml_on_cdata(void *ctx, const char *text, size_t text_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->format_state->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0 ||
      data_bind_stream_xml_append(parser, "<![CDATA[", 9) != 0 ||
      data_bind_stream_xml_append(parser, text, text_len) != 0 ||
      data_bind_stream_xml_append(parser, "]]>", 3) != 0) {
    return -1;
  }
  return 0;
}

static const json_sax_handler_raw_t DATA_BIND_JSON_STREAM_HANDLER = {
    data_bind_stream_json_on_null,         data_bind_stream_json_on_bool,
    data_bind_stream_json_on_number_raw,   data_bind_stream_json_on_string,
    data_bind_stream_json_on_object_start, data_bind_stream_json_on_object_key,
    data_bind_stream_json_on_object_end,   data_bind_stream_json_on_array_start,
    data_bind_stream_json_on_array_end};

static const json_path_stream_handler_t DATA_BIND_JSON_PATH_STREAM_HANDLER = {
    data_bind_stream_json_path_match_start,
    data_bind_stream_json_path_match_end,
    {data_bind_stream_json_path_on_null,
     data_bind_stream_json_path_on_bool,
     data_bind_stream_json_path_on_number,
     data_bind_stream_json_path_on_string,
     data_bind_stream_json_path_on_object_start,
     data_bind_stream_json_on_object_key,
     data_bind_stream_json_path_on_object_end,
     data_bind_stream_json_path_on_array_start,
     data_bind_stream_json_path_on_array_end}};

static const salts_xml_sax_handler_t DATA_BIND_XML_STREAM_HANDLER = {
    NULL,
    NULL,
    data_bind_stream_xml_on_element_start,
    data_bind_stream_xml_on_attribute,
    data_bind_stream_xml_on_element_end,
    data_bind_stream_xml_on_text,
    data_bind_stream_xml_on_comment,
    data_bind_stream_xml_on_cdata,
    NULL,
    NULL};

static const json_sax_handler_t DATA_BIND_JSON_SAX_VALIDATE_HANDLER = {0};
static const cyaml_sax_handler_t DATA_BIND_YAML_SAX_VALIDATE_HANDLER = {0};
static const salts_xml_sax_handler_t DATA_BIND_XML_SAX_VALIDATE_HANDLER = {0};

static DataBindStatus data_bind_stream_sax_error(data_bind_stream_t *parser, DataBindError *error,
                                                 const char *operation) {
  const char *message = "Stream parse failed";
  const char *path = "stream";
  if (parser != NULL) {
    if (parser->format_state->stream_error[0] != '\0') {
      message = parser->format_state->stream_error;
    } else if (parser->format_state->json_path_stream != NULL) {
      message = json_path_stream_error(parser->format_state->json_path_stream);
      path = "json";
    } else if (parser->format_state->json_sax != NULL) {
      message = json_sax_parser_error(parser->format_state->json_sax);
      path = "json";
    } else if (parser->format_state->yaml_sax != NULL) {
      const cyaml_error_t *native_error = cyaml_sax_parser_error(parser->format_state->yaml_sax);
      message = native_error ? native_error->msg : NULL;
      path = "yaml";
    } else if (parser->format_state->xml_sax != NULL) {
      message = salts_xml_sax_parser_error(parser->format_state->xml_sax);
      path = "xml";
    }
    parser->format_state->sax_failed = 1;
  }
  if (message == NULL || message[0] == '\0') message = "Stream parse failed";
  if (parser != NULL && parser->canceled) {
    return db_error_set(error, DATA_BIND_ERR_CANCELED, "record_callback", -1, -1, "%s", message);
  }
  if (parser != NULL && parser->record_callback_failed) {
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1, "%s", message);
  }
  if (parser != NULL && parser->limit_failed) {
    return db_error_set(error, DATA_BIND_ERR_LIMIT, "stream.limit", -1, -1, "%s: %s", operation,
                        message);
  }
  return db_error_set(error, DATA_BIND_ERR_PARSE, path, -1, -1, "%s: %s", operation, message);
}

static DataBindStatus data_bind_stream_sax_feed(data_bind_stream_t *parser, const char *data,
                                                size_t len, DataBindError *error) {
  if (parser == NULL || parser->format_state->sax_failed) {
    return data_bind_stream_sax_error(parser, error, "stream feed");
  }
  if (parser->format_state->json_path_stream != NULL) {
    if (json_path_stream_feed(parser->format_state->json_path_stream, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "JSONPath stream feed");
    }
  } else if (parser->format_state->json_sax != NULL) {
    if (json_sax_parser_feed(parser->format_state->json_sax, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "JSON stream feed");
    }
  } else if (parser->format_state->yaml_sax != NULL) {
    if (cyaml_sax_parser_feed(parser->format_state->yaml_sax, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "YAML stream feed");
    }
  } else if (parser->format_state->xml_sax != NULL) {
    if (salts_xml_sax_parser_feed(parser->format_state->xml_sax, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "XML stream feed");
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_sax_finish(data_bind_stream_t *parser,
                                                  DataBindError *error) {
  if (parser == NULL || parser->format_state->sax_failed) {
    return data_bind_stream_sax_error(parser, error, "stream finish");
  }
  if (parser->format_state->json_path_stream != NULL) {
    if (json_path_stream_finish(parser->format_state->json_path_stream) != 0) {
      return data_bind_stream_sax_error(parser, error, "JSONPath stream finish");
    }
  } else if (parser->format_state->json_sax != NULL) {
    if (json_sax_parser_finish(parser->format_state->json_sax) != 0) {
      return data_bind_stream_sax_error(parser, error, "JSON stream finish");
    }
  } else if (parser->format_state->yaml_sax != NULL) {
    if (cyaml_sax_parser_finish(parser->format_state->yaml_sax) != 0) {
      return data_bind_stream_sax_error(parser, error, "YAML stream finish");
    }
  } else if (parser->format_state->xml_sax != NULL) {
    if (salts_xml_sax_parser_finish(parser->format_state->xml_sax) != 0) {
      return data_bind_stream_sax_error(parser, error, "XML stream finish");
    }
  }
  return DATA_BIND_OK;
}

static int data_bind_stream_csv_record_append(data_bind_stream_t *parser, char ch) {
  char *grown;
  size_t next_capacity;
  if (parser == NULL) return 0;
  if (parser->format_state->csv_record_len >= parser->limits.max_record_bytes) {
    parser->limit_failed = 1;
    data_bind_stream_error_msg(parser, "CSV record byte limit exceeded");
    return 0;
  }
  if (parser->format_state->csv_record_len + 1 >= parser->format_state->csv_record_capacity) {
    next_capacity = parser->format_state->csv_record_capacity == 0 ? 256 : parser->format_state->csv_record_capacity * 2;
    if (next_capacity <= parser->format_state->csv_record_capacity) return 0;
    if (next_capacity > parser->limits.max_record_bytes + 1u)
      next_capacity = parser->limits.max_record_bytes + 1u;
    grown = (char *)realloc(parser->format_state->csv_record, next_capacity);
    if (grown == NULL) return 0;
    parser->format_state->csv_record = grown;
    parser->format_state->csv_record_capacity = next_capacity;
  }
  parser->format_state->csv_record[parser->format_state->csv_record_len++] = ch;
  parser->format_state->csv_record[parser->format_state->csv_record_len] = '\0';
  return 1;
}

static int data_bind_stream_csv_field_append(data_bind_stream_t *parser, char ch) {
  char *grown;
  size_t next_capacity;
  if (parser == NULL) return 0;
  if (parser->format_state->csv_field_len >= parser->limits.max_field_bytes) {
    parser->limit_failed = 1;
    data_bind_stream_error_msg(parser, "CSV field byte limit exceeded");
    return 0;
  }
  if (parser->format_state->csv_field_len + 1 >= parser->format_state->csv_field_capacity) {
    next_capacity = parser->format_state->csv_field_capacity == 0 ? 128 : parser->format_state->csv_field_capacity * 2;
    if (next_capacity <= parser->format_state->csv_field_capacity) return 0;
    if (next_capacity > parser->limits.max_field_bytes + 1u)
      next_capacity = parser->limits.max_field_bytes + 1u;
    grown = (char *)realloc(parser->format_state->csv_field, next_capacity);
    if (grown == NULL) return 0;
    parser->format_state->csv_field = grown;
    parser->format_state->csv_field_capacity = next_capacity;
  }
  parser->format_state->csv_field[parser->format_state->csv_field_len++] = ch;
  parser->format_state->csv_field[parser->format_state->csv_field_len] = '\0';
  return 1;
}

static DataBindStatus data_bind_stream_csv_growth_error(data_bind_stream_t *parser,
                                                         DataBindError *error,
                                                         const char *oom_message) {
  if (parser != NULL) parser->format_state->csv_failed = 1;
  if (parser != NULL && parser->limit_failed)
    return db_error_set(error, DATA_BIND_ERR_LIMIT, "stream.limit", -1, -1, "%s",
                        parser->format_state->stream_error);
  return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1, "%s",
                      oom_message);
}

static void data_bind_stream_csv_clear_fields(data_bind_stream_t *parser) {
  size_t i;
  if (parser == NULL) return;
  for (i = 0; i < parser->format_state->csv_field_count; i++) {
    free(parser->format_state->csv_field_storage[i]);
    parser->format_state->csv_field_storage[i] = NULL;
  }
  parser->format_state->csv_field_count = 0;
  parser->format_state->csv_field_len = 0;
  if (parser->format_state->csv_field != NULL) parser->format_state->csv_field[0] = '\0';
}

static int data_bind_stream_csv_finish_field(data_bind_stream_t *parser) {
  char **grown_storage;
  vstr *grown_fields;
  char *field_copy;
  size_t next_capacity;

  if (parser == NULL) return 0;
  if (parser->format_state->csv_field_count >= parser->format_state->csv_fields_capacity) {
    next_capacity = parser->format_state->csv_fields_capacity == 0 ? 8 : parser->format_state->csv_fields_capacity * 2;
    if (next_capacity <= parser->format_state->csv_fields_capacity) return 0;
    grown_fields = (vstr *)realloc(parser->format_state->csv_fields, next_capacity * sizeof(*grown_fields));
    if (grown_fields == NULL) return 0;
    parser->format_state->csv_fields = grown_fields;
    grown_storage =
        (char **)realloc(parser->format_state->csv_field_storage, next_capacity * sizeof(*grown_storage));
    if (grown_storage == NULL) return 0;
    parser->format_state->csv_field_storage = grown_storage;
    parser->format_state->csv_fields_capacity = next_capacity;
  }

  field_copy = (char *)malloc(parser->format_state->csv_field_len + 1);
  if (field_copy == NULL) return 0;
  if (parser->format_state->csv_field_len > 0) memcpy(field_copy, parser->format_state->csv_field, parser->format_state->csv_field_len);
  field_copy[parser->format_state->csv_field_len] = '\0';
  parser->format_state->csv_field_storage[parser->format_state->csv_field_count] = field_copy;
  parser->format_state->csv_fields[parser->format_state->csv_field_count] = vstr_from_buf(field_copy, parser->format_state->csv_field_len);
  parser->format_state->csv_field_count++;
  parser->format_state->csv_field_len = 0;
  if (parser->format_state->csv_field != NULL) parser->format_state->csv_field[0] = '\0';
  return 1;
}

static DataBindStatus data_bind_stream_csv_compile_filter(data_bind_stream_t *parser,
                                                          DataBindError *error) {
  qvm_limits_t native_limits;
  qvm_diagnostic_t native_diagnostic = {0};
  char *header_doc = NULL;
  size_t header_doc_len;
  csv_options_t opts = {false, ',', '"', true};
  int compiled;

  if (parser == NULL || parser->path_or_expr == NULL || parser->format_state->csv_filter != NULL)
    return DATA_BIND_OK;

  native_limits = db_query_native_limits(
      parser->query_limits_configured ? &parser->query_limits : NULL);
  header_doc_len = parser->format_state->csv_header_len + 1;
  header_doc = (char *)malloc(header_doc_len + 1);
  if (header_doc == NULL) {
    parser->format_state->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                        "Out of memory building CSVPath stream header");
  }
  memcpy(header_doc, parser->format_state->csv_header, parser->format_state->csv_header_len);
  header_doc[parser->format_state->csv_header_len] = '\n';
  header_doc[header_doc_len] = '\0';

  if ((parser->format_state->csv_filter_doc = csv_parse_opts(header_doc, header_doc_len, &opts)) == NULL) {
    free(header_doc);
    parser->format_state->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1,
                        "Failed to parse CSVPath stream header");
  }
  free(header_doc);
  if (parser->format_state->csv_filter_doc == NULL) {
    parser->format_state->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1,
                        "Failed to parse CSVPath stream header");
  }

  parser->format_state->csv_filter = dsv_filter_create(parser->format_state->csv_filter_doc, 0);
  compiled = parser->format_state->csv_filter != NULL &&
             dsv_filter_compile_ex(parser->format_state->csv_filter, parser->path_or_expr,
                                   &native_limits, &native_diagnostic);
  db_query_diagnostic_copy(&parser->query_diagnostic, &native_diagnostic);
  if (!compiled) {
    const char *filter_error = parser->format_state->csv_filter != NULL
                                   ? dsv_filter_error(parser->format_state->csv_filter)
                                   : "Failed to create CSVPath filter";
    parser->format_state->csv_failed = 1;
    {
      DataBindStatus status =
          data_bind_query_failure_status(&parser->query_diagnostic);
      if (status == DATA_BIND_ERR_LIMIT) parser->limit_failed = 1;
      return db_error_set(error, status,
                        "csvpath", -1, -1, "%s", filter_error);
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_csv_process_record(data_bind_stream_t *parser,
                                                          DataBindError *error) {
  char *doc_text = NULL;
  size_t doc_len;
  DataBindValue *value = NULL;
  DataBindStatus status;
  int match;
  if (parser == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (parser->format_state->csv_record_len == 0 && parser->format_state->csv_header_seen) {
    data_bind_stream_csv_clear_fields(parser);
    return DATA_BIND_OK;
  }

  if (!parser->format_state->csv_header_seen) {
    parser->format_state->csv_header = (char *)malloc(parser->format_state->csv_record_len + 1);
    if (parser->format_state->csv_header == NULL) {
      parser->format_state->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory storing CSV stream header");
    }
    memcpy(parser->format_state->csv_header, parser->format_state->csv_record, parser->format_state->csv_record_len);
    parser->format_state->csv_header[parser->format_state->csv_record_len] = '\0';
    parser->format_state->csv_header_len = parser->format_state->csv_record_len;
    parser->format_state->csv_header_seen = 1;
    parser->format_state->csv_record_len = 0;
    if (parser->format_state->csv_record != NULL) parser->format_state->csv_record[0] = '\0';
    status = data_bind_stream_csv_compile_filter(parser, error);
    data_bind_stream_csv_clear_fields(parser);
    return status;
  }

  if (parser->path_or_expr != NULL) {
    if (parser->format_state->csv_filter == NULL) {
      status = data_bind_stream_csv_compile_filter(parser, error);
      if (status != DATA_BIND_OK) return status;
    }
    match = dsv_filter_check_values(parser->format_state->csv_filter, parser->format_state->csv_fields,
                                          parser->format_state->csv_field_count);
    if (match < 0) {
      const qvm_diagnostic_t *native_diagnostic = dsv_filter_qvm_diagnostic(parser->format_state->csv_filter);
      qvm_status_t query_status = native_diagnostic ? native_diagnostic->status
                                                   : QVM_STATUS_INVALID_ARGUMENT;
      db_query_diagnostic_copy(&parser->query_diagnostic, native_diagnostic);
      parser->format_state->csv_failed = 1;
      if (query_status == QVM_STATUS_RESOURCE_LIMIT) parser->limit_failed = 1;
      return db_error_set(error,
                          query_status == QVM_STATUS_RESOURCE_LIMIT
                              ? DATA_BIND_ERR_LIMIT
                              : DATA_BIND_ERR_PARSE,
                          "csvpath", -1, -1,
                          "CSVPath stream row filter evaluation failed");
    }
    if (match == 0) {
      data_bind_stream_csv_clear_fields(parser);
      parser->format_state->csv_record_len = 0;
      if (parser->format_state->csv_record != NULL) parser->format_state->csv_record[0] = '\0';
      parser->format_state->csv_data_row++;
      return DATA_BIND_OK;
    }
  }

  doc_len = parser->format_state->csv_header_len + 1 + parser->format_state->csv_record_len + 1;
  doc_text = (char *)malloc(doc_len + 1);
  if (doc_text == NULL) {
    parser->format_state->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                        "Out of memory building CSV stream row");
  }
  memcpy(doc_text, parser->format_state->csv_header, parser->format_state->csv_header_len);
  doc_text[parser->format_state->csv_header_len] = '\n';
  memcpy(doc_text + parser->format_state->csv_header_len + 1, parser->format_state->csv_record, parser->format_state->csv_record_len);
  doc_text[doc_len - 1] = '\n';
  doc_text[doc_len] = '\0';

  status =
      data_bind_parse_csv(parser->codec, parser->type_name, doc_text, doc_len, 0, &value, error);
  if (status == DATA_BIND_OK && value != NULL) {
    DataBindStatus push_status = DATA_BIND_OK;
    status = db_dynamic_rehome_sequence_item(parser->csv_values, value);
    if (status != DATA_BIND_OK) {
      data_bind_value_free(value);
      free(doc_text);
      parser->format_state->csv_failed = 1;
      return db_error_set(error, status, "data_bind_stream_feed", -1, -1,
                          "CSV stream item dynamic identity assignment failed");
    }
    if (parser->result_count >= parser->limits.max_result_count) {
      data_bind_value_free(value);
      free(doc_text);
      parser->format_state->csv_failed = 1;
      parser->limit_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_LIMIT, "stream.results", -1, -1,
                          "Stream result count exceeds limit of %zu",
                          parser->limits.max_result_count);
    }
    if (data_bind_stream_emit_record(parser, value) != 0) {
      data_bind_value_free(value);
      free(doc_text);
      parser->format_state->csv_failed = 1;
      if (parser->canceled)
        return db_error_set(error, DATA_BIND_ERR_CANCELED, "record_callback", -1, -1,
                            "Record callback canceled the stream at CSV row %llu",
                            (unsigned long long)parser->format_state->csv_data_row);
      return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1,
                          "Record callback failed at CSV row %llu",
                          (unsigned long long)parser->format_state->csv_data_row);
    }
    if (parser->output_mode == DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY) {
      data_bind_value_free(value);
    } else {
      push_status = dbv_collection_push(parser->csv_values, value);
      if (push_status != DATA_BIND_OK) {
        data_bind_value_free(value);
        free(doc_text);
        parser->format_state->csv_failed = 1;
        if (push_status == DATA_BIND_ERR_LIMIT) {
          parser->limit_failed = 1;
          return db_error_set(error, DATA_BIND_ERR_LIMIT, "stream.results", -1, -1,
                              "Stream result count exceeds limit of %zu",
                              parser->limits.max_result_count);
        }
        return db_error_set(error, push_status, "data_bind_stream_feed", -1, -1,
                            "Out of memory appending CSV stream row");
      }
    }
    parser->result_count++;
  }

  free(doc_text);
  data_bind_stream_csv_clear_fields(parser);
  parser->format_state->csv_record_len = 0;
  if (parser->format_state->csv_record != NULL) parser->format_state->csv_record[0] = '\0';
  parser->format_state->csv_data_row++;
  if (status != DATA_BIND_OK) parser->format_state->csv_failed = 1;
  return status;
}

static DataBindStatus data_bind_stream_csv_feed(data_bind_stream_t *parser, const char *data,
                                                size_t len, DataBindError *error) {
  size_t i;
  DataBindStatus status = DATA_BIND_OK;
  if (parser == NULL || data == NULL) return DATA_BIND_ERR_INVALID_ARG;

  for (i = 0; i < len; i++) {
    char ch = data[i];
  reprocess:
    if (parser->format_state->csv_skip_next_lf) {
      parser->format_state->csv_skip_next_lf = 0;
      if (ch == '\n') continue;
    }
    if (parser->format_state->csv_quote_pending) {
      parser->format_state->csv_quote_pending = 0;
      if (ch == '"') {
        if (!data_bind_stream_csv_record_append(parser, ch)) {
          return data_bind_stream_csv_growth_error(
              parser, error, "Out of memory extending CSV stream record");
        }
        if (!data_bind_stream_csv_field_append(parser, ch)) {
          return data_bind_stream_csv_growth_error(
              parser, error, "Out of memory extending CSV stream field");
        }
        continue;
      }
      parser->format_state->csv_in_quotes = 0;
      goto reprocess;
    }

    if (parser->format_state->csv_in_quotes) {
      if (!data_bind_stream_csv_record_append(parser, ch)) {
        return data_bind_stream_csv_growth_error(
            parser, error, "Out of memory extending CSV stream record");
      }
      if (ch == '"') {
        parser->format_state->csv_quote_pending = 1;
      } else if (!data_bind_stream_csv_field_append(parser, ch)) {
        return data_bind_stream_csv_growth_error(
            parser, error, "Out of memory extending CSV stream field");
      }
      continue;
    }

    if (ch == '"') {
      parser->format_state->csv_in_quotes = 1;
      if (!data_bind_stream_csv_record_append(parser, ch)) {
        return data_bind_stream_csv_growth_error(
            parser, error, "Out of memory extending CSV stream record");
      }
      continue;
    }
    if (ch == ',') {
      if (!data_bind_stream_csv_record_append(parser, ch) ||
          !data_bind_stream_csv_finish_field(parser)) {
        return data_bind_stream_csv_growth_error(
            parser, error, "Out of memory extending CSV stream field list");
      }
      continue;
    }
    if (ch == '\r' || ch == '\n') {
      if (!data_bind_stream_csv_finish_field(parser)) {
        parser->format_state->csv_failed = 1;
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Out of memory extending CSV stream field list");
      }
      status = data_bind_stream_csv_process_record(parser, error);
      if (status != DATA_BIND_OK) return status;
      if (ch == '\r') parser->format_state->csv_skip_next_lf = 1;
      continue;
    }
    if (!data_bind_stream_csv_record_append(parser, ch)) {
      return data_bind_stream_csv_growth_error(
          parser, error, "Out of memory extending CSV stream record");
    }
    if (!data_bind_stream_csv_field_append(parser, ch)) {
      return data_bind_stream_csv_growth_error(
          parser, error, "Out of memory extending CSV stream field");
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_csv_finish(data_bind_stream_t *parser,
                                                  DataBindValue **out_value, DataBindError *error) {
  DataBindStatus status;
  if (parser == NULL || out_value == NULL) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid CSV stream finish arguments");
  }
  if (parser->format_state->csv_quote_pending) {
    parser->format_state->csv_quote_pending = 0;
    parser->format_state->csv_in_quotes = 0;
  }
  if (parser->format_state->csv_in_quotes) {
    parser->format_state->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1, "Unterminated quoted CSV field");
  }
  if (parser->format_state->csv_record_len > 0 || parser->format_state->csv_field_len > 0 || parser->format_state->csv_field_count > 0 ||
      !parser->format_state->csv_header_seen) {
    if (!data_bind_stream_csv_finish_field(parser)) {
      parser->format_state->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory extending CSV stream field list");
    }
    status = data_bind_stream_csv_process_record(parser, error);
    if (status != DATA_BIND_OK) return status;
  }
  if (!parser->format_state->csv_header_seen || parser->format_state->csv_failed || parser->csv_values == NULL) {
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1, "CSV stream parse failed");
  }
  *out_value = parser->csv_values;
  parser->csv_values = NULL;
  db_error_clear(error);
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_text_feed(data_bind_stream_t *parser, const char *data,
                                                 size_t len, DataBindError *error);
static DataBindStatus data_bind_stream_json_finish(data_bind_stream_t *parser,
                                                   DataBindValue **out_value, DataBindError *error);
static DataBindStatus data_bind_stream_xml_finish(data_bind_stream_t *parser,
                                                  DataBindValue **out_value, DataBindError *error);
static DataBindStatus data_bind_stream_buffered_finish(data_bind_stream_t *parser,
                                                       DataBindValue **out_value,
                                                       DataBindError *error);
static DataBindStatus data_bind_parse_json_path_with_query(
    DataBind *codec, const char *type_name, const char *json, size_t len,
    const char *jsonpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error);
static DataBindStatus data_bind_parse_json_path_all_with_query(
    DataBind *codec, const char *type_name, const char *json, size_t len,
    const char *jsonpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error);
static DataBindStatus data_bind_parse_yaml_selected(
    DataBind *codec, const char *type_name, const char *yaml, size_t len,
    const char *yamlpath, int bind_all, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error);
static DataBindStatus data_bind_parse_xml_path_all_with_query(
    DataBind *codec, const char *type_name, const char *xml, size_t len,
    const char *xmlpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error);

static data_bind_stream_t *data_bind_stream_create_common(
    DataBind *codec, const char *type_name, const char *path_or_expr, DataBindValue **out_value,
    DataBindError *error,
    DataBindStatus (*feed_fn)(data_bind_stream_t *, const char *, size_t, DataBindError *),
    DataBindStatus (*finish_fn)(data_bind_stream_t *, DataBindValue **, DataBindError *),
    DataBindStatus (*bind_fn)(DataBind *, const char *, const char *, size_t, const char *,
                              const DataBindQueryLimits *, DataBindQueryDiagnostic *,
                              DataBindValue **, DataBindError *),
    int is_csv, int json_stream_candidate, int json_path_stream_mode,
    int xml_stream_candidate) {
  data_bind_stream_t *parser = NULL;
  size_t type_name_len;
  size_t path_len;
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' || out_value == NULL ||
      feed_fn == NULL || finish_fn == NULL || (!is_csv && bind_fn == NULL)) {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_create", -1, -1,
                 "Invalid stream constructor arguments");
    return NULL;
  }

  parser = (data_bind_stream_t *)calloc(
      1u, sizeof(*parser) + sizeof(data_bind_stream_format_state));
  if (parser == NULL) {
    db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                 "Out of memory creating stream");
    return NULL;
  }

  parser->format_state =
      (data_bind_stream_format_state *)(void *)(parser + 1);
  data_bind_stream_format_state_init(
      parser->format_state, is_csv, json_stream_candidate,
      json_path_stream_mode, xml_stream_candidate);

  type_name_len = strlen(type_name);
  parser->type_name = (char *)malloc(type_name_len + 1);
  if (parser->type_name == NULL) {
    free(parser);
    db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                 "Out of memory creating stream");
    return NULL;
  }
  memcpy(parser->type_name, type_name, type_name_len + 1);

  if (path_or_expr != NULL && path_or_expr[0] != '\0') {
    path_len = strlen(path_or_expr);
    parser->path_or_expr = (char *)malloc(path_len + 1);
    if (parser->path_or_expr == NULL) {
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating stream");
      return NULL;
    }
    memcpy(parser->path_or_expr, path_or_expr, path_len + 1);
  } else {
    parser->path_or_expr = NULL;
  }

  parser->codec = codec;
  parser->out_value = out_value;
  parser->internal_out_value = NULL;
  parser->error = error;
  parser->record_callback = NULL;
  parser->record_callback_user = NULL;
  parser->record_callback_index = 0;
  parser->output_mode = DATA_BIND_STREAM_OUTPUT_RETAIN;
  parser->limits.size = sizeof(parser->limits);
  parser->limits.max_input_bytes = DATA_BIND_STREAM_DEFAULT_MAX_INPUT_BYTES;
  parser->limits.max_record_bytes = DATA_BIND_STREAM_DEFAULT_MAX_RECORD_BYTES;
  parser->limits.max_field_bytes = DATA_BIND_STREAM_DEFAULT_MAX_FIELD_BYTES;
  parser->limits.max_result_count = DATA_BIND_STREAM_DEFAULT_MAX_RESULT_COUNT;
  parser->query_limits = (DataBindQueryLimits)DATA_BIND_QUERY_LIMITS_INIT;
  parser->query_diagnostic =
      (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  parser->query_limits_configured = 0;
  parser->total_input_bytes = 0;
  parser->result_count = 0;
  parser->feed_fn = feed_fn;
  parser->finish_fn = finish_fn;
  parser->bind_fn = bind_fn;
  parser->buffer = NULL;
  parser->size = 0;
  parser->capacity = 0;
  parser->csv_values = NULL;
  parser->stream_values = NULL;
  parser->finished = 0;
  parser->started = 0;
  parser->record_callback_stopped = 0;
  parser->record_callback_failed = 0;
  parser->limit_failed = 0;
  parser->canceled = 0;
  if (is_csv) {
    parser->csv_values = dbv_new(DATA_BIND_VALUE_LIST);
    if (parser->csv_values == NULL ||
        !dbv_sequence_set_limit(parser->csv_values,
                                parser->limits.max_result_count)) {
      data_bind_value_free(parser->csv_values);
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating CSV stream output");
      return NULL;
    }
  } else if (finish_fn == data_bind_stream_json_finish) {
    if (parser->format_state->json_stream_candidate ||
        parser->format_state->json_path_stream_mode == DATA_BIND_JSON_PATH_STREAM_ALL) {
      parser->stream_values = dbv_new(DATA_BIND_VALUE_LIST);
      if (parser->stream_values == NULL ||
          !dbv_sequence_set_limit(parser->stream_values,
                                  parser->limits.max_result_count)) {
        data_bind_value_free(parser->stream_values);
        free(parser->path_or_expr);
        free(parser->type_name);
        free(parser);
        db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                     "Out of memory creating JSON stream output");
        return NULL;
      }
    }
    if (parser->format_state->json_path_stream_mode != DATA_BIND_JSON_PATH_STREAM_NONE) {
      const char *path_error;
      parser->format_state->json_path_program = json_path_compile(parser->path_or_expr);
      if (parser->format_state->json_path_program == NULL) {
        parser->format_state->json_path_stream_mode = DATA_BIND_JSON_PATH_STREAM_NONE;
        data_bind_value_free(parser->stream_values);
        parser->stream_values = NULL;
      } else {
        parser->format_state->json_path_stream = json_path_stream_create(
            parser->format_state->json_path_program, &DATA_BIND_JSON_PATH_STREAM_HANDLER, parser);
      }
      if (parser->format_state->json_path_program != NULL && parser->format_state->json_path_stream == NULL) {
        path_error = json_path_stream_error(NULL);
        if (path_error != NULL && strstr(path_error, "not streamable") != NULL) {
          json_path_program_free(parser->format_state->json_path_program);
          parser->format_state->json_path_program = NULL;
          parser->format_state->json_path_stream_mode = DATA_BIND_JSON_PATH_STREAM_NONE;
          data_bind_value_free(parser->stream_values);
          parser->stream_values = NULL;
        } else {
          json_path_program_free(parser->format_state->json_path_program);
          data_bind_value_free(parser->stream_values);
          free(parser->path_or_expr);
          free(parser->type_name);
          free(parser);
          db_error_set(error, DATA_BIND_ERR_OOM, "json", -1, -1,
                       "Unable to create JSONPath stream: %s",
                       path_error != NULL ? path_error : "out of memory");
          return NULL;
        }
      }
    }
    if (parser->format_state->json_path_stream == NULL) {
      parser->format_state->json_sax = parser->format_state->json_stream_candidate
                             ? json_sax_parser_create_raw(&DATA_BIND_JSON_STREAM_HANDLER,
                                                               parser)
                             : json_sax_parser_create(&DATA_BIND_JSON_SAX_VALIDATE_HANDLER,
                                                           parser);
    }
    if (parser->format_state->json_path_stream == NULL && parser->format_state->json_sax == NULL) {
      data_bind_value_free(parser->stream_values);
      json_path_program_free(parser->format_state->json_path_program);
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating JSON stream validator");
      return NULL;
    }
  } else if (finish_fn == data_bind_stream_buffered_finish) {
    parser->format_state->yaml_sax =
        cyaml_sax_parser_create(&DATA_BIND_YAML_SAX_VALIDATE_HANDLER, parser, NULL);
    if (parser->format_state->yaml_sax == NULL) {
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating YAML stream validator");
      return NULL;
    }
  } else if (finish_fn == data_bind_stream_xml_finish) {
    if (parser->format_state->xml_stream_candidate) {
      parser->format_state->xml_stream_target = data_bind_stream_xml_target_from_path(parser->path_or_expr);
      parser->format_state->xml_capture = tstr_new();
      parser->stream_values = dbv_new(DATA_BIND_VALUE_LIST);
      if (parser->format_state->xml_stream_target == NULL || parser->format_state->xml_capture == NULL ||
          parser->stream_values == NULL ||
          !dbv_sequence_set_limit(parser->stream_values,
                                  parser->limits.max_result_count)) {
        free(parser->format_state->xml_stream_target);
        tstr_free(parser->format_state->xml_capture);
        data_bind_value_free(parser->stream_values);
        free(parser->path_or_expr);
        free(parser->type_name);
        free(parser);
        db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                     "Out of memory creating XML stream output");
        return NULL;
      }
    }
    parser->format_state->xml_sax = salts_xml_sax_parser_create(parser->format_state->xml_stream_candidate
                                                      ? &DATA_BIND_XML_STREAM_HANDLER
                                                      : &DATA_BIND_XML_SAX_VALIDATE_HANDLER,
                                                  parser, parser->limits.max_input_bytes);
    if (parser->format_state->xml_sax == NULL) {
      free(parser->format_state->xml_stream_target);
      tstr_free(parser->format_state->xml_capture);
      data_bind_value_free(parser->stream_values);
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating XML stream validator");
      return NULL;
    }
  }
  if (parser->csv_values != NULL) {
    DataBindStatus identity_status = db_dynamic_attach_root(
        parser->codec, parser->type_name, 1, parser->csv_values);
    if (identity_status != DATA_BIND_OK) {
      data_bind_stream_destroy(parser);
      db_error_set(error, identity_status, "data_bind_stream_create", -1, -1,
                   "Failed to build CSV stream dynamic identity graph");
      return NULL;
    }
  }
  if (parser->stream_values != NULL) {
    DataBindStatus identity_status = db_dynamic_attach_root(
        parser->codec, parser->type_name, 1, parser->stream_values);
    if (identity_status != DATA_BIND_OK) {
      data_bind_stream_destroy(parser);
      db_error_set(error, identity_status, "data_bind_stream_create", -1, -1,
                   "Failed to build stream dynamic identity graph");
      return NULL;
    }
  }
  db_error_clear(error);
  return parser;
}

static DataBindStatus data_bind_stream_bind_json(DataBind *codec, const char *type_name,
                                                 const char *text, size_t len, const char *path,
                                                 const DataBindQueryLimits *query_limits,
                                                 DataBindQueryDiagnostic *query_diagnostic,
                                                 DataBindValue **out_value, DataBindError *error) {
  (void)path;
  (void)query_limits;
  (void)query_diagnostic;
  return data_bind_parse_json(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_json_all(DataBind *codec, const char *type_name,
                                                     const char *text, size_t len, const char *path,
                                                     const DataBindQueryLimits *query_limits,
                                                     DataBindQueryDiagnostic *query_diagnostic,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  (void)path;
  (void)query_limits;
  (void)query_diagnostic;
  return data_bind_parse_json_all(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_json_path(DataBind *codec, const char *type_name,
                                                      const char *text, size_t len,
                                                      const char *path,
                                                      const DataBindQueryLimits *query_limits,
                                                      DataBindQueryDiagnostic *query_diagnostic,
                                                      DataBindValue **out_value,
                                                      DataBindError *error) {
  return data_bind_parse_json_path_with_query(codec, type_name, text, len, path,
                                              query_limits, query_diagnostic,
                                              out_value, error);
}

static DataBindStatus data_bind_stream_bind_json_path_all(DataBind *codec, const char *type_name,
                                                          const char *text, size_t len,
                                                          const char *path,
                                                          const DataBindQueryLimits *query_limits,
                                                          DataBindQueryDiagnostic *query_diagnostic,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  return data_bind_parse_json_path_all_with_query(codec, type_name, text, len, path,
                                                  query_limits, query_diagnostic,
                                                  out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml(DataBind *codec, const char *type_name,
                                                 const char *text, size_t len, const char *path,
                                                 const DataBindQueryLimits *query_limits,
                                                 DataBindQueryDiagnostic *query_diagnostic,
                                                 DataBindValue **out_value, DataBindError *error) {
  (void)path;
  (void)query_limits;
  (void)query_diagnostic;
  return data_bind_parse_yaml(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml_all(DataBind *codec, const char *type_name,
                                                     const char *text, size_t len, const char *path,
                                                     const DataBindQueryLimits *query_limits,
                                                     DataBindQueryDiagnostic *query_diagnostic,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  (void)path;
  (void)query_limits;
  (void)query_diagnostic;
  return data_bind_parse_yaml_all(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml_path(DataBind *codec, const char *type_name,
                                                      const char *text, size_t len,
                                                      const char *path,
                                                      const DataBindQueryLimits *query_limits,
                                                      DataBindQueryDiagnostic *query_diagnostic,
                                                      DataBindValue **out_value,
                                                      DataBindError *error) {
  return data_bind_parse_yaml_selected(codec, type_name, text, len, path, 0,
                                       query_limits, query_diagnostic, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml_path_all(DataBind *codec, const char *type_name,
                                                          const char *text, size_t len,
                                                          const char *path,
                                                          const DataBindQueryLimits *query_limits,
                                                          DataBindQueryDiagnostic *query_diagnostic,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  return data_bind_parse_yaml_selected(codec, type_name, text, len, path, 1,
                                       query_limits, query_diagnostic, out_value, error);
}

static DataBindStatus data_bind_stream_bind_xml(DataBind *codec, const char *type_name,
                                                const char *text, size_t len, const char *path,
                                                const DataBindQueryLimits *query_limits,
                                                DataBindQueryDiagnostic *query_diagnostic,
                                                DataBindValue **out_value, DataBindError *error) {
  (void)path;
  (void)query_limits;
  (void)query_diagnostic;
  return data_bind_parse_xml(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_xml_path_all(DataBind *codec, const char *type_name,
                                                         const char *text, size_t len,
                                                         const char *path,
                                                         const DataBindQueryLimits *query_limits,
                                                         DataBindQueryDiagnostic *query_diagnostic,
                                                         DataBindValue **out_value,
                                                         DataBindError *error) {
  return data_bind_parse_xml_path_all_with_query(codec, type_name, text, len, path,
                                                 query_limits, query_diagnostic,
                                                 out_value, error);
}

data_bind_stream_t *data_bind_stream_json_create(DataBind *codec, const char *type_name,
                                                 DataBindValue **out_value, DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json, 0, 0,
                                        DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_json_all_create(DataBind *codec, const char *type_name,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json_all, 0, 1,
                                        DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_json_path_create(DataBind *codec, const char *type_name,
                                                      const char *json_path,
                                                      DataBindValue **out_value,
                                                      DataBindError *error) {
  if (json_path == NULL || json_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_json_path_create", -1, -1,
                 "JSONPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, json_path, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json_path, 0, 0,
                                        DATA_BIND_JSON_PATH_STREAM_FIRST, 0);
}

data_bind_stream_t *data_bind_stream_json_path_all_create(DataBind *codec, const char *type_name,
                                                          const char *json_path,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  if (json_path == NULL || json_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_json_path_all_create", -1, -1,
                 "JSONPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, json_path, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json_path_all, 0,
                                        strcmp(json_path, "$[*]") == 0 ? 1 : 0,
                                        strcmp(json_path, "$[*]") == 0
                                            ? DATA_BIND_JSON_PATH_STREAM_NONE
                                            : (data_bind_stream_json_path_is_bounded(json_path)
                                                   ? DATA_BIND_JSON_PATH_STREAM_ALL
                                                   : DATA_BIND_JSON_PATH_STREAM_NONE),
                                        0);
}

data_bind_stream_t *data_bind_stream_yaml_create(DataBind *codec, const char *type_name,
                                                 DataBindValue **out_value, DataBindError *error) {
  return data_bind_stream_create_common(
      codec, type_name, NULL, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml, 0, 0,
      DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_yaml_all_create(DataBind *codec, const char *type_name,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  return data_bind_stream_create_common(
      codec, type_name, NULL, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml_all, 0, 0,
      DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_yaml_path_create(DataBind *codec, const char *type_name,
                                                      const char *yaml_path,
                                                      DataBindValue **out_value,
                                                      DataBindError *error) {
  if (yaml_path == NULL || yaml_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_yaml_path_create", -1, -1,
                 "YPATH is required");
    return NULL;
  }
  return data_bind_stream_create_common(
      codec, type_name, yaml_path, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml_path, 0, 0,
      DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_yaml_path_all_create(DataBind *codec, const char *type_name,
                                                          const char *yaml_path,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  if (yaml_path == NULL || yaml_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_yaml_path_all_create", -1, -1,
                 "YPATH is required");
    return NULL;
  }
  return data_bind_stream_create_common(
      codec, type_name, yaml_path, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml_path_all, 0, 0,
      DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_csv_all_create(DataBind *codec, const char *type_name,
                                                    DataBindValue **out_value,
                                                    DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_csv_feed, data_bind_stream_csv_finish,
                                        NULL, 1, 0, DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_csv_path_create(DataBind *codec, const char *type_name,
                                                     const char *csv_path,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  if (csv_path == NULL || csv_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_csv_path_create", -1, -1,
                 "CSVPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, csv_path, out_value, error,
                                        data_bind_stream_csv_feed, data_bind_stream_csv_finish,
                                        NULL, 1, 0, DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_xml_create(DataBind *codec, const char *type_name,
                                                DataBindValue **out_value, DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_xml_finish,
                                        data_bind_stream_bind_xml, 0, 0,
                                        DATA_BIND_JSON_PATH_STREAM_NONE, 0);
}

data_bind_stream_t *data_bind_stream_xml_path_all_create(DataBind *codec, const char *type_name,
                                                         const char *xml_path,
                                                         DataBindValue **out_value,
                                                         DataBindError *error) {
  if (xml_path == NULL || xml_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_xml_path_all_create", -1, -1,
                 "XMLPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, xml_path, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_xml_finish,
                                        data_bind_stream_bind_xml_path_all, 0, 0,
                                        DATA_BIND_JSON_PATH_STREAM_NONE,
                                        data_bind_stream_xml_can_bind_incrementally(xml_path));
}

static int data_bind_stream_supports(DataBindFormat format, DataBindStreamSelection selection) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
  case DATA_BIND_FORMAT_YAML:
    return selection == DATA_BIND_STREAM_SELECT_ROOT ||
           selection == DATA_BIND_STREAM_SELECT_ALL ||
           selection == DATA_BIND_STREAM_SELECT_PATH_FIRST ||
           selection == DATA_BIND_STREAM_SELECT_PATH_ALL;
  case DATA_BIND_FORMAT_CSV:
    return selection == DATA_BIND_STREAM_SELECT_ALL ||
           selection == DATA_BIND_STREAM_SELECT_PATH_ALL;
  case DATA_BIND_FORMAT_XML:
    return selection == DATA_BIND_STREAM_SELECT_ROOT ||
           selection == DATA_BIND_STREAM_SELECT_PATH_ALL;
  case DATA_BIND_FORMAT_BINARY:
  default:
    return 0;
  }
}

DataBindStatus data_bind_stream_create(DataBind *codec, const DataBindStreamConfig *config,
                                       data_bind_stream_t **out_stream, DataBindError *error) {
  DataBindValue *discard_output = NULL;
  DataBindValue **output = NULL;
  data_bind_stream_t *stream = NULL;
  DataBindStatus status;
  DataBindError create_error = DATA_BIND_ERROR_INIT;
  const size_t required_size = offsetof(DataBindStreamConfig, out_value) + sizeof(config->out_value);

  if (out_stream != NULL) *out_stream = NULL;
  if (codec == NULL || config == NULL || out_stream == NULL || config->size < required_size) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_create", -1, -1,
                        "Invalid configured stream arguments");
  }
  if (config->out_value != NULL) *config->out_value = NULL;
  if (config->type_name == NULL || config->type_name[0] == '\0') {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_create", -1, -1,
                        "Invalid configured stream arguments");
  }
  if (config->output_mode != DATA_BIND_STREAM_OUTPUT_RETAIN &&
      config->output_mode != DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "stream.output_mode", -1, -1,
                        "Invalid configured stream output mode");
  }
  if (config->output_mode == DATA_BIND_STREAM_OUTPUT_RETAIN && config->out_value == NULL) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "stream.out_value", -1, -1,
                        "Retained stream output requires out_value");
  }
  if (config->output_mode == DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY &&
      config->record_callback == NULL) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "stream.record_callback", -1, -1,
                        "Callback-only stream output requires a record callback");
  }
  if (!data_bind_stream_supports(config->format, config->selection)) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "stream.format_selection", -1, -1,
                        "Unsupported stream format and selection combination");
  }
  if ((config->selection == DATA_BIND_STREAM_SELECT_PATH_FIRST ||
       config->selection == DATA_BIND_STREAM_SELECT_PATH_ALL) &&
      (config->path == NULL || config->path[0] == '\0')) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "stream.path", -1, -1,
                        "Path selection requires a non-empty path");
  }
  if ((config->selection == DATA_BIND_STREAM_SELECT_ROOT ||
       config->selection == DATA_BIND_STREAM_SELECT_ALL) &&
      config->path != NULL && config->path[0] != '\0') {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "stream.path", -1, -1,
                        "Root/all stream selection does not accept a path");
  }

  output = config->out_value != NULL ? config->out_value : &discard_output;
  switch (config->format) {
  case DATA_BIND_FORMAT_JSON:
    switch (config->selection) {
    case DATA_BIND_STREAM_SELECT_ROOT:
      stream = data_bind_stream_json_create(codec, config->type_name, output, &create_error);
      break;
    case DATA_BIND_STREAM_SELECT_ALL:
      stream = data_bind_stream_json_all_create(codec, config->type_name, output, &create_error);
      break;
    case DATA_BIND_STREAM_SELECT_PATH_FIRST:
      stream = data_bind_stream_json_path_create(codec, config->type_name, config->path, output,
                                                 &create_error);
      break;
    case DATA_BIND_STREAM_SELECT_PATH_ALL:
      stream = data_bind_stream_json_path_all_create(codec, config->type_name, config->path, output,
                                                     &create_error);
      break;
    default: break;
    }
    break;
  case DATA_BIND_FORMAT_YAML:
    switch (config->selection) {
    case DATA_BIND_STREAM_SELECT_ROOT:
      stream = data_bind_stream_yaml_create(codec, config->type_name, output, &create_error);
      break;
    case DATA_BIND_STREAM_SELECT_ALL:
      stream = data_bind_stream_yaml_all_create(codec, config->type_name, output, &create_error);
      break;
    case DATA_BIND_STREAM_SELECT_PATH_FIRST:
      stream = data_bind_stream_yaml_path_create(codec, config->type_name, config->path, output,
                                                 &create_error);
      break;
    case DATA_BIND_STREAM_SELECT_PATH_ALL:
      stream = data_bind_stream_yaml_path_all_create(codec, config->type_name, config->path, output,
                                                     &create_error);
      break;
    default: break;
    }
    break;
  case DATA_BIND_FORMAT_CSV:
    if (config->selection == DATA_BIND_STREAM_SELECT_ALL)
      stream = data_bind_stream_csv_all_create(codec, config->type_name, output, &create_error);
    else if (config->selection == DATA_BIND_STREAM_SELECT_PATH_ALL)
      stream = data_bind_stream_csv_path_create(codec, config->type_name, config->path, output,
                                                &create_error);
    break;
  case DATA_BIND_FORMAT_XML:
    if (config->selection == DATA_BIND_STREAM_SELECT_ROOT)
      stream = data_bind_stream_xml_create(codec, config->type_name, output, &create_error);
    else if (config->selection == DATA_BIND_STREAM_SELECT_PATH_ALL)
      stream = data_bind_stream_xml_path_all_create(codec, config->type_name, config->path, output,
                                                    &create_error);
    break;
  case DATA_BIND_FORMAT_BINARY:
  default:
    break;
  }
  if (stream == NULL) {
    status = create_error.code != DATA_BIND_OK ? create_error.code : DATA_BIND_ERR_OOM;
    db_error_set(error, status, create_error.path, create_error.line, create_error.column, "%s",
                 create_error.message[0] != '\0' ? create_error.message
                                                  : "Failed to create configured stream");
    return status;
  }

  stream->error = error;
  db_error_clear(error);
  if (config->out_value == NULL) stream->out_value = &stream->internal_out_value;
  if (config->record_callback != NULL) {
    status = data_bind_stream_set_record_callback(stream, config->record_callback,
                                                  config->record_callback_user);
    if (status != DATA_BIND_OK) goto fail;
  }
  status = data_bind_stream_set_output_mode(stream, config->output_mode);
  if (status != DATA_BIND_OK) goto fail;
  status = data_bind_stream_set_limits(stream, &config->limits);
  if (status != DATA_BIND_OK) goto fail;
  if (config->size >= offsetof(DataBindStreamConfig, query_limits) +
                          sizeof(config->query_limits)) {
    status = data_bind_stream_set_query_limits(stream, &config->query_limits);
    if (status != DATA_BIND_OK) goto fail;
  }

  *out_stream = stream;
  return DATA_BIND_OK;

fail:
  data_bind_stream_destroy(stream);
  return status;
}

DataBindStatus data_bind_stream_set_record_callback(data_bind_stream_t *stream,
                                                    DataBindRecordFn callback, void *user_data) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL || callback == NULL || parser->started || parser->finished) {
    return db_error_set(parser != NULL ? parser->error : NULL, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_record_callback", -1, -1,
                        "Record callback must be set before first feed");
  }
  parser->record_callback = callback;
  parser->record_callback_user = user_data;
  parser->record_callback_index = 0;
  parser->record_callback_stopped = 0;
  parser->record_callback_failed = 0;
  db_error_clear(parser->error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_stream_set_output_mode(data_bind_stream_t *stream,
                                                DataBindStreamOutputMode mode) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL || parser->started || parser->finished ||
      (mode != DATA_BIND_STREAM_OUTPUT_RETAIN &&
       mode != DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY)) {
    return db_error_set(parser != NULL ? parser->error : NULL, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_output_mode", -1, -1,
                        "Valid stream output mode must be set before first feed");
  }
  parser->output_mode = mode;
  db_error_clear(parser->error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_stream_set_limits(data_bind_stream_t *stream,
                                           const DataBindStreamLimits *limits) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL || limits == NULL || limits->size < sizeof(*limits) || parser->started ||
      parser->finished || limits->max_input_bytes == 0 || limits->max_input_bytes == SIZE_MAX ||
      limits->max_record_bytes == 0 || limits->max_record_bytes == SIZE_MAX ||
      limits->max_field_bytes == 0 || limits->max_field_bytes == SIZE_MAX ||
      limits->max_result_count == 0 || limits->max_field_bytes > limits->max_record_bytes ||
      limits->max_record_bytes > limits->max_input_bytes) {
    return db_error_set(parser != NULL ? parser->error : NULL, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_limits", -1, -1,
                        "Valid stream limits must be set before first feed");
  }
  if (parser->format_state->xml_sax != NULL &&
      salts_xml_sax_parser_set_buffer_limit(parser->format_state->xml_sax, limits->max_input_bytes) != 0) {
    return db_error_set(parser->error, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_limits", -1, -1,
                        "XML parser limits cannot change after parsing starts");
  }
  if ((parser->stream_values != NULL &&
       !dbv_sequence_set_limit(parser->stream_values, limits->max_result_count)) ||
      (parser->csv_values != NULL &&
       !dbv_sequence_set_limit(parser->csv_values, limits->max_result_count))) {
    return db_error_set(parser->error, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_limits", -1, -1,
                        "Stream result storage cannot accept the requested limit");
  }
  parser->limits = *limits;
  parser->limits.size = sizeof(parser->limits);
  parser->limit_failed = 0;
  parser->format_state->stream_error[0] = '\0';
  db_error_clear(parser->error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_stream_set_query_limits(
    data_bind_stream_t *stream, const DataBindQueryLimits *limits) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL || limits == NULL || limits->size < sizeof(*limits) ||
      parser->started || parser->finished || limits->max_instructions == 0 ||
      limits->max_operands == 0 || limits->max_regexes == 0 ||
      limits->max_steps == 0) {
    return db_error_set(parser ? parser->error : NULL, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_query_limits", -1, -1,
                        "Valid query limits must be set before first feed");
  }
  parser->query_limits = *limits;
  parser->query_limits.size = sizeof(parser->query_limits);
  parser->query_diagnostic =
      (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  parser->query_limits_configured = 1;

  if (parser->format_state->json_path_program != NULL && parser->path_or_expr != NULL) {
    qvm_limits_t native_limits = db_query_native_limits(&parser->query_limits);
    qvm_diagnostic_t native_diagnostic = {0};
    json_path_program_t *verified = json_path_compile_ex(
        parser->path_or_expr, &native_limits, &native_diagnostic);
    db_query_diagnostic_copy(&parser->query_diagnostic, &native_diagnostic);
    if (verified == NULL) {
      DataBindStatus status = data_bind_query_failure_status(
          &parser->query_diagnostic);
      return db_error_set(parser->error, status, "jsonpath", -1, -1,
                          "JSONPath query limits rejected the program: %s",
                          parser->query_diagnostic.message[0]
                              ? parser->query_diagnostic.message
                              : "query VM failure");
    }
    json_path_program_free(verified);
    if (parser->format_state->json_path_stream != NULL &&
        (limits->max_instructions != QVM_DEFAULT_MAX_INSTRUCTIONS ||
         limits->max_operands != QVM_DEFAULT_MAX_OPERANDS ||
         limits->max_regexes != QVM_DEFAULT_MAX_REGEXES ||
         limits->max_steps != QVM_DEFAULT_MAX_STEPS)) {
      json_sax_parser_t *replacement = json_sax_parser_create(
          &DATA_BIND_JSON_SAX_VALIDATE_HANDLER, parser);
      if (replacement == NULL)
        return db_error_set(parser->error, DATA_BIND_ERR_OOM, "jsonpath", -1,
                            -1, "Out of memory applying JSONPath query limits");
      json_path_stream_destroy(parser->format_state->json_path_stream);
      parser->format_state->json_path_stream = NULL;
      parser->format_state->json_path_stream_mode = DATA_BIND_JSON_PATH_STREAM_NONE;
      data_bind_value_free(parser->stream_values);
      parser->stream_values = NULL;
      parser->format_state->json_sax = replacement;
    }
  }
  db_error_clear(parser->error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_stream_query_diagnostic(
    const data_bind_stream_t *stream, DataBindQueryDiagnostic *diagnostic) {
  const data_bind_stream_t *parser = (const data_bind_stream_t *)stream;
  size_t size;
  if (parser == NULL || diagnostic == NULL ||
      diagnostic->size < sizeof(*diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  size = diagnostic->size;
  *diagnostic = parser->query_diagnostic;
  diagnostic->size = size;
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_text_feed(data_bind_stream_t *parser, const char *data,
                                                 size_t len, DataBindError *error) {
  size_t needed;
  size_t new_cap;
  char *grown;
  DataBindStatus status;

  if (parser == NULL || parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_feed", -1, -1,
                        "Invalid stream parser feed state");
  }
  if (len == 0) return DATA_BIND_OK;
  if (data == NULL) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_feed", -1, -1,
                        "Invalid stream parser feed data");
  }

  status = data_bind_stream_sax_feed(parser, data, len, error);
  if (status != DATA_BIND_OK) return status;

  if (parser->format_state->json_path_stream != NULL ||
      (parser->format_state->json_stream_candidate && parser->format_state->json_stream_active) ||
      parser->format_state->xml_stream_candidate) {
    parser->started = 1;
    db_error_clear(error);
    return DATA_BIND_OK;
  }

  needed = parser->size + len;
  if (needed < parser->size) {
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                        "Stream input size overflow");
  }
  if (parser->capacity < needed + 1) {
    new_cap = parser->capacity == 0 ? 4096 : parser->capacity * 2;
    while (new_cap < needed + 1) {
      if (new_cap > (SIZE_MAX / 2)) {
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Stream input too large");
      }
      new_cap *= 2;
    }
    grown = (char *)realloc(parser->buffer, new_cap);
    if (grown == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory while extending stream buffer");
    }
    parser->buffer = grown;
    parser->capacity = new_cap;
  }
  memcpy(parser->buffer + parser->size, data, len);
  parser->size += len;
  parser->buffer[parser->size] = '\0';
  parser->started = 1;
  db_error_clear(error);
  return DATA_BIND_OK;
}

static void data_bind_stream_discard_results(data_bind_stream_t *parser) {
  if (parser == NULL) return;
  data_bind_value_free(parser->stream_values);
  parser->stream_values = NULL;
  data_bind_value_free(parser->csv_values);
  parser->csv_values = NULL;
  data_bind_value_free(parser->internal_out_value);
  parser->internal_out_value = NULL;
  if (parser->out_value != NULL) *parser->out_value = NULL;
}

DataBindStatus data_bind_stream_feed(data_bind_stream_t *stream, const void *data, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  DataBindStatus status;
  if (parser == NULL || parser->feed_fn == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (parser->canceled)
    return db_error_set(parser->error, DATA_BIND_ERR_CANCELED, "data_bind_stream_feed", -1, -1,
                        "Stream was canceled");
  if (parser->output_mode == DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY &&
      parser->record_callback == NULL) {
    return db_error_set(parser->error, DATA_BIND_ERR_INVALID_ARG, "stream.output_mode", -1, -1,
                        "Callback-only stream output requires a record callback");
  }
  if (parser->limit_failed)
    return db_error_set(parser->error, DATA_BIND_ERR_LIMIT, "stream.limit", -1, -1,
                        "Stream is in a failed resource-limit state");
  if (data_bind_stream_limit_exceeded(parser->total_input_bytes, len,
                                      parser->limits.max_input_bytes)) {
    parser->limit_failed = 1;
    return db_error_set(parser->error, DATA_BIND_ERR_LIMIT, "stream.input", -1, -1,
                        "Stream input exceeds byte limit of %zu",
                        parser->limits.max_input_bytes);
  }
  status = parser->feed_fn(parser, (const char *)data, len, parser->error);
  if (status == DATA_BIND_OK) {
    parser->total_input_bytes += len;
    parser->started = 1;
    db_error_clear(parser->error);
  } else if (status == DATA_BIND_ERR_CANCELED) {
    data_bind_stream_discard_results(parser);
  }
  return status;
}

DataBindStatus data_bind_stream_feed_file(data_bind_stream_t *stream, const char *file_path) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  DataBindError *error = parser ? parser->error : NULL;
  char *chunk = NULL;
  salts_file_t fd;
  DataBindStatus status;
  int close_rc;

  if (parser == NULL || file_path == NULL || file_path[0] == '\0') {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_feed_file", -1, -1,
                        "Invalid stream file feed arguments");
  }
  fd = salts_fs_open(file_path, SALTS_FS_O_RDONLY, 0);
  if (fd == SALTS_INVALID_FILE) {
    return db_error_set(error, DATA_BIND_ERR_IO, file_path, -1, -1,
                        "Failed to open stream input file");
  }

  chunk = (char *)malloc(DATA_BIND_FILE_STREAM_CHUNK_SIZE);
  if (chunk == NULL) {
    salts_fs_close(fd);
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed_file", -1, -1,
                        "Out of memory allocating stream file chunk");
  }

  status = DATA_BIND_OK;
  for (;;) {
    int nread = salts_fs_read(fd, chunk, DATA_BIND_FILE_STREAM_CHUNK_SIZE);
    if (nread < 0) {
      status = db_error_set(error, DATA_BIND_ERR_IO, file_path, -1, -1,
                            "Failed to read stream input file");
      break;
    }
    if (nread == 0) break;
    status = data_bind_stream_feed(parser, chunk, (size_t)nread);
    if (status != DATA_BIND_OK) break;
  }

  free(chunk);
  close_rc = salts_fs_close(fd);
  if (status == DATA_BIND_OK && close_rc != 0) {
    status = db_error_set(error, DATA_BIND_ERR_IO, file_path, -1, -1,
                          "Failed to close stream input file");
  }
  return status;
}

static DataBindStatus data_bind_stream_json_finish(data_bind_stream_t *parser,
                                                   DataBindValue **out_value,
                                                   DataBindError *error) {
  const char *path = parser ? parser->path_or_expr : NULL;
  DataBindStatus status = DATA_BIND_OK;

  if (parser == NULL || out_value == NULL || parser->codec == NULL || parser->type_name == NULL ||
      parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid stream parser finish state");
  }
  status = data_bind_stream_sax_finish(parser, error);
  if (status != DATA_BIND_OK) {
    parser->finished = 1;
    return status;
  }
  if (parser->format_state->json_path_stream != NULL ||
      (parser->format_state->json_stream_candidate && parser->format_state->json_stream_active) ||
      parser->format_state->xml_stream_candidate) {
    if (parser->format_state->json_path_stream != NULL &&
        parser->format_state->json_path_stream_mode == DATA_BIND_JSON_PATH_STREAM_FIRST &&
        parser->result_count == 0) {
      parser->finished = 1;
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                          "JSONPath selected no value for type: %s", parser->type_name);
    }
    *out_value = parser->stream_values;
    parser->stream_values = NULL;
    parser->finished = 1;
    db_error_clear(error);
    return DATA_BIND_OK;
  }
  if (!parser->started) {
    parser->buffer = (char *)realloc(parser->buffer, 1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream parser");
    }
    parser->buffer[0] = '\0';
    parser->size = 0;
    parser->capacity = 1;
  }
  if (parser->buffer == NULL) {
    parser->buffer = (char *)malloc(1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream parser");
    }
    parser->buffer[0] = '\0';
    parser->capacity = 1;
  }
  parser->buffer[parser->size] = '\0';

  status = parser->bind_fn(parser->codec, parser->type_name, parser->buffer, parser->size, path,
                           parser->query_limits_configured ? &parser->query_limits : NULL,
                           &parser->query_diagnostic, out_value, error);

  if (status == DATA_BIND_OK) {
    status = data_bind_stream_emit_result(parser, *out_value, error);
    if (status != DATA_BIND_OK) {
      data_bind_value_free(*out_value);
      *out_value = NULL;
    }
  }

  parser->finished = 1;
  return status;
}

static DataBindStatus data_bind_stream_xml_finish(data_bind_stream_t *parser,
                                                  DataBindValue **out_value, DataBindError *error) {
  const char *path = parser ? parser->path_or_expr : NULL;
  DataBindStatus status;
  if (parser == NULL || out_value == NULL || parser->codec == NULL || parser->type_name == NULL ||
      parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid stream finish state");
  }
  status = data_bind_stream_sax_finish(parser, error);
  if (status != DATA_BIND_OK) {
    parser->finished = 1;
    return status;
  }
  if (parser->format_state->xml_stream_candidate) {
    *out_value = parser->stream_values;
    parser->stream_values = NULL;
    parser->finished = 1;
    db_error_clear(error);
    return DATA_BIND_OK;
  }
  if (!parser->started) {
    parser->buffer = (char *)realloc(parser->buffer, 1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream");
    }
    parser->buffer[0] = '\0';
    parser->size = 0;
    parser->capacity = 1;
  }
  if (parser->buffer == NULL) {
    parser->buffer = (char *)malloc(1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream");
    }
    parser->buffer[0] = '\0';
    parser->capacity = 1;
  }
  parser->buffer[parser->size] = '\0';
  status = parser->bind_fn(parser->codec, parser->type_name, parser->buffer, parser->size, path,
                           parser->query_limits_configured ? &parser->query_limits : NULL,
                           &parser->query_diagnostic, out_value, error);
  if (status == DATA_BIND_OK) {
    status = data_bind_stream_emit_result(parser, *out_value, error);
    if (status != DATA_BIND_OK) {
      data_bind_value_free(*out_value);
      *out_value = NULL;
    }
  }
  parser->finished = 1;
  return status;
}

static DataBindStatus data_bind_stream_buffered_finish(data_bind_stream_t *parser,
                                                       DataBindValue **out_value,
                                                       DataBindError *error) {
  DataBindStatus status;
  if (parser == NULL || out_value == NULL || parser->codec == NULL || parser->type_name == NULL ||
      parser->bind_fn == NULL || parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid buffered stream finish state");
  }
  status = data_bind_stream_sax_finish(parser, error);
  if (status != DATA_BIND_OK) {
    parser->finished = 1;
    return status;
  }
  if (parser->buffer == NULL) {
    parser->buffer = (char *)malloc(1);
    if (parser->buffer == NULL) {
      parser->finished = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory finalizing buffered stream");
    }
    parser->buffer[0] = '\0';
    parser->capacity = 1;
  }
  status = parser->bind_fn(parser->codec, parser->type_name, parser->buffer, parser->size,
                           parser->path_or_expr,
                           parser->query_limits_configured ? &parser->query_limits : NULL,
                           &parser->query_diagnostic, out_value, error);
  if (status == DATA_BIND_OK) {
    status = data_bind_stream_emit_result(parser, *out_value, error);
    if (status != DATA_BIND_OK) {
      data_bind_value_free(*out_value);
      *out_value = NULL;
    }
  }
  parser->finished = 1;
  return status;
}

DataBindStatus data_bind_stream_finish(data_bind_stream_t *stream) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  DataBindValue *callback_value = NULL;
  DataBindStatus status;
  if (parser == NULL || parser->finish_fn == NULL || parser->out_value == NULL) {
    return DATA_BIND_ERR_INVALID_ARG;
  }
  if (parser->canceled) {
    data_bind_stream_discard_results(parser);
    return db_error_set(parser->error, DATA_BIND_ERR_CANCELED, "data_bind_stream_finish", -1, -1,
                        "Stream was canceled");
  }
  if (parser->output_mode == DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY &&
      parser->record_callback == NULL) {
    return db_error_set(parser->error, DATA_BIND_ERR_INVALID_ARG, "stream.output_mode", -1, -1,
                        "Callback-only stream output requires a record callback");
  }
  if (parser->limit_failed) {
    parser->finished = 1;
    return db_error_set(parser->error, DATA_BIND_ERR_LIMIT, "stream.limit", -1, -1,
                        "Stream is in a failed resource-limit state");
  }
  if (parser->output_mode == DATA_BIND_STREAM_OUTPUT_RETAIN) {
    return parser->finish_fn(parser, parser->out_value, parser->error);
  }
  status = parser->finish_fn(parser, &callback_value, parser->error);
  data_bind_value_free(callback_value);
  *parser->out_value = NULL;
  return status;
}

DataBindStatus data_bind_stream_cancel(data_bind_stream_t *stream) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (parser->canceled)
    return db_error_set(parser->error, DATA_BIND_ERR_CANCELED, "data_bind_stream_cancel", -1, -1,
                        "Stream was canceled");
  if (parser->finished)
    return db_error_set(parser->error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_cancel", -1,
                        -1, "Finished stream cannot be canceled");
  parser->canceled = 1;
  data_bind_stream_discard_results(parser);
  return db_error_set(parser->error, DATA_BIND_ERR_CANCELED, "data_bind_stream_cancel", -1, -1,
                      "Stream was canceled");
}

void data_bind_stream_destroy(data_bind_stream_t *stream) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL) return;
  free(parser->type_name);
  free(parser->path_or_expr);
  free(parser->buffer);
  data_bind_stream_format_state_cleanup(parser->format_state);
  data_bind_value_free(parser->stream_values);
  data_bind_value_free(parser->csv_values);
  data_bind_value_free(parser->internal_out_value);
  free(parser);
}

static DataBindStatus data_bind_json_root_to_value(DataBind *codec, const char *type_name,
                                                   json_value_t *root,
                                                   DataBindValue **out_value,
                                                   DataBindError *error) {
  DataBindValue *result;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || root == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid JSON bind arguments");
  result = bind_json_typed_value(codec->schema_root, type_name, root);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", "$");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSON bind failed for type: %s", type_name);
  }
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_parse_json_without_dynamic_identity(DataBind *codec, const char *type_name, const char *json,
                                    size_t len, DataBindValue **out_value, DataBindError *error) {
  json_value_t *root = NULL;
  DataBindStatus status;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid JSON bind arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((root = json_parse(json, len)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  status = data_bind_json_root_to_value(codec, type_name, root, out_value, error);
  (json_free(root), root = NULL);
  return status;
}

DataBindStatus data_bind_parse_json(DataBind *codec, const char *type_name, const char *json,
                                    size_t len, DataBindValue **out_value, DataBindError *error) {
  DataBindStatus status = data_bind_parse_json_without_dynamic_identity(
      codec, type_name, json, len, out_value, error);
  if (status != DATA_BIND_OK || out_value == NULL || *out_value == NULL) return status;
  {
    DataBindValue *value = *out_value;
    *out_value = NULL;
    return db_dynamic_publish_result(codec, type_name, 0, value, out_value,
                                     error, "json");
  }
}


DataBindStatus data_bind_parse_json_all(DataBind *codec, const char *type_name, const char *json,
                                        size_t len, DataBindValue **out_value,
                                        DataBindError *error) {
  json_value_t *root = NULL;
  DataBindValue *list;
  size_t i;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSON bind_all arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((root = json_parse(json, len)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL) {
    if (json_type(root) == JSON_ARRAY) {
      for (i = 0; i < json_array_size(root); i++) {
        DataBindValue *item =
            bind_json_typed_value(codec->schema_root, type_name, json_array_get(root, i));
        if (item == NULL) {
          data_bind_value_free(list);
          list = NULL;
          break;
        }
        if (dbv_collection_push(list, item) != DATA_BIND_OK) {
          data_bind_value_free(item);
          data_bind_value_free(list);
          list = NULL;
          break;
        }
      }
    } else {
      DataBindValue *item = bind_json_typed_value(codec->schema_root, type_name, root);
      if (item == NULL || dbv_collection_push(list, item) != DATA_BIND_OK) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        list = NULL;
      }
    }
  }
  (json_free(root), root = NULL);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", "$[]");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSON bind_all failed for type: %s", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 1, list, out_value, error,
                                   "json");
}

static DataBindStatus data_bind_query_failure_status(
    const DataBindQueryDiagnostic *diagnostic) {
  if (!diagnostic) return DATA_BIND_ERR_PARSE;
  if (diagnostic->status == DATA_BIND_QUERY_RESOURCE_LIMIT)
    return DATA_BIND_ERR_LIMIT;
  if (diagnostic->status == DATA_BIND_QUERY_NO_MEMORY)
    return DATA_BIND_ERR_OOM;
  if (diagnostic->status == DATA_BIND_QUERY_INVALID_ARGUMENT)
    return DATA_BIND_ERR_INVALID_ARG;
  return DATA_BIND_ERR_PARSE;
}

static DataBindStatus data_bind_parse_json_path_with_query(
    DataBind *codec, const char *type_name, const char *json, size_t len,
    const char *jsonpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error) {
  qvm_limits_t native_limits = db_query_native_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  json_value_t *root = NULL;
  json_value_t *selected;
  json_path_program_t *program = NULL;
  DataBindValue *result;
  char error_path[256];
  const char *path_error;
  if (jsonpath == NULL || jsonpath[0] == '\0')
    return data_bind_parse_json(codec, type_name, json, len, out_value, error);
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSONPath bind arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((root = json_parse(json, len)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  program = json_path_compile_ex(jsonpath, &native_limits, &native_diagnostic);
  db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
  if (program == NULL) {
    DataBindStatus query_status = data_bind_query_failure_status(query_diagnostic);
    path_error = json_path_get_error();
    (json_free(root), root = NULL);
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, query_status, error_path, -1, -1,
                        "JSONPath compile failed: %s",
                        query_diagnostic && query_diagnostic->message[0]
                            ? query_diagnostic->message
                            : (path_error ? path_error : "invalid path"));
  }
  selected = json_path_get_compiled_ex(root, program, &native_diagnostic);
  db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
  json_path_program_free(program);
  path_error = json_path_get_error();
  if (selected == NULL) {
    DataBindStatus query_status = data_bind_query_failure_status(query_diagnostic);
    (json_free(root), root = NULL);
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    if (query_diagnostic && query_diagnostic->status != DATA_BIND_QUERY_OK)
      return db_error_set(error, query_status, error_path, -1, -1,
                          "JSONPath query failed: %s",
                          query_diagnostic->message[0]
                              ? query_diagnostic->message
                              : "query VM failure");
    if (path_error != NULL)
      return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1,
                          "JSONPath parse failed: %s", path_error);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSONPath selected no value for type: %s", type_name);
  }
  result = bind_json_typed_value(codec->schema_root, type_name, selected);
  (json_free(root), root = NULL);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSONPath bind failed for type: %s", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 0, result, out_value,
                                   error, error_path);
}

DataBindStatus data_bind_parse_json_path(DataBind *codec, const char *type_name,
                                         const char *json, size_t len,
                                         const char *jsonpath,
                                         DataBindValue **out_value,
                                         DataBindError *error) {
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  return data_bind_parse_json_path_with_query(codec, type_name, json, len, jsonpath,
                                              &limits, &diagnostic, out_value, error);
}

static DataBindStatus data_bind_parse_json_path_all_with_query(
    DataBind *codec, const char *type_name, const char *json, size_t len,
    const char *jsonpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error) {
  qvm_limits_t native_limits = db_query_native_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  json_value_t *root = NULL;
  json_path_program_t *program = NULL;
  json_path_result_t *matches = NULL;
  DataBindValue *list;
  int program_compiled = 0;
  size_t i;
  char error_path[256];
  const char *path_error;
  if (jsonpath == NULL || jsonpath[0] == '\0')
    return data_bind_parse_json_all(codec, type_name, json, len, out_value, error);
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSONPath bind_all arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((root = json_parse(json, len)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  program = json_path_compile_ex(jsonpath, &native_limits, &native_diagnostic);
  db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
  if (program != NULL) {
    program_compiled = 1;
    matches = json_path_query_compiled_ex(root, program, &native_diagnostic);
    db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
  }
  json_path_program_free(program);
  path_error = json_path_get_error();
  if (matches == NULL && (!program_compiled || path_error != NULL)) {
    DataBindStatus query_status = data_bind_query_failure_status(query_diagnostic);
    (json_free(root), root = NULL);
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, query_status, error_path, -1, -1,
                        "JSONPath query failed: %s",
                        query_diagnostic && query_diagnostic->message[0]
                            ? query_diagnostic->message
                            : (path_error ? path_error : "invalid path"));
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list == NULL) {
    if (matches != NULL) json_path_result_free(matches);
    (json_free(root), root = NULL);
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_OOM, error_path, -1, -1,
                        "Out of memory binding JSONPath result");
  }
  if (matches != NULL) {
    DataBindStatus failure = DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < json_path_result_size(matches); i++) {
      json_value_t *matched = json_path_result_get(matches, i);
      DataBindValue *item = bind_json_typed_value(codec->schema_root, type_name, matched);
      if (item == NULL) {
        data_bind_value_free(list);
        list = NULL;
        break;
      }
      if (dbv_collection_push(list, item) != DATA_BIND_OK) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        list = NULL;
        failure = DATA_BIND_ERR_OOM;
        break;
      }
    }
    if (list == NULL) {
      json_path_result_free(matches);
      (json_free(root), root = NULL);
      db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
      if (failure == DATA_BIND_ERR_OOM)
        return db_error_set(error, DATA_BIND_ERR_OOM, error_path, -1, -1,
                            "Out of memory binding JSONPath result");
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                          "JSONPath bind_all failed for type: %s", type_name);
    }
  }
  if (matches != NULL) json_path_result_free(matches);
  (json_free(root), root = NULL);
  return db_dynamic_publish_result(codec, type_name, 1, list, out_value, error,
                                   error_path);
}

DataBindStatus data_bind_parse_json_path_all(DataBind *codec, const char *type_name,
                                             const char *json, size_t len,
                                             const char *jsonpath,
                                             DataBindValue **out_value,
                                             DataBindError *error) {
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  return data_bind_parse_json_path_all_with_query(
      codec, type_name, json, len, jsonpath, &limits, &diagnostic, out_value,
      error);
}

static DataBindStatus data_bind_parse_yaml_selected(DataBind *codec, const char *type_name,
                                                    const char *yaml, size_t len,
                                                    const char *yamlpath, int bind_all,
                                                    const DataBindQueryLimits *query_limits,
                                                    DataBindQueryDiagnostic *query_diagnostic,
                                                    DataBindValue **out_value,
                                                    DataBindError *error) {
  cyaml_doc_t *doc = NULL;
  cyaml_path_result_t matches = {0};
  int has_path = yamlpath != NULL && yamlpath[0] != '\0';
  cyaml_node_t *root;
  DataBindValue *result = NULL;
  char error_path[256];
  size_t count = 0;
  size_t i;

  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || yaml == NULL ||
      out_value == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid YAML bind arguments");
  }
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((doc = cyaml_parse(yaml, len, NULL, NULL)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "yaml", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "YAML parse failed");
  }

  root = cyaml_root(doc);
  if (has_path) {
    qvm_limits_t native_limits = db_query_native_limits(query_limits);
    qvm_diagnostic_t native_diagnostic = {0};
    matches = cyaml_path_query_ex(doc, NULL, yamlpath, &native_limits, &native_diagnostic);
    db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
    if (matches.error != NULL || native_diagnostic.status != QVM_STATUS_OK) {
      uint32_t error_pos = matches.error_pos;
      char path_message[DATA_BIND_QUERY_DIAGNOSTIC_MESSAGE_CAPACITY];
      snprintf(path_message, sizeof(path_message), "%s", matches.error ? matches.error :
               (native_diagnostic.message ? native_diagnostic.message : "query failed"));
      cyaml_path_result_free(&matches);
      cyaml_free(doc);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, data_bind_query_failure_status(query_diagnostic), error_path, -1,
                          error_pos <= INT_MAX ? (int)error_pos : -1,
                          "YPATH parse failed: %s", path_message);
    }
    count = matches.count;
  } else if (bind_all && root != NULL && root->type == CYAML_SEQ) {
    count = cyaml_seq_len(root);
  } else if (root != NULL) {
    count = 1;
  }

  if (count == 0) {
    cyaml_path_result_free(&matches);
    (cyaml_free(doc), doc = NULL);
    db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "YAML selection matched no values");
  }

  if (bind_all) {
    result = dbv_new(DATA_BIND_VALUE_LIST);
    if (result == NULL) {
      cyaml_path_result_free(&matches);
      (cyaml_free(doc), doc = NULL);
      return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                          "Out of memory creating YAML result list");
    }
  }

  for (i = 0; i < (bind_all ? count : 1); i++) {
    cyaml_node_t *node;
    json_value_t *json_value;
    DataBindValue *bound;
    if (has_path) node = matches.nodes[i];
    else if (bind_all && root != NULL && root->type == CYAML_SEQ)
      node = cyaml_seq_get(root, i);
    else node = root;

    json_value = json_value_from_cyaml_node(doc, node);
    if (json_value == NULL) {
      data_bind_value_free(result);
      cyaml_path_result_free(&matches);
      (cyaml_free(doc), doc = NULL);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                          "YAML value cannot be represented as JSON-compatible data");
    }
    bound = bind_json_typed_value(codec->schema_root, type_name, json_value);
    (json_free(json_value), json_value = NULL);
    if (bound == NULL) {
      data_bind_value_free(result);
      cyaml_path_result_free(&matches);
      (cyaml_free(doc), doc = NULL);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                          "YAML bind failed for type: %s", type_name);
    }
    if (!bind_all) {
      result = bound;
    } else if (dbv_collection_push(result, bound) != DATA_BIND_OK) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      cyaml_path_result_free(&matches);
      (cyaml_free(doc), doc = NULL);
      return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                          "Out of memory appending YAML result");
    }
  }

  cyaml_path_result_free(&matches);
  (cyaml_free(doc), doc = NULL);
  return db_dynamic_publish_result(codec, type_name, bind_all, result, out_value,
                                   error, "yaml");
}

DataBindStatus data_bind_parse_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                    size_t len, DataBindValue **out_value, DataBindError *error) {
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, NULL, 0, NULL, NULL,
                                       out_value, error);
}

DataBindStatus data_bind_parse_yaml_all(DataBind *codec, const char *type_name, const char *yaml,
                                        size_t len, DataBindValue **out_value,
                                        DataBindError *error) {
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, NULL, 1, NULL, NULL,
                                       out_value, error);
}

DataBindStatus data_bind_parse_yaml_path(DataBind *codec, const char *type_name, const char *yaml,
                                         size_t len, const char *yamlpath,
                                         DataBindValue **out_value, DataBindError *error) {
  if (yamlpath == NULL || yamlpath[0] == '\0')
    return data_bind_parse_yaml(codec, type_name, yaml, len, out_value, error);
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, yamlpath, 0, NULL, NULL,
                                       out_value, error);
}

DataBindStatus data_bind_parse_yaml_path_all(DataBind *codec, const char *type_name,
                                             const char *yaml, size_t len, const char *yamlpath,
                                             DataBindValue **out_value, DataBindError *error) {
  if (yamlpath == NULL || yamlpath[0] == '\0')
    return data_bind_parse_yaml_all(codec, type_name, yaml, len, out_value, error);
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, yamlpath, 1, NULL, NULL,
                                       out_value, error);
}

DataBindStatus data_bind_parse_csv(DataBind *codec, const char *type_name, const char *csv,
                                   size_t len, size_t row, DataBindValue **out_value,
                                   DataBindError *error) {
  csv_doc_t *doc = NULL;
  data_bind_csv_headers_t headers = {0};
  csv_options_t opts = {true, ',', '"', true};
  DataBindValue *result = NULL;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid CSV bind arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((doc = csv_parse_opts(csv, len, &opts)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSV parse failed");
  }
  if (csv_parse_header_names(csv, len, &headers))
    result = bind_csv_typed_value(codec->schema_root, type_name, doc, row, &headers, "");
  csv_headers_free(&headers);
  (csv_free(doc), doc = NULL);
  if (result == NULL) {
    snprintf(error_path, sizeof(error_path), "csv: row %zu", row);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, (int)row, -1,
                        "CSV bind failed for type: %s", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 0, result, out_value,
                                   error, error_path);
}

DataBindStatus data_bind_parse_csv_all(DataBind *codec, const char *type_name, const char *csv,
                                       size_t len, DataBindValue **out_value,
                                       DataBindError *error) {
  csv_doc_t *doc = NULL;
  data_bind_csv_headers_t headers = {0};
  csv_options_t opts = {true, ',', '"', true};
  DataBindValue *list = NULL;
  size_t row;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid CSV bind_all arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((doc = csv_parse_opts(csv, len, &opts)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSV parse failed");
  }
  if (csv_parse_header_names(csv, len, &headers)) {
    list = dbv_new(DATA_BIND_VALUE_LIST);
    if (list != NULL) {
      for (row = 0; row < csv_row_count(doc); row++) {
        DataBindValue *item =
            bind_csv_typed_value(codec->schema_root, type_name, doc, row, &headers, "");
        if (item == NULL) {
          data_bind_value_free(list);
          list = NULL;
          break;
        }
        if (dbv_collection_push(list, item) != DATA_BIND_OK) {
          data_bind_value_free(item);
          data_bind_value_free(list);
          list = NULL;
          break;
        }
      }
    }
  }
  csv_headers_free(&headers);
  (csv_free(doc), doc = NULL);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", "multiple rows");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "CSV bind_all failed for type: %s", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 1, list, out_value, error,
                                   "csv");
}

static DataBindStatus data_bind_parse_csv_path_with_query(
    DataBind *codec, const char *type_name, const char *csv, size_t len,
    const char *csvpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error) {
  qvm_limits_t native_limits = db_query_native_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  csv_doc_t *bind_doc = NULL;
  csv_doc_t *filter_doc = NULL;
  dsv_filter_t *filter = NULL;
  data_bind_csv_headers_t headers = {0};
  csv_options_t bind_opts = {true, ',', '"', true};
  csv_options_t filter_opts = {false, ',', '"', true};
  DataBindValue *list = NULL;
  size_t raw_row;
  char error_path[256];
  DataBindStatus failure = DATA_BIND_OK;
  const char *failure_msg = NULL;
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL ||
      csvpath == NULL || csvpath[0] == '\0' || out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid CSVPath bind arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "csv", csvpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if ((bind_doc = csv_parse_opts(csv, len, &bind_opts)) == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSV parse failed");
  }
  if ((filter_doc = csv_parse_opts(csv, len, &filter_opts)) == NULL) {
    (csv_free(bind_doc), bind_doc = NULL);
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSVPath parse failed");
  }
  if (!csv_parse_header_names(csv, len, &headers)) {
    (csv_free(filter_doc), filter_doc = NULL);
    (csv_free(bind_doc), bind_doc = NULL);
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "CSV header parse failed");
  }
  filter = dsv_filter_create(filter_doc, 0);
  if (filter == NULL ||
      !dsv_filter_compile_ex(filter, csvpath, &native_limits, &native_diagnostic)) {
    db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
    const char *filter_error = dsv_filter_error(filter);
    char filter_msg[128];
    snprintf(filter_msg, sizeof(filter_msg), "%s",
             filter_error != NULL && filter_error[0] != '\0' ? filter_error : "invalid filter");
    if (filter != NULL) dsv_filter_destroy(filter);
    csv_headers_free(&headers);
    (csv_free(filter_doc), filter_doc = NULL);
    (csv_free(bind_doc), bind_doc = NULL);
    db_error_format_path(error_path, sizeof(error_path), "csv", csvpath);
    return db_error_set(error, data_bind_query_failure_status(query_diagnostic),
                        error_path, -1, -1,
                        "CSVPath compile failed: %s", filter_msg);
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL) {
    db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
    for (raw_row = 1; raw_row < csv_row_count(filter_doc); raw_row++) {
      int match = dsv_filter_check_row(filter, raw_row);
      if (match < 0) {
        const qvm_diagnostic_t *filter_diagnostic = dsv_filter_qvm_diagnostic(filter);
        qvm_status_t query_status = filter_diagnostic ? filter_diagnostic->status
                                                    : QVM_STATUS_INVALID_ARGUMENT;
        db_query_diagnostic_copy(query_diagnostic, filter_diagnostic);
        data_bind_value_free(list);
        list = NULL;
        failure = query_status == QVM_STATUS_RESOURCE_LIMIT ? DATA_BIND_ERR_LIMIT
                                                             : DATA_BIND_ERR_PARSE;
        failure_msg = "CSVPath evaluation failed";
        break;
      }
      if (match) {
        size_t bind_row = raw_row - 1;
        DataBindValue *item =
            bind_csv_typed_value(codec->schema_root, type_name, bind_doc, bind_row, &headers, "");
        if (item == NULL) {
          data_bind_value_free(list);
          list = NULL;
          failure = DATA_BIND_ERR_TYPE_MISMATCH;
          failure_msg = "CSVPath row bind failed";
          break;
        }
        if (item != NULL) {
          if (dbv_collection_push(list, item) != DATA_BIND_OK) {
            data_bind_value_free(item);
            data_bind_value_free(list);
            list = NULL;
            failure = DATA_BIND_ERR_OOM;
            failure_msg = "Out of memory binding CSVPath rows";
            break;
          }
        }
      }
    }
  }
  dsv_filter_destroy(filter);
  csv_headers_free(&headers);
  (csv_free(filter_doc), filter_doc = NULL);
  (csv_free(bind_doc), bind_doc = NULL);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", csvpath);
    return db_error_set(error, failure != DATA_BIND_OK ? failure : DATA_BIND_ERR_TYPE_MISMATCH,
                        error_path, -1, -1, "%s for type: %s",
                        failure_msg != NULL ? failure_msg : "CSVPath bind failed", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 1, list, out_value, error,
                                   error_path);
}

DataBindStatus data_bind_parse_csv_path(DataBind *codec, const char *type_name,
                                        const char *csv, size_t len,
                                        const char *csvpath,
                                        DataBindValue **out_value,
                                        DataBindError *error) {
  return data_bind_parse_csv_path_with_query(codec, type_name, csv, len, csvpath,
                                             NULL, NULL, out_value, error);
}

DataBindStatus data_bind_parse_xml(DataBind *codec, const char *type_name, const char *xml,
                                   size_t len, DataBindValue **out_value, DataBindError *error) {
  salts_xml_document doc = {0};
  DataBindValue *result = NULL;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || xml == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid XML bind arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (salts_xml_parse(&doc, xml, len, NULL, NULL) != SALTS_XML_OK) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "XML parse failed");
  }
  result = bind_xml_typed_value(codec->schema_root, type_name, &doc, "/*");
  salts_xml_document_destroy(&doc);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "xml", "/*");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "XML bind failed for type: %s", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 0, result, out_value,
                                   error, error_path);
}

static DataBindStatus data_bind_parse_xml_path_all_with_query(
    DataBind *codec, const char *type_name, const char *xml, size_t len,
    const char *xmlpath, const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic, DataBindValue **out_value,
    DataBindError *error) {
  qvm_limits_t native_limits = db_query_native_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  salts_xml_document doc = {0};
  DataBindValue *list = NULL;
  char error_path[256];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || xml == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid XML bind_all arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (salts_xml_parse(&doc, xml, len, NULL, NULL) != SALTS_XML_OK) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "XML parse failed");
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL) {
    if (xmlpath == NULL || xmlpath[0] == '\0') {
      DataBindValue *item = bind_xml_typed_value(codec->schema_root, type_name, &doc, "/*");
      if (item == NULL || dbv_collection_push(list, item) != DATA_BIND_OK) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        list = NULL;
      }
    } else {
      salts_xml_node_list nodes = {0};
      size_t index = 0;
      qvm_status_t query_status = salts_xml_document_xpath_query(
          &doc, xmlpath, &nodes, &native_limits, &native_diagnostic);
      db_query_diagnostic_copy(query_diagnostic, &native_diagnostic);
      if (query_status != QVM_STATUS_OK) {
        salts_xml_node_list_destroy(&nodes);
        data_bind_value_free(list);
        salts_xml_document_destroy(&doc);
        db_error_format_path(error_path, sizeof(error_path), "xml", xmlpath);
        return db_error_set(error, data_bind_query_failure_status(query_diagnostic),
                            error_path, -1, -1, "XPath query failed: %s",
                            query_diagnostic && query_diagnostic->message[0]
                                ? query_diagnostic->message
                                : "query VM failure");
      }
      for (index = 0; index < nodes.size; index++) {
        char item_path[320];
        DataBindValue *item;
        if (snprintf(item_path, sizeof(item_path), "%s[%zu]", xmlpath, index + 1) >=
            (int)sizeof(item_path)) {
          data_bind_value_free(list);
          list = NULL;
          break;
        }
        item = bind_xml_typed_value(codec->schema_root, type_name, &doc, item_path);
        if (item == NULL) {
          data_bind_value_free(list);
          list = NULL;
          break;
        }
        if (dbv_collection_push(list, item) != DATA_BIND_OK) {
          data_bind_value_free(item);
          data_bind_value_free(list);
          list = NULL;
          break;
        }
      }
      salts_xml_node_list_destroy(&nodes);
    }
  }
  salts_xml_document_destroy(&doc);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "xml",
                         xmlpath != NULL && xmlpath[0] != '\0' ? xmlpath : "/*");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "XML bind_all failed for type: %s", type_name);
  }
  return db_dynamic_publish_result(codec, type_name, 1, list, out_value, error,
                                   error_path);
}

DataBindStatus data_bind_parse_xml_path_all(DataBind *codec, const char *type_name,
                                            const char *xml, size_t len,
                                            const char *xmlpath,
                                            DataBindValue **out_value,
                                            DataBindError *error) {
  return data_bind_parse_xml_path_all_with_query(codec, type_name, xml, len,
                                                 xmlpath, NULL, NULL, out_value,
                                                 error);
}

DataBindStatus data_bind_validate_json(DataBind *codec, const char *type_name, const char *json,
                                       size_t len, DataBindError *error) {
  json_value_t *root = NULL;
  size_t i;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSON validate arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  }
  if ((root = json_parse(json, len)) == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "JSON parse failed");
  }
  if (json_type(root) == JSON_ARRAY) {
    for (i = 0; i < json_array_size(root); i++) {
      DataBindValue *item =
          bind_json_typed_value(codec->schema_root, type_name, json_array_get(root, i));
      if (item == NULL) {
        (json_free(root), root = NULL);
        return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                              "JSON validation failed for type: %s", type_name);
      }
      data_bind_value_free(item);
    }
  } else {
    DataBindValue *item = bind_json_typed_value(codec->schema_root, type_name, root);
    if (item == NULL) {
      (json_free(root), root = NULL);
      return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "JSON validation failed for type: %s", type_name);
    }
    data_bind_value_free(item);
  }
  (json_free(root), root = NULL);
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_validate_json_path(DataBind *codec, const char *type_name,
                                            const char *json, size_t len, const char *jsonpath,
                                            DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (jsonpath == NULL || jsonpath[0] == '\0')
    return data_bind_validate_json(codec, type_name, json, len, error);
  status = data_bind_parse_json_path(codec, type_name, json, len, jsonpath, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                       size_t len, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status = data_bind_parse_yaml_all(codec, type_name, yaml, len, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_yaml_path(DataBind *codec, const char *type_name,
                                            const char *yaml, size_t len, const char *yamlpath,
                                            DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (yamlpath == NULL || yamlpath[0] == '\0')
    return data_bind_validate_yaml(codec, type_name, yaml, len, error);
  status = data_bind_parse_yaml_path(codec, type_name, yaml, len, yamlpath, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_csv(DataBind *codec, const char *type_name, const char *csv,
                                      size_t len, DataBindError *error) {
  csv_doc_t *doc = NULL;
  data_bind_csv_headers_t headers = {0};
  csv_options_t opts = {true, ',', '"', true};
  size_t row;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid CSV validate arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  }
  if ((doc = csv_parse_opts(csv, len, &opts)) == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "CSV parse failed");
  }
  if (!csv_parse_header_names(csv, len, &headers)) {
    (csv_free(doc), doc = NULL);
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "CSV header parse failed");
  }
  for (row = 0; row < csv_row_count(doc); row++) {
    DataBindValue *item =
        bind_csv_typed_value(codec->schema_root, type_name, doc, row, &headers, "");
    if (item == NULL) {
      csv_headers_free(&headers);
      (csv_free(doc), doc = NULL);
      return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "CSV validation failed for type: %s", type_name);
    }
    data_bind_value_free(item);
  }
  csv_headers_free(&headers);
  (csv_free(doc), doc = NULL);
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_validate_csv_path(DataBind *codec, const char *type_name, const char *csv,
                                           size_t len, const char *csvpath, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status =
      data_bind_parse_csv_path(codec, type_name, csv, len, csvpath, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_xml_path(DataBind *codec, const char *type_name, const char *xml,
                                           size_t len, const char *xmlpath, DataBindError *error) {
  salts_xml_document doc = {0};
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || xml == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid XML validate arguments");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  }
  if (salts_xml_parse(&doc, xml, len, NULL, NULL) != SALTS_XML_OK) {
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "XML parse failed");
  }
  if (xmlpath == NULL || xmlpath[0] == '\0') {
    DataBindValue *item = bind_xml_typed_value(codec->schema_root, type_name, &doc, "/*");
    if (item == NULL) {
      salts_xml_document_destroy(&doc);
      return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "XML validation failed for type: %s", type_name);
    }
    data_bind_value_free(item);
  } else {
    salts_xml_node_list nodes = {0};
    size_t index;
    if (salts_xml_document_xpath_query(&doc, xmlpath, &nodes, NULL, NULL) != QVM_STATUS_OK) {
      salts_xml_node_list_destroy(&nodes);
      salts_xml_document_destroy(&doc);
      return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "Invalid XMLPath expression");
    }
    for (index = 0; index < nodes.size; index++) {
      char item_path[320];
      DataBindValue *item;
      if (snprintf(item_path, sizeof(item_path), "%s[%zu]", xmlpath, index + 1) >=
          (int)sizeof(item_path)) {
        salts_xml_node_list_destroy(&nodes);
        salts_xml_document_destroy(&doc);
        return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                              "XML validation path is too long");
      }
      item = bind_xml_typed_value(codec->schema_root, type_name, &doc, item_path);
      if (item == NULL) {
        salts_xml_node_list_destroy(&nodes);
        salts_xml_document_destroy(&doc);
        return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                              "XML validation failed for type: %s", type_name);
      }
      data_bind_value_free(item);
    }
    salts_xml_node_list_destroy(&nodes);
  }
  salts_xml_document_destroy(&doc);
  db_error_clear(error);
  return DATA_BIND_OK;
}

#define DATA_BIND_JSON_MAX_DEPTH 64u

static DataBindStatus data_bind_object_take(
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE], const char *type_name,
    DataBindValue *value, DataBindObject **out_object, DataBindError *error) {
  DataBindObject *object;
  size_t type_len;

  if (schema_fingerprint == NULL || out_object == NULL || type_name == NULL || value == NULL) {
    data_bind_value_free(value);
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object arguments");
  }
  *out_object = NULL;
  type_len = strlen(type_name);
  object = (DataBindObject *)calloc(1, sizeof(*object));
  if (object != NULL) {
    object->type_name = (char *)malloc(type_len + 1);
    if (object->type_name != NULL) {
      memcpy(object->type_name, type_name, type_len + 1);
      object->value = value;
      memcpy(object->schema_fingerprint, schema_fingerprint,
             sizeof(object->schema_fingerprint));
      *out_object = object;
      db_error_clear(error);
      return DATA_BIND_OK;
    }
  }
  free(object);
  data_bind_value_free(value);
  return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1,
                      "Out of memory creating DataBind object");
}

DataBindStatus data_bind_object_from_json(DataBind *codec, const char *type_name, const char *json,
                                          size_t len, DataBindObject **out_object,
                                          DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_json(codec, type_name, json, len, &value, error);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_object_from_json_value(DataBind *codec, const char *type_name,
                                                json_value_t *json,
                                                DataBindObject **out_object,
                                                DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  char error_path[128];
  if (out_object != NULL) *out_object = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  status = data_bind_json_root_to_value(codec, type_name, json, &value, error);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_object_from_bin(DataBind *codec, const char *type_name,
                                         const uint8_t *data, size_t len,
                                         DataBindObject **out_object, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse(codec, type_name, data, len, &value, error);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_record_from_bin(DataBind *codec, const char *type_name,
                                         const uint8_t *data, size_t len,
                                         DataBindRecord **out_object, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind Record output");
  status = data_bind_parse_record_v1(codec, type_name, data, len, &value, error);
  if (status != DATA_BIND_OK) return status;
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT) {
    data_bind_value_free(value);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, type_name, -1, -1,
                        "Record root must be an object value");
  }
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_object_from_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                          size_t len, DataBindObject **out_object,
                                          DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_yaml(codec, type_name, yaml, len, &value, error);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_object_from_xml(DataBind *codec, const char *type_name, const char *xml,
                                         size_t len, DataBindObject **out_object,
                                         DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_xml(codec, type_name, xml, len, &value, error);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_object_from_csv(DataBind *codec, const char *type_name, const char *csv,
                                         size_t len, size_t row, DataBindObject **out_object,
                                         DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_csv(codec, type_name, csv, len, row, &value, error);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(codec->schema_fingerprint, type_name, value, out_object, error);
}

DataBindStatus data_bind_object_clone(const DataBindObject *object, DataBindObject **out_object) {
  DataBindValue *value = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (object == NULL || out_object == NULL) return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_value_clone(object->value, &value);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(object->schema_fingerprint, object->type_name, value, out_object,
                               &error);
}

const char *data_bind_object_type_name(const DataBindObject *object) {
  return object != NULL ? object->type_name : NULL;
}

const DataBindValue *data_bind_object_value(const DataBindObject *object) {
  return object != NULL ? object->value : NULL;
}

DataBindStatus data_bind_object_serialize_bin_into(DataBind *codec, const DataBindObject *object,
                                                   uint8_t *output, size_t capacity,
                                                   size_t *out_len, DataBindError *error) {
  emit_field_array_t fields = {0};
  data_bind_binary_writer_t writer;
  DataBindStatus status;
  size_t required = 0;
  if (out_len != NULL) *out_len = 0;
  if (out_len == NULL || (output == NULL && capacity != 0))
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary output buffer arguments");
  status = db_binary_plan(codec, object, &fields, error);
  if (status != DATA_BIND_OK) return status;
  status = db_binary_measure(&fields, object->value, &required, error);
  if (status != DATA_BIND_OK) goto cleanup;
  *out_len = required;
  if (capacity < required) {
    status = db_error_set(error, DATA_BIND_ERR_BUFFER_TOO_SMALL, "binary", -1, -1,
                          "Binary output buffer is too small");
    goto cleanup;
  }
  writer.data = output;
  writer.capacity = capacity;
  writer.offset = 0;
  writer.error = error;
  status = db_binary_write_fields(&writer, &fields, object->value);
  if (status == DATA_BIND_OK) db_error_clear(error);

cleanup:
  emit_field_array_free(&fields);
  return status;
}

DataBindStatus data_bind_object_serialize_bin(DataBind *codec, const DataBindObject *object,
                                              uint8_t **out_bin, size_t *out_len,
                                              DataBindError *error) {
  emit_field_array_t fields = {0};
  data_bind_binary_writer_t writer;
  DataBindStatus status;
  uint8_t *data = NULL;
  size_t required = 0;
  if (out_bin != NULL) *out_bin = NULL;
  if (out_len != NULL) *out_len = 0;
  if (out_bin == NULL || out_len == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary output arguments");
  status = db_binary_plan(codec, object, &fields, error);
  if (status != DATA_BIND_OK) return status;
  status = db_binary_measure(&fields, object->value, &required, error);
  if (status != DATA_BIND_OK) goto cleanup;
  data = (uint8_t *)malloc(required != 0 ? required : 1);
  if (data == NULL) {
    status = db_error_set(error, DATA_BIND_ERR_OOM, object->type_name, -1, -1,
                          "Out of memory serializing binary object");
    goto cleanup;
  }
  writer.data = data;
  writer.capacity = required;
  writer.offset = 0;
  writer.error = error;
  status = db_binary_write_fields(&writer, &fields, object->value);
  if (status == DATA_BIND_OK) {
    *out_bin = data;
    *out_len = required;
    data = NULL;
    db_error_clear(error);
  }

cleanup:
  free(data);
  emit_field_array_free(&fields);
  return status;
}

static DataBindValue *object_field_value_mutable(DataBindValue *object, const char *name,
                                                 db_field_slot_t **out_field) {
  size_t i;
  if (out_field != NULL) *out_field = NULL;
  if (object == NULL || object->kind != DATA_BIND_VALUE_OBJECT || name == NULL) return NULL;
  for (i = 0; i < vec_size(&object->data.object.fields); ++i) {
    db_field_slot_t *field = (db_field_slot_t *)vec_at(&object->data.object.fields, i);
    if (field == NULL || field->name == NULL) return NULL;
    if (strcmp(field->name, name) == 0) {
      if (out_field != NULL) *out_field = field;
      return field->value;
    }
  }
  return NULL;
}

static DataBindStatus apply_mapped_names(Node *schema_root, const char *type_name,
                                         DataBindValue *value, unsigned depth,
                                         DataBindError *error) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node = find_union_record(schema_root, type_name);
  Node *fields;
  size_t i;
  if (value == NULL || (record == NULL && union_node == NULL)) return DATA_BIND_OK;
  if (depth > DATA_BIND_JSON_MAX_DEPTH)
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, type_name, -1, -1,
                        "Mapped serialization nesting limit exceeded");
  if (value->kind != DATA_BIND_VALUE_OBJECT)
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, type_name, -1, -1,
                        "Mapped serialization expected an object");
  if (union_node != NULL) {
    db_field_slot_t *owned_field;
    Node *variant;
    const char *mapped;
    const char *variant_type;
    DataBindStatus status = DATA_BIND_OK;
    if (vec_size(&value->data.object.fields) != 1u)
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, type_name, -1, -1,
                          "Mapped serialization expected one union variant");
    owned_field = (db_field_slot_t *)vec_at(&value->data.object.fields, 0u);
    if (owned_field == NULL || owned_field->name == NULL || owned_field->value == NULL)
      return db_error_set(error, DATA_BIND_ERR_RUNTIME, type_name, -1, -1,
                          "Mapped serialization found invalid object storage");
    variant = union_variant(union_node, owned_field->name);
    if (variant == NULL)
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, type_name, -1, -1,
                          "Mapped serialization found an unknown union variant");
    mapped = field_binding_name(variant);
    variant_type = get_string_val(find_child(variant, "type"));
    if (variant_type != NULL &&
        (find_data_record(schema_root, variant_type) != NULL ||
         find_union_record(schema_root, variant_type) != NULL))
      status = apply_mapped_names(schema_root, variant_type, owned_field->value, depth + 1u, error);
    if (status != DATA_BIND_OK) return status;
    if (mapped != NULL && strcmp(mapped, owned_field->name) != 0) {
      char *copy = dbv_strdup(mapped);
      if (copy == NULL)
        return db_error_set(error, DATA_BIND_ERR_OOM, owned_field->name, -1, -1,
                            "Out of memory applying union variant mapping");
      free(owned_field->name);
      owned_field->name = copy;
    }
    return DATA_BIND_OK;
  }
  fields = fields_node_for_record(record);
  if (fields == NULL) return DATA_BIND_OK;
  for (i = 0; i < fields->data.list.count; ++i) {
    Node *schema_field = fields->data.list.items[i];
    const char *canonical = get_string_val(find_child(schema_field, "name"));
    const char *mapped = field_binding_name(schema_field);
    const char *nested_type = NULL;
    db_field_slot_t *owned_field = NULL;
    DataBindValue *child = object_field_value_mutable(value, canonical, &owned_field);
    DataBindStatus status = DATA_BIND_OK;
    size_t j;
    if (canonical == NULL || child == NULL || owned_field == NULL) continue;
    if (field_flag(schema_field, "is_group_field"))
      nested_type = get_string_val(find_child(schema_field, "group_type"));
    else if (field_flag(schema_field, "is_map"))
      nested_type = get_string_val(find_child(schema_field, "value_type"));
    else if (field_flag(schema_field, "is_collection"))
      nested_type = get_string_val(find_child(schema_field, "inner_type"));
    else
      nested_type = get_string_val(find_child(schema_field, "type"));

    if (nested_type != NULL &&
        (find_data_record(schema_root, nested_type) != NULL ||
         find_union_record(schema_root, nested_type) != NULL)) {
      if (child->kind == DATA_BIND_VALUE_LIST || child->kind == DATA_BIND_VALUE_SET) {
        const vec_t *child_values = dbv_ordered_values_const(child);
        for (j = 0; j < vec_size(child_values) &&
                    status == DATA_BIND_OK;
             ++j) {
          const db_owned_value_slot_t *slot =
              (const db_owned_value_slot_t *)vec_at_const(child_values, j);
          status = slot != NULL && slot->value != NULL
                       ? apply_mapped_names(schema_root, nested_type, slot->value,
                                            depth + 1u, error)
                       : DATA_BIND_ERR_RUNTIME;
        }
      } else if (child->kind == DATA_BIND_VALUE_MAP) {
        for (j = 0; j < vec_size(&child->data.map.ordered_entries) &&
                    status == DATA_BIND_OK;
             ++j) {
          const db_map_entry_slot_t *entry =
              (const db_map_entry_slot_t *)vec_at_const(
                  &child->data.map.ordered_entries, j);
          status = entry != NULL && entry->value != NULL
                       ? apply_mapped_names(schema_root, nested_type, entry->value,
                                            depth + 1u, error)
                       : DATA_BIND_ERR_RUNTIME;
        }
      } else {
        status = apply_mapped_names(schema_root, nested_type, child, depth + 1u, error);
      }
    }
    if (status != DATA_BIND_OK) return status;
    if (mapped != NULL && strcmp(mapped, canonical) != 0) {
      char *copy = dbv_strdup(mapped);
      if (copy == NULL)
        return db_error_set(error, DATA_BIND_ERR_OOM, canonical, -1, -1,
                            "Out of memory applying field mapping");
      free(owned_field->name);
      owned_field->name = copy;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus create_mapped_object(DataBind *codec, const DataBindObject *object,
                                           DataBindObject *mapped, DataBindError *error) {
  DataBindStatus status;
  if (mapped != NULL) memset(mapped, 0, sizeof(*mapped));
  if (codec == NULL || codec->schema_root == NULL || object == NULL || object->type_name == NULL ||
      object->value == NULL || mapped == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid mapped serialize arguments");
  status = data_bind_object_check_schema(codec, object, "text", error);
  if (status != DATA_BIND_OK) return status;
  if (find_data_record(codec->schema_root, object->type_name) == NULL &&
      find_union_record(codec->schema_root, object->type_name) == NULL)
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, object->type_name, -1, -1,
                        "Mapped serializer type not found");
  status = data_bind_value_clone(object->value, &mapped->value);
  if (status != DATA_BIND_OK)
    return db_error_set(error, status, object->type_name, -1, -1,
                        "Failed to clone object for mapped serialization");
  mapped->type_name = object->type_name;
  status = apply_mapped_names(codec->schema_root, object->type_name, mapped->value, 0u, error);
  if (status != DATA_BIND_OK) {
    data_bind_value_free(mapped->value);
    mapped->value = NULL;
  }
  return status;
}

static json_value_t *data_bind_value_to_json(const DataBindValue *value, unsigned depth,
                                              DataBindStatus *status) {
  json_value_t *json = NULL;
  size_t i;
  char text[128];

  if (value == NULL || depth > DATA_BIND_JSON_MAX_DEPTH) {
    *status = DATA_BIND_ERR_RUNTIME;
    return NULL;
  }

  switch (value->kind) {
  case DATA_BIND_VALUE_NULL:
    json = json_create_null();
    break;
  case DATA_BIND_VALUE_INT:
    json = json_create_int64(value->data.int_val);
    break;
  case DATA_BIND_VALUE_INT64:
    json = json_create_int64(value->data.int64_val);
    break;
  case DATA_BIND_VALUE_UINT64:
    json = json_create_uint64(value->data.uint64_val);
    break;
  case DATA_BIND_VALUE_DOUBLE:
    if (!isfinite(value->data.double_val)) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_number(value->data.double_val);
    break;
  case DATA_BIND_VALUE_BOOL:
    json = json_create_bool(value->data.bool_val != 0);
    break;
  case DATA_BIND_VALUE_STRING:
    if (value->data.string_val.ptr == NULL ||
        !vstr_utf8_valid(
            vstr_from_buf(value->data.string_val.ptr, value->data.string_val.len))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string_n(value->data.string_val.ptr, value->data.string_val.len);
    break;
  case DATA_BIND_VALUE_BYTES:
    if (!vstr_utf8_valid(
            vstr_from_buf((const char *)value->data.bytes_val.ptr, value->data.bytes_val.len))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string_n((const char *)value->data.bytes_val.ptr,
                                      value->data.bytes_val.len);
    break;
  case DATA_BIND_VALUE_UUID:
    if (salts_uuid_format(&value->data.uuid_val, text, sizeof(text)) != SALTS_OK) {
      *status = DATA_BIND_ERR_RUNTIME;
      return NULL;
    }
    json = json_create_string(text);
    break;
  case DATA_BIND_VALUE_DATETIME:
    if (data_bind_temporal_format_rfc822(
            &value->data.datetime_val, text, sizeof(text)) != DATA_BIND_OK) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string(text);
    break;
  case DATA_BIND_VALUE_DATE:
    if (!db_date_to_text(value->data.date_val, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string(text);
    break;
  case DATA_BIND_VALUE_TIME:
    if (!db_time_to_text(value->data.time_val, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string(text);
    break;
  case DATA_BIND_VALUE_DURATION:
    if (!db_duration_to_text(value->data.duration_ms, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string(text);
    break;
  case DATA_BIND_VALUE_DECIMAL:
    if (!db_decimal_to_text(value->data.decimal_val, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_string(text);
    break;
  case DATA_BIND_VALUE_BIGINT:
    json = json_create_string(value->data.bigint_val.ptr);
    break;
  case DATA_BIND_VALUE_MONEY: {
    json_value_t *amount;
    json_value_t *currency;
    if (!db_decimal_to_text(value->data.money_val.amount, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = json_create_object();
    amount = json_create_string(text);
    currency = json_create_string(value->data.money_val.currency);
    if (json == NULL || amount == NULL || currency == NULL) {
      (json_free(amount), amount = NULL);
      (json_free(currency), currency = NULL);
      (json_free(json), json = NULL);
      *status = DATA_BIND_ERR_OOM;
      return NULL;
    }
    if (!json_object_add_checked(json, "amount", amount)) {
      (json_free(amount), amount = NULL);
      (json_free(currency), currency = NULL);
      (json_free(json), json = NULL);
      *status = DATA_BIND_ERR_OOM;
      return NULL;
    }
    amount = NULL;
    if (!json_object_add_checked(json, "currency", currency)) {
      (json_free(currency), currency = NULL);
      (json_free(json), json = NULL);
      *status = DATA_BIND_ERR_OOM;
      return NULL;
    }
    break;
  }
  case DATA_BIND_VALUE_OBJECT:
    json = json_create_object();
    for (i = 0; json != NULL && i < vec_size(&value->data.object.fields); ++i) {
      const db_field_slot_t *field =
          (const db_field_slot_t *)vec_at_const(&value->data.object.fields, i);
      json_value_t *child = field != NULL
                                ? data_bind_value_to_json(field->value, depth + 1, status)
                                : NULL;
      if (field == NULL || field->name == NULL || child == NULL ||
          !json_object_add_checked(json, field->name, child)) {
        (json_free(child), child = NULL);
        (json_free(json), json = NULL);
        if (*status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
      }
    }
    break;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET: {
    const vec_t *values = dbv_ordered_values_const(value);
    json = json_create_array();
    for (i = 0; json != NULL && i < vec_size(values); ++i) {
      const db_owned_value_slot_t *slot =
          (const db_owned_value_slot_t *)vec_at_const(values, i);
      json_value_t *child = slot != NULL
                                ? data_bind_value_to_json(slot->value, depth + 1, status)
                                : NULL;
      if (child == NULL || !json_array_add_checked(json, child)) {
        (json_free(child), child = NULL);
        (json_free(json), json = NULL);
        if (*status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
      }
    }
    break;
  }
  case DATA_BIND_VALUE_MAP:
    json = json_create_object();
    for (i = 0; json != NULL &&
                i < vec_size(&value->data.map.ordered_entries);
         ++i) {
      const db_map_entry_slot_t *entry =
          (const db_map_entry_slot_t *)vec_at_const(
              &value->data.map.ordered_entries, i);
      const char *key = entry != NULL ? entry->public_key_text : NULL;
      json_value_t *child = entry != NULL
                                ? data_bind_value_to_json(entry->value, depth + 1,
                                                          status)
                                : NULL;
      if (key == NULL || !vstr_utf8_valid(vstr_from_cstr(key))) {
        (json_free(child), child = NULL);
        (json_free(json), json = NULL);
        *status = DATA_BIND_ERR_TYPE_MISMATCH;
      } else if (child == NULL || !json_object_add_checked(json, key, child)) {
        (json_free(child), child = NULL);
        (json_free(json), json = NULL);
        if (*status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
      }
    }
    break;
  default:
    *status = DATA_BIND_ERR_TYPE_MISMATCH;
    return NULL;
  }

  if (json == NULL && *status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
  return json;
}

static DataBindStatus data_bind_object_serialize_json_canonical(
    const DataBindObject *object, char **out_json, size_t *out_len, DataBindError *error) {
  DataBindStatus status = DATA_BIND_OK;
  json_value_t *json;

  if (out_json != NULL) *out_json = NULL;
  if (out_len != NULL) *out_len = 0;
  if (object == NULL || object->value == NULL || out_json == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid JSON serialize arguments");

  json = data_bind_value_to_json(object->value, 0, &status);
  if (json == NULL)
    return db_error_set(error, status, "json", -1, -1,
                        status == DATA_BIND_ERR_TYPE_MISMATCH
                            ? "DataBind value cannot be represented as UTF-8 JSON"
                            : "Failed to construct JSON document");
  *out_json = json_serialize(json, out_len);
  (json_free(json), json = NULL);
  if (*out_json == NULL)
    return db_error_set(error, DATA_BIND_ERR_OOM, "json", -1, -1, "Out of memory serializing JSON");
  db_error_clear(error);
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_object_serialize_yaml_canonical(
    const DataBindObject *object, char **out_yaml, size_t *out_len, DataBindError *error) {
  DataBindStatus status = DATA_BIND_OK;
  json_value_t *json;
  cyaml_doc_t *yaml;

  if (out_yaml != NULL) *out_yaml = NULL;
  if (out_len != NULL) *out_len = 0;
  if (object == NULL || object->value == NULL || out_yaml == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid YAML serialize arguments");
  json = data_bind_value_to_json(object->value, 0, &status);
  if (!json)
    return db_error_set(error, status, "yaml", -1, -1,
                        "DataBind value cannot be represented as YAML");
  yaml = cyaml_doc_from_json_value(json);
  (json_free(json), json = NULL);
  if (!yaml)
    return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                        "Failed to construct YAML document");
  *out_yaml = cyaml_emit(yaml, NULL, out_len);
  (cyaml_free(yaml), yaml = NULL);
  if (!*out_yaml)
    return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                        "Failed to serialize YAML document");
  db_error_clear(error);
  return DATA_BIND_OK;
}

static int data_bind_xml_name_valid(const char *name) {
  const unsigned char *p = (const unsigned char *)name;
  if (!p || !(isalpha(*p) || *p == '_' || *p == ':')) return 0;
  for (++p; *p; ++p)
    if (!(isalnum(*p) || *p == '_' || *p == ':' || *p == '-' || *p == '.')) return 0;
  return 1;
}

static int data_bind_standard_scalar_text(const DataBindValue *value, char *text, size_t size) {
  switch (value->kind) {
  case DATA_BIND_VALUE_INT:
    return snprintf(text, size, "%d", value->data.int_val) > 0;
  case DATA_BIND_VALUE_INT64:
    return snprintf(text, size, "%lld", (long long)value->data.int64_val) > 0;
  case DATA_BIND_VALUE_UINT64:
    return snprintf(text, size, "%llu", (unsigned long long)value->data.uint64_val) > 0;
  case DATA_BIND_VALUE_DOUBLE:
    return isfinite(value->data.double_val) &&
           snprintf(text, size, "%.17g", value->data.double_val) > 0;
  case DATA_BIND_VALUE_BOOL:
    return snprintf(text, size, "%s", value->data.bool_val ? "true" : "false") > 0;
  case DATA_BIND_VALUE_UUID:
    return salts_uuid_format(&value->data.uuid_val, text, size) == SALTS_OK;
  case DATA_BIND_VALUE_DATETIME:
    return data_bind_temporal_format_rfc822(
               &value->data.datetime_val, text, size) == DATA_BIND_OK;
  case DATA_BIND_VALUE_DATE:
    return db_date_to_text(value->data.date_val, text, size);
  case DATA_BIND_VALUE_TIME:
    return db_time_to_text(value->data.time_val, text, size);
  case DATA_BIND_VALUE_DURATION:
    return db_duration_to_text(value->data.duration_ms, text, size);
  case DATA_BIND_VALUE_DECIMAL:
    return db_decimal_to_text(value->data.decimal_val, text, size);
  case DATA_BIND_VALUE_BIGINT:
    return snprintf(text, size, "%s", value->data.bigint_val.ptr) > 0;
  case DATA_BIND_VALUE_MONEY:
    return db_money_to_text(value->data.money_val, text, size);
  default:
    return 0;
  }
}

static int data_bind_value_to_xml(const DataBindValue *value, salts_xml_node node,
                                  unsigned depth) {
  char text[128];
  size_t i;
  if (!value || !node.impl || depth > DATA_BIND_JSON_MAX_DEPTH) return 0;
  switch (value->kind) {
  case DATA_BIND_VALUE_STRING:
    return value->data.string_val.ptr &&
           memchr(value->data.string_val.ptr, '\0', value->data.string_val.len) == NULL &&
           vstr_utf8_valid(
               vstr_from_buf(value->data.string_val.ptr, value->data.string_val.len)) &&
           salts_xml_node_set_text(node, value->data.string_val.ptr) == 0;
  case DATA_BIND_VALUE_BYTES:
    if (memchr(value->data.bytes_val.ptr, '\0', value->data.bytes_val.len)) return 0;
    if (value->data.bytes_val.len >= sizeof(text)) return 0;
    memcpy(text, value->data.bytes_val.ptr, value->data.bytes_val.len);
    text[value->data.bytes_val.len] = '\0';
    return vstr_utf8_valid(vstr_from_buf(text, value->data.bytes_val.len)) &&
           salts_xml_node_set_text(node, text) == 0;
  case DATA_BIND_VALUE_BIGINT:
    return value->data.bigint_val.ptr && salts_xml_node_set_text(node, value->data.bigint_val.ptr) == 0;
  case DATA_BIND_VALUE_OBJECT:
    for (i = 0; i < vec_size(&value->data.object.fields); ++i) {
      const db_field_slot_t *field =
          (const db_field_slot_t *)vec_at_const(&value->data.object.fields, i);
      const char *name = field != NULL ? field->name : NULL;
      const DataBindValue *child_value = field != NULL ? field->value : NULL;
      if (name == NULL || child_value == NULL) return 0;
      if (child_value->kind == DATA_BIND_VALUE_LIST || child_value->kind == DATA_BIND_VALUE_SET) {
        const vec_t *child_values = dbv_ordered_values_const(child_value);
        for (size_t j = 0; j < vec_size(child_values); ++j) {
          const db_owned_value_slot_t *slot = (const db_owned_value_slot_t *)vec_at_const(
              child_values, j);
          salts_xml_node child = {0};
          if (salts_xml_node_add_element(node, name, &child) != SALTS_XML_OK) return 0;
          if (!child.impl ||
              slot == NULL || slot->value == NULL ||
              !data_bind_value_to_xml(slot->value, child, depth + 1))
            return 0;
        }
      } else {
        salts_xml_node child = {0};
        if (salts_xml_node_add_element(node, name, &child) != SALTS_XML_OK) return 0;
        if (!child.impl || !data_bind_value_to_xml(child_value, child, depth + 1)) return 0;
      }
    }
    return 1;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET: {
    const vec_t *values = dbv_ordered_values_const(value);
    for (i = 0; i < vec_size(values); ++i) {
      const db_owned_value_slot_t *slot =
          (const db_owned_value_slot_t *)vec_at_const(values, i);
      salts_xml_node child = {0};
      if (salts_xml_node_add_element(node, "item", &child) != SALTS_XML_OK) return 0;
      if (!child.impl || slot == NULL || slot->value == NULL ||
          !data_bind_value_to_xml(slot->value, child, depth + 1))
        return 0;
    }
    return 1;
  }
  case DATA_BIND_VALUE_MAP:
    for (i = 0; i < vec_size(&value->data.map.ordered_entries); ++i) {
      const db_map_entry_slot_t *entry =
          (const db_map_entry_slot_t *)vec_at_const(
              &value->data.map.ordered_entries, i);
      const char *key = entry != NULL ? entry->public_key_text : NULL;
      salts_xml_node child = {0};
      if (!data_bind_xml_name_valid(key)) return 0;
      if (salts_xml_node_add_element(node, key, &child) != SALTS_XML_OK) return 0;
      if (!child.impl || entry->value == NULL ||
          !data_bind_value_to_xml(entry->value, child, depth + 1))
        return 0;
    }
    return 1;
  case DATA_BIND_VALUE_NULL:
    return 0;
  default:
    return data_bind_standard_scalar_text(value, text, sizeof(text)) &&
           salts_xml_node_set_text(node, text) == 0;
  }
}

static DataBindStatus data_bind_object_serialize_xml_canonical(
    const DataBindObject *object, char **out_xml, size_t *out_len, DataBindError *error) {
  salts_xml_document xml = {0};
  salts_xml_node root = {0};
  if (out_xml != NULL) *out_xml = NULL;
  if (out_len != NULL) *out_len = 0;
  if (!object || !object->value || !object->type_name || !out_xml ||
      !data_bind_xml_name_valid(object->type_name))
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid XML serialize arguments");
  if (salts_xml_document_create(&xml, object->type_name) != SALTS_XML_OK) {
    return db_error_set(error, DATA_BIND_ERR_OOM, "xml", -1, -1,
                        "Unable to create XML document");
  }
  root = salts_xml_document_root(&xml);
  if (!root.impl || !data_bind_value_to_xml(object->value, root, 0)) {
    salts_xml_document_destroy(&xml);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, "xml", -1, -1,
                        "DataBind value cannot be represented as XML");
  }
  *out_xml = salts_xml_document_serialize(&xml, out_len);
  salts_xml_document_destroy(&xml);
  if (!*out_xml)
    return db_error_set(error, DATA_BIND_ERR_OOM, "xml", -1, -1,
                        "Failed to serialize XML document");
  db_error_clear(error);
  return DATA_BIND_OK;
}

#define DATA_BIND_CSV_MAX_PATH_LENGTH 255u

typedef struct data_bind_csv_cell {
  tstr path;
  tstr text;
} data_bind_csv_cell_t;

TBE_TYPED_VEC_DEFINE(data_bind_csv_cell_vec_t, data_bind_csv_cell_t)

static void data_bind_csv_cells_destroy(data_bind_csv_cell_vec_t *cells) {
  size_t i;
  if (cells == NULL) return;
  for (i = 0; i < data_bind_csv_cell_vec_t_size(cells); ++i) {
    data_bind_csv_cell_t *cell = data_bind_csv_cell_vec_t_at(cells, i);
    if (cell != NULL) {
      tstr_free(cell->path);
      tstr_free(cell->text);
    }
  }
  data_bind_csv_cell_vec_t_destroy(cells);
}

static int data_bind_csv_tstr_append(tstr *out, const char *data, size_t len) {
  tstr next;
  if (out == NULL || *out == NULL || (data == NULL && len != 0)) return 0;
  next = tstr_cat_len(*out, data, len);
  if (next == NULL) return 0;
  *out = next;
  return 1;
}

static int data_bind_csv_path_component_valid(const char *component) {
  if (component == NULL || component[0] == '\0' || strchr(component, '.') != NULL ||
      strchr(component, '[') != NULL)
    return 0;
  return vstr_utf8_valid(vstr_from_cstr(component));
}

static tstr data_bind_csv_child_path(const tstr prefix, const char *name,
                                       DataBindStatus *status) {
  size_t prefix_len = prefix != NULL ? tstr_len(prefix) : 0;
  size_t name_len;
  size_t separator_len = prefix_len != 0 ? 1u : 0u;
  tstr path;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (!data_bind_csv_path_component_valid(name)) return NULL;
  name_len = strlen(name);
  if (prefix_len > DATA_BIND_CSV_MAX_PATH_LENGTH - separator_len ||
      name_len > DATA_BIND_CSV_MAX_PATH_LENGTH - prefix_len - separator_len)
    return NULL;
  path = prefix != NULL ? tstr_clone(prefix) : tstr_new();
  if (path == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  if ((separator_len != 0 && !data_bind_csv_tstr_append(&path, ".", 1u)) ||
      !data_bind_csv_tstr_append(&path, name, name_len)) {
    tstr_free(path);
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return path;
}

static tstr data_bind_csv_index_path(const tstr prefix, size_t index,
                                       DataBindStatus *status) {
  char suffix[32];
  int suffix_len;
  size_t prefix_len;
  tstr path;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (prefix == NULL || tstr_empty(prefix)) return NULL;
  suffix_len = fmt(suffix, sizeof(suffix), "[{}]", index);
  if (suffix_len <= 0 || (size_t)suffix_len >= sizeof(suffix)) return NULL;
  prefix_len = tstr_len(prefix);
  if (prefix_len > DATA_BIND_CSV_MAX_PATH_LENGTH ||
      (size_t)suffix_len > DATA_BIND_CSV_MAX_PATH_LENGTH - prefix_len)
    return NULL;
  path = tstr_clone(prefix);
  if (path == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  if (!data_bind_csv_tstr_append(&path, suffix, (size_t)suffix_len)) {
    tstr_free(path);
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return path;
}

static tstr data_bind_csv_scalar_text(const DataBindValue *value, DataBindStatus *status) {
  char text[128];
  tstr result = NULL;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (value == NULL) return NULL;
  if (value->kind == DATA_BIND_VALUE_STRING) {
    const char *string = value->data.string_val.ptr;
    size_t len = value->data.string_val.len;
    if (string == NULL || memchr(string, '\0', len) != NULL ||
        !vstr_utf8_valid(vstr_from_buf(string, len)))
      return NULL;
    result = tstr_dup_len(string, len);
  } else if (value->kind == DATA_BIND_VALUE_BYTES) {
    const char *bytes = (const char *)value->data.bytes_val.ptr;
    size_t len = value->data.bytes_val.len;
    if (len != 0 &&
        (bytes == NULL || memchr(bytes, '\0', len) != NULL ||
         !vstr_utf8_valid(vstr_from_buf(bytes, len))))
      return NULL;
    result = len != 0 ? tstr_dup_len(bytes, len) : tstr_new();
  } else if (value->kind == DATA_BIND_VALUE_BIGINT) {
    if (value->data.bigint_val.ptr == NULL) return NULL;
    result = tstr_dup(value->data.bigint_val.ptr);
  } else {
    if (!data_bind_standard_scalar_text(value, text, sizeof(text))) return NULL;
    result = tstr_dup(text);
  }
  if (result == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return result;
}

static DataBindStatus data_bind_csv_add_scalar(data_bind_csv_cell_vec_t *cells,
                                               const tstr path,
                                               const DataBindValue *value) {
  data_bind_csv_cell_t cell = {0};
  DataBindStatus status;
  cell.path = path != NULL && !tstr_empty(path) ? tstr_clone(path) : tstr_dup("value");
  if (cell.path == NULL) return DATA_BIND_ERR_OOM;
  cell.text = data_bind_csv_scalar_text(value, &status);
  if (cell.text == NULL) {
    tstr_free(cell.path);
    return status;
  }
  if (data_bind_csv_cell_vec_t_push(cells, cell) != STL_OK) {
    tstr_free(cell.path);
    tstr_free(cell.text);
    return DATA_BIND_ERR_OOM;
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_csv_flatten_value(data_bind_csv_cell_vec_t *cells,
                                                  const DataBindValue *value,
                                                  const tstr path, unsigned depth) {
  size_t i;
  if (cells == NULL || value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (depth > DATA_BIND_JSON_MAX_DEPTH) return DATA_BIND_ERR_RUNTIME;
  switch (value->kind) {
  case DATA_BIND_VALUE_OBJECT:
    if (vec_size(&value->data.object.fields) == 0u) return DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < vec_size(&value->data.object.fields); ++i) {
      const db_field_slot_t *field =
          (const db_field_slot_t *)vec_at_const(&value->data.object.fields, i);
      DataBindStatus status;
      tstr child_path;
      if (field == NULL || field->name == NULL || field->value == NULL)
        return DATA_BIND_ERR_RUNTIME;
      child_path = data_bind_csv_child_path(path, field->name, &status);
      if (child_path == NULL) return status;
      status = data_bind_csv_flatten_value(cells, field->value, child_path, depth + 1);
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET: {
    const vec_t *values = dbv_ordered_values_const(value);
    if (path == NULL || tstr_empty(path) || vec_size(values) == 0u)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < vec_size(values); ++i) {
      const db_owned_value_slot_t *slot =
          (const db_owned_value_slot_t *)vec_at_const(values, i);
      DataBindStatus status;
      tstr child_path = data_bind_csv_index_path(path, i, &status);
      if (child_path == NULL) return status;
      status = slot != NULL && slot->value != NULL
                   ? data_bind_csv_flatten_value(cells, slot->value, child_path, depth + 1)
                   : DATA_BIND_ERR_RUNTIME;
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  }
  case DATA_BIND_VALUE_MAP:
    if (path == NULL || tstr_empty(path) ||
        vec_size(&value->data.map.ordered_entries) == 0u)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < vec_size(&value->data.map.ordered_entries); ++i) {
      const db_map_entry_slot_t *entry =
          (const db_map_entry_slot_t *)vec_at_const(
              &value->data.map.ordered_entries, i);
      DataBindStatus status;
      if (entry == NULL || entry->public_key_text == NULL || entry->value == NULL)
        return DATA_BIND_ERR_RUNTIME;
      tstr child_path =
          data_bind_csv_child_path(path, entry->public_key_text, &status);
      if (child_path == NULL) return status;
      status = data_bind_csv_flatten_value(cells, entry->value, child_path,
                                           depth + 1);
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_NULL:
    return DATA_BIND_ERR_TYPE_MISMATCH;
  default:
    return data_bind_csv_add_scalar(cells, path, value);
  }
}

static int data_bind_csv_append_field(tstr *csv, const tstr field) {
  size_t i;
  size_t start = 0;
  size_t len;
  int quoted = 0;
  if (csv == NULL || *csv == NULL || field == NULL) return 0;
  len = tstr_len(field);
  if ((len != 0 && (field[0] == ' ' || field[0] == '\t' || field[len - 1] == ' ' ||
                    field[len - 1] == '\t')) ||
      memchr(field, ',', len) != NULL || memchr(field, '"', len) != NULL ||
      memchr(field, '\r', len) != NULL || memchr(field, '\n', len) != NULL)
    quoted = 1;
  if (!quoted) return data_bind_csv_tstr_append(csv, field, len);
  if (!data_bind_csv_tstr_append(csv, "\"", 1u)) return 0;
  for (i = 0; i < len; ++i) {
    if (field[i] != '"') continue;
    if (!data_bind_csv_tstr_append(csv, field + start, i - start) ||
        !data_bind_csv_tstr_append(csv, "\"\"", 2u))
      return 0;
    start = i + 1;
  }
  return data_bind_csv_tstr_append(csv, field + start, len - start) &&
         data_bind_csv_tstr_append(csv, "\"", 1u);
}

static DataBindStatus data_bind_object_serialize_csv_canonical(
    const DataBindObject *object, char **out_csv, size_t *out_len, DataBindError *error) {
  data_bind_csv_cell_vec_t cells = {0};
  DataBindStatus status;
  tstr csv = NULL;
  size_t i;
  if (out_csv != NULL) *out_csv = NULL;
  if (out_len != NULL) *out_len = 0;
  if (object == NULL || object->value == NULL || out_csv == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid CSV serialize arguments");
  if (object->value->kind == DATA_BIND_VALUE_LIST ||
      object->value->kind == DATA_BIND_VALUE_SET || object->value->kind == DATA_BIND_VALUE_MAP)
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, "csv", -1, -1,
                        "A CSV object must contain one record or scalar value");
  if (data_bind_csv_cell_vec_t_init(&cells, SIZE_MAX) != STL_OK)
    return db_error_set(error, DATA_BIND_ERR_OOM, "csv", -1, -1,
                        "Out of memory creating CSV columns");
  status = data_bind_csv_flatten_value(&cells, object->value, NULL, 0);
  if (status != DATA_BIND_OK || data_bind_csv_cell_vec_t_empty(&cells)) {
    data_bind_csv_cells_destroy(&cells);
    return db_error_set(error, status != DATA_BIND_OK ? status : DATA_BIND_ERR_TYPE_MISMATCH,
                        "csv", -1, -1,
                        status == DATA_BIND_ERR_OOM
                            ? "Out of memory flattening CSV columns"
                            : "DataBind value cannot be represented losslessly as CSV");
  }
  csv = tstr_new();
  if (csv == NULL) status = DATA_BIND_ERR_OOM;
  for (i = 0; status == DATA_BIND_OK && i < data_bind_csv_cell_vec_t_size(&cells); ++i) {
    const data_bind_csv_cell_t *cell = data_bind_csv_cell_vec_t_at_const(&cells, i);
    if ((i != 0 && !data_bind_csv_tstr_append(&csv, ",", 1u)) ||
        !data_bind_csv_append_field(&csv, cell->path))
      status = DATA_BIND_ERR_OOM;
  }
  if (status == DATA_BIND_OK && !data_bind_csv_tstr_append(&csv, "\r\n", 2u))
    status = DATA_BIND_ERR_OOM;
  for (i = 0; status == DATA_BIND_OK && i < data_bind_csv_cell_vec_t_size(&cells); ++i) {
    const data_bind_csv_cell_t *cell = data_bind_csv_cell_vec_t_at_const(&cells, i);
    if ((i != 0 && !data_bind_csv_tstr_append(&csv, ",", 1u)) ||
        !data_bind_csv_append_field(&csv, cell->text))
      status = DATA_BIND_ERR_OOM;
  }
  if (status == DATA_BIND_OK && !data_bind_csv_tstr_append(&csv, "\r\n", 2u))
    status = DATA_BIND_ERR_OOM;
  if (status == DATA_BIND_OK) {
    *out_csv = tstr_to_cstr(csv);
    if (*out_csv == NULL) status = DATA_BIND_ERR_OOM;
    else if (out_len != NULL) *out_len = tstr_len(csv);
  }
  tstr_free(csv);
  data_bind_csv_cells_destroy(&cells);
  if (status != DATA_BIND_OK)
    return db_error_set(error, status, "csv", -1, -1, "Out of memory serializing CSV");
  db_error_clear(error);
  return DATA_BIND_OK;
}

typedef DataBindStatus (*data_bind_mapped_serialize_fn)(const DataBindObject *, char **, size_t *,
                                                        DataBindError *);

static DataBindStatus data_bind_object_serialize_with_schema_names(
    DataBind *codec, const DataBindObject *object, char **out, size_t *out_len,
    DataBindError *error, data_bind_mapped_serialize_fn serialize) {
  DataBindObject mapped = {0};
  DataBindStatus status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0;
  if (serialize == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid schema-aware serializer");
  status = create_mapped_object(codec, object, &mapped, error);
  if (status == DATA_BIND_OK) status = serialize(&mapped, out, out_len, error);
  data_bind_value_free(mapped.value);
  return status;
}

DataBindStatus data_bind_object_serialize_json(DataBind *codec, const DataBindObject *object,
                                               char **out_json, size_t *out_len,
                                               DataBindError *error) {
  return data_bind_object_serialize_with_schema_names(
      codec, object, out_json, out_len, error, data_bind_object_serialize_json_canonical);
}

DataBindStatus data_bind_object_serialize_yaml(DataBind *codec, const DataBindObject *object,
                                               char **out_yaml, size_t *out_len,
                                               DataBindError *error) {
  return data_bind_object_serialize_with_schema_names(
      codec, object, out_yaml, out_len, error, data_bind_object_serialize_yaml_canonical);
}

DataBindStatus data_bind_object_serialize_xml(DataBind *codec, const DataBindObject *object,
                                              char **out_xml, size_t *out_len,
                                              DataBindError *error) {
  return data_bind_object_serialize_with_schema_names(
      codec, object, out_xml, out_len, error, data_bind_object_serialize_xml_canonical);
}

DataBindStatus data_bind_object_serialize_csv(DataBind *codec, const DataBindObject *object,
                                              char **out_csv, size_t *out_len,
                                              DataBindError *error) {
  return data_bind_object_serialize_with_schema_names(
      codec, object, out_csv, out_len, error, data_bind_object_serialize_csv_canonical);
}

typedef DataBindStatus (*data_bind_object_serialize_fn)(DataBind *, const DataBindObject *, char **,
                                                        size_t *, DataBindError *);

static DataBindStatus data_bind_object_write(DataBind *codec, const DataBindObject *object,
                                             DataBindWriteFn write, void *user,
                                             DataBindError *error,
                                             data_bind_object_serialize_fn serialize) {
  char *text = NULL;
  size_t len = 0;
  DataBindStatus status;
  if (!write)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1, "Invalid serialize writer");
  status = serialize(codec, object, &text, &len, error);
  if (status != DATA_BIND_OK) return status;
  if (write(text, len, user) != 0) {
    data_bind_serialized_free(text);
    return db_error_set(error, DATA_BIND_ERR_IO, NULL, -1, -1, "Serialize writer failed");
  }
  data_bind_serialized_free(text);
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_object_write_json(DataBind *codec, const DataBindObject *object,
                                           DataBindWriteFn write, void *user,
                                           DataBindError *error) {
  return data_bind_object_write(codec, object, write, user, error,
                                data_bind_object_serialize_json);
}

DataBindStatus data_bind_object_write_yaml(DataBind *codec, const DataBindObject *object,
                                           DataBindWriteFn write, void *user,
                                           DataBindError *error) {
  return data_bind_object_write(codec, object, write, user, error,
                                data_bind_object_serialize_yaml);
}

DataBindStatus data_bind_object_write_xml(DataBind *codec, const DataBindObject *object,
                                          DataBindWriteFn write, void *user,
                                          DataBindError *error) {
  return data_bind_object_write(codec, object, write, user, error,
                                data_bind_object_serialize_xml);
}

DataBindStatus data_bind_object_write_csv(DataBind *codec, const DataBindObject *object,
                                          DataBindWriteFn write, void *user,
                                          DataBindError *error) {
  return data_bind_object_write(codec, object, write, user, error,
                                data_bind_object_serialize_csv);
}

void data_bind_serialized_free(char *data) { json_serialize_free(data); }

void data_bind_binary_free(void *data) { free(data); }

void data_bind_object_free(DataBindObject *object) {
  if (object == NULL) return;
  free(object->type_name);
  data_bind_value_free(object->value);
  free(object);
}

DataBindValueKind data_bind_value_kind(const DataBindValue *value) {
  return value != NULL ? value->kind : DATA_BIND_VALUE_NULL;
}

uint64_t data_bind_value_generation(const DataBindValue *value) {
  if (value == NULL) return UINT64_C(0);
  switch (value->kind) {
  case DATA_BIND_VALUE_OBJECT:
    return vec_generation(&value->data.object.fields);
  case DATA_BIND_VALUE_LIST:
    return vec_generation(&value->data.sequence.values);
  case DATA_BIND_VALUE_SET:
    return value->data.set.generation;
  case DATA_BIND_VALUE_MAP:
    return value->data.map.generation;
  default:
    return UINT64_C(0);
  }
}

const cmeta_type_identity *data_bind_value_type_identity(const DataBindValue *value) {
  return value != NULL ? value->type_identity : NULL;
}

size_t data_bind_value_field_count(const DataBindValue *value) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT) return 0;
  return vec_size(&value->data.object.fields);
}

const char *data_bind_value_field_name(const DataBindValue *value, size_t index) {
  const db_field_slot_t *field;
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT) return NULL;
  field = (const db_field_slot_t *)vec_at_const(&value->data.object.fields, index);
  return field != NULL ? field->name : NULL;
}

const DataBindValue *data_bind_value_field_at(const DataBindValue *value, size_t index) {
  const db_field_slot_t *field;
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT) return NULL;
  field = (const db_field_slot_t *)vec_at_const(&value->data.object.fields, index);
  return field != NULL ? field->value : NULL;
}

const DataBindValue *data_bind_value_get(const DataBindValue *value, const char *name) {
  size_t i;
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT || name == NULL) return NULL;
  for (i = 0; i < vec_size(&value->data.object.fields); i++) {
    const db_field_slot_t *field =
        (const db_field_slot_t *)vec_at_const(&value->data.object.fields, i);
    if (field != NULL && field->name != NULL && strcmp(field->name, name) == 0)
      return field->value;
  }
  return NULL;
}

size_t data_bind_value_count(const DataBindValue *value) {
  const vec_t *values;
  if (value == NULL) return 0;
  values = dbv_ordered_values_const(value);
  if (values != NULL) return vec_size(values);
  if (value->kind == DATA_BIND_VALUE_MAP)
    return vec_size(&value->data.map.ordered_entries);
  return 0;
}

const DataBindValue *data_bind_value_at(const DataBindValue *value, size_t index) {
  const vec_t *values;
  const db_owned_value_slot_t *slot;
  if (value == NULL ||
      (value->kind != DATA_BIND_VALUE_LIST && value->kind != DATA_BIND_VALUE_SET))
    return NULL;
  values = dbv_ordered_values_const(value);
  slot = (const db_owned_value_slot_t *)vec_at_const(values, index);
  return slot != NULL ? slot->value : NULL;
}

DataBindMapEntry data_bind_value_map_entry_at(const DataBindValue *value, size_t index) {
  DataBindMapEntry entry;
  const db_map_entry_slot_t *stored;
  entry.key = NULL;
  entry.value = NULL;
  if (value == NULL || value->kind != DATA_BIND_VALUE_MAP) return entry;
  stored = (const db_map_entry_slot_t *)vec_at_const(
      &value->data.map.ordered_entries, index);
  if (stored == NULL) return entry;
  entry.key = stored->public_key_text;
  entry.value = stored->value;
  return entry;
}

int32_t data_bind_value_as_int(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_INT) return value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= INT32_MIN &&
      value->data.int64_val <= INT32_MAX)
    return (int32_t)value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT32_MAX)
    return (int32_t)value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_DOUBLE && isfinite(value->data.double_val) &&
      value->data.double_val >= (double)INT32_MIN && value->data.double_val <= (double)INT32_MAX)
    return (int32_t)value->data.double_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1 : 0;
  return 0;
}

int64_t data_bind_value_as_int64(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_INT64) return value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_INT) return value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT64_MAX)
    return (int64_t)value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_DOUBLE && isfinite(value->data.double_val) &&
      value->data.double_val >= (double)INT64_MIN && value->data.double_val < 0x1p63)
    return (int64_t)value->data.double_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1 : 0;
  return 0;
}

uint64_t data_bind_value_as_uint64(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_UINT64) return value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= 0)
    return (uint64_t)value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_INT && value->data.int_val >= 0)
    return (uint64_t)value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1u : 0u;
  return 0;
}

double data_bind_value_as_double(const DataBindValue *value) {
  if (value == NULL) return 0.0;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) return value->data.double_val;
  if (value->kind == DATA_BIND_VALUE_INT64) return (double)value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_UINT64) return (double)value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_INT) return (double)value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1.0 : 0.0;
  return 0.0;
}

int data_bind_value_as_bool(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val != 0;
  if (value->kind == DATA_BIND_VALUE_INT) return value->data.int_val != 0;
  if (value->kind == DATA_BIND_VALUE_INT64) return value->data.int64_val != 0;
  if (value->kind == DATA_BIND_VALUE_UINT64) return value->data.uint64_val != 0;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) return value->data.double_val != 0.0;
  return 0;
}

const char *data_bind_value_as_string(const DataBindValue *value) {
  return value != NULL && value->kind == DATA_BIND_VALUE_STRING ? value->data.string_val.ptr : NULL;
}

const uint8_t *data_bind_value_as_bytes(const DataBindValue *value, size_t *len) {
  if (len != NULL) *len = 0;
  if (value == NULL || value->kind != DATA_BIND_VALUE_BYTES) return NULL;
  if (len != NULL) *len = value->data.bytes_val.len;
  return value->data.bytes_val.ptr;
}

int data_bind_value_as_uuid(const DataBindValue *value, uint8_t out[DATA_BIND_UUID_SIZE]) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_UUID || out == NULL) return 0;
  memcpy(out, value->data.uuid_val.bytes, DATA_BIND_UUID_SIZE);
  return 1;
}

const char *data_bind_value_as_uuid_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_UUID || out == NULL ||
      len < SALTS_UUID_STRING_SIZE)
    return NULL;
  return salts_uuid_format(&value->data.uuid_val, out, len) == SALTS_OK ? out : NULL;
}

int data_bind_value_as_datetime(const DataBindValue *value, DataBindDateTime *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATETIME || out == NULL) return 0;
  *out = value->data.datetime_val;
  return 1;
}

double data_bind_value_as_datetime_timestamp(const DataBindValue *value) {
  int64_t seconds;
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATETIME) return -1.0;
  if (data_bind_temporal_to_unix_seconds(
          &value->data.datetime_val, &seconds) != DATA_BIND_OK)
    return -1.0;
  return (double)seconds;
}

const char *data_bind_value_as_datetime_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATETIME || out == NULL || len < 32)
    return NULL;
  return data_bind_temporal_format_rfc822(
             &value->data.datetime_val, out, len) == DATA_BIND_OK
             ? out
             : NULL;
}

int data_bind_value_as_date(const DataBindValue *value, DataBindDate *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATE || out == NULL) return 0;
  *out = value->data.date_val;
  return 1;
}

const char *data_bind_value_as_date_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATE) return NULL;
  return db_date_to_text(value->data.date_val, out, len) ? out : NULL;
}

int data_bind_value_as_time(const DataBindValue *value, DataBindTime *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_TIME || out == NULL) return 0;
  *out = value->data.time_val;
  return 1;
}

const char *data_bind_value_as_time_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_TIME) return NULL;
  return db_time_to_text(value->data.time_val, out, len) ? out : NULL;
}

int64_t data_bind_value_as_duration_milliseconds(const DataBindValue *value) {
  return value != NULL && value->kind == DATA_BIND_VALUE_DURATION ? value->data.duration_ms : 0;
}

const char *data_bind_value_as_duration_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DURATION) return NULL;
  return db_duration_to_text(value->data.duration_ms, out, len) ? out : NULL;
}

int data_bind_value_as_decimal(const DataBindValue *value, DataBindDecimal *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DECIMAL || out == NULL) return 0;
  *out = value->data.decimal_val;
  return 1;
}

const char *data_bind_value_as_decimal_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DECIMAL) return NULL;
  return db_decimal_to_text(value->data.decimal_val, out, len) ? out : NULL;
}

const char *data_bind_value_as_bigint_string(const DataBindValue *value) {
  return value != NULL && value->kind == DATA_BIND_VALUE_BIGINT ? value->data.bigint_val.ptr : NULL;
}

int data_bind_value_as_money(const DataBindValue *value, DataBindMoney *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_MONEY || out == NULL) return 0;
  *out = value->data.money_val;
  return 1;
}

const char *data_bind_value_as_money_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_MONEY) return NULL;
  return db_money_to_text(value->data.money_val, out, len) ? out : NULL;
}

DataBindStatus data_bind_value_get_int32(const DataBindValue *value, int32_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1 : 0;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= INT32_MIN &&
      value->data.int64_val <= INT32_MAX) {
    *out = (int32_t)value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT32_MAX) {
    *out = (int32_t)value->data.uint64_val;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_int64(const DataBindValue *value, int64_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_INT64) {
    *out = value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1 : 0;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT64_MAX) {
    *out = (int64_t)value->data.uint64_val;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_uint64(const DataBindValue *value, uint64_t *out) {
  if (out == NULL || value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_UINT64) {
    *out = value->data.uint64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= 0) {
    *out = (uint64_t)value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT && value->data.int_val >= 0) {
    *out = (uint64_t)value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1u : 0u;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_has_any_bits(const DataBindValue *value, uint64_t mask, int *out) {
  uint64_t bits;
  DataBindStatus status;
  if (out == NULL || mask == 0) return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_value_get_uint64(value, &bits);
  if (status != DATA_BIND_OK) return status;
  *out = (bits & mask) != 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_has_all_bits(const DataBindValue *value, uint64_t mask, int *out) {
  uint64_t bits;
  DataBindStatus status;
  if (out == NULL || mask == 0) return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_value_get_uint64(value, &bits);
  if (status != DATA_BIND_OK) return status;
  *out = (bits & mask) == mask;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_double(const DataBindValue *value, double *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) {
    *out = value->data.double_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT64) {
    *out = (double)value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_UINT64) {
    *out = (double)value->data.uint64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = (double)value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1.0 : 0.0;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_bool(const DataBindValue *value, int *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_BOOL) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.bool_val != 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_string(const DataBindValue *value, const char **data,
                                          size_t *len) {
  if (data == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *data = NULL;
  if (len != NULL) *len = 0;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_STRING) return DATA_BIND_ERR_TYPE_MISMATCH;
  *data = value->data.string_val.ptr;
  if (len != NULL) *len = value->data.string_val.ptr ? value->data.string_val.len : 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_bytes(const DataBindValue *value, const uint8_t **data,
                                         size_t *len) {
  if (data == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *data = NULL;
  if (len != NULL) *len = 0;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_BYTES) return DATA_BIND_ERR_TYPE_MISMATCH;
  *data = value->data.bytes_val.ptr;
  if (len != NULL) *len = value->data.bytes_val.len;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_uuid(const DataBindValue *value,
                                        uint8_t out[DATA_BIND_UUID_SIZE]) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_UUID) return DATA_BIND_ERR_TYPE_MISMATCH;
  memcpy(out, value->data.uuid_val.bytes, DATA_BIND_UUID_SIZE);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_datetime(const DataBindValue *value, DataBindDateTime *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DATETIME) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.datetime_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_date(const DataBindValue *value, DataBindDate *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DATE) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.date_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_time(const DataBindValue *value, DataBindTime *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_TIME) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.time_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_duration_milliseconds(const DataBindValue *value, int64_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DURATION) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.duration_ms;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_decimal(const DataBindValue *value, DataBindDecimal *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DECIMAL) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.decimal_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_bigint(const DataBindValue *value, const char **text,
                                          size_t *len) {
  if (text == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *text = NULL;
  if (len != NULL) *len = 0;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_BIGINT) return DATA_BIND_ERR_TYPE_MISMATCH;
  *text = value->data.bigint_val.ptr;
  if (len != NULL)
    *len = value->data.bigint_val.ptr != NULL ? strlen(value->data.bigint_val.ptr) : 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_money(const DataBindValue *value, DataBindMoney *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_MONEY) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.money_val;
  return DATA_BIND_OK;
}

const char *data_bind_schema_kind_name(DataBindSchemaKind kind) {
  switch (kind) {
  case DATA_BIND_SCHEMA_MESSAGE:
    return "message";
  case DATA_BIND_SCHEMA_COMPOSITE:
    return "composite";
  case DATA_BIND_SCHEMA_GROUP:
    return "group";
  case DATA_BIND_SCHEMA_ENUM:
    return "enum";
  case DATA_BIND_SCHEMA_FLAGS:
    return "flags";
  case DATA_BIND_SCHEMA_UNION:
    return "union";
  case DATA_BIND_SCHEMA_SCALAR:
    return "scalar";
  case DATA_BIND_SCHEMA_UNKNOWN:
  default:
    return "unknown";
  }
}

size_t data_bind_schema_type_count(DataBind *codec) {
  size_t count = 0;
  static const char *const lists[] = {"messages", "composites", "groups", "unions", "enums"};
  size_t i;
  if (codec == NULL || codec->schema_root == NULL) return 0;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    Node *list = find_child(codec->schema_root, lists[i]);
    if (list != NULL && list->type == NODE_LIST) count += list->data.list.count;
  }
  return count;
}

int data_bind_schema_type_at(DataBind *codec, size_t index, DataBindSchemaType *out) {
  static const char *const lists[] = {"messages", "composites", "groups", "unions", "enums"};
  size_t i;
  if (codec == NULL || codec->schema_root == NULL || out == NULL) return 0;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    Node *list = find_child(codec->schema_root, lists[i]);
    if (list == NULL || list->type != NODE_LIST) continue;
    if (index < list->data.list.count)
      return fill_schema_type(list->data.list.items[index], lists[i], out);
    index -= list->data.list.count;
  }
  db_reflect_clear(out, out->size, sizeof(*out));
  return 0;
}

int data_bind_schema_find_type(DataBind *codec, const char *name, DataBindSchemaType *out) {
  static const char *const lists[] = {"messages", "composites", "groups", "unions", "enums"};
  size_t i;
  if (codec == NULL || codec->schema_root == NULL || name == NULL || out == NULL) return 0;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    Node *record = find_named_record(codec->schema_root, lists[i], name);
    if (record != NULL) return fill_schema_type(record, lists[i], out);
  }
  db_reflect_clear(out, out->size, sizeof(*out));
  return 0;
}

size_t data_bind_schema_field_count(DataBind *codec, const char *type_name) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL) return 0;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  return fields != NULL ? fields->data.list.count : 0;
}

int data_bind_schema_field_at(DataBind *codec, const char *type_name, size_t index,
                              DataBindSchemaField *out) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || out == NULL) return 0;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  if (fields == NULL || index >= fields->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  return fill_schema_field(codec->schema_root, fields->data.list.items[index], out);
}

json_value_t *data_bind_internal_json_field_value(
    DataBind *codec, const char *type_name, size_t field_index,
    const json_value_t *object) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL ||
      object == NULL)
    return NULL;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  if (fields == NULL || field_index >= fields->data.list.count) return NULL;
  return json_field_value(fields->data.list.items[field_index], object);
}

const char *data_bind_internal_json_field_output_name(
    DataBind *codec, const char *type_name, size_t field_index) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL)
    return NULL;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  if (fields == NULL || field_index >= fields->data.list.count) return NULL;
  return field_binding_name(fields->data.list.items[field_index]);
}

size_t data_bind_internal_field_input_name_count(
    DataBind *codec, const char *type_name, size_t field_index) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL)
    return 0u;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  if (fields == NULL || field_index >= fields->data.list.count) return 0u;
  return field_input_name_count(fields->data.list.items[field_index]);
}

const char *data_bind_internal_field_input_name_at(
    DataBind *codec, const char *type_name, size_t field_index,
    size_t input_name_index) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL)
    return NULL;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  if (fields == NULL || field_index >= fields->data.list.count) return NULL;
  return field_input_name_at(fields->data.list.items[field_index],
                             input_name_index);
}

int data_bind_internal_csv_find_path_column(
    const csv_doc_t *document, const char *path, size_t *out_column) {
  return csv_find_path_column((csv_doc_t *)document, path, out_column);
}

int data_bind_internal_csv_header_matches_path(const char *header,
                                               const char *path) {
  return csv_header_matches_path(header, path);
}

DataBindStatus data_bind_schema_field_cmeta_data(DataBind *codec, const char *type_name,
                                                size_t index,
                                                const cmeta_data_desc **out_data,
                                                DataBindError *error) {
  Node *record, *fields, *field;
  schema_cmeta_field_type semantic;
  const char *name, *declared;
  char path[260];
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || out_data == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, type_name, -1, -1,
                        "Invalid CMeta field descriptor query");
  record = find_schema_record(codec->schema_root, type_name);
  if (record == NULL)
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name, -1, -1,
                        "CMeta schema type not found");
  fields = fields_node_for_record(record);
  if (fields == NULL || index >= fields->data.list.count)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, type_name, -1, -1,
                        "CMeta schema field index is out of range");
  field = fields->data.list.items[index];
  name = get_string_val(find_child(field, "name"));
  declared = get_string_val(find_child(field, "type"));
  snprintf(path, sizeof(path), "%s.%s", type_name, name != NULL ? name : "<unnamed>");
  if (!schema_cmeta_field_resolve(codec->schema_root, field, &semantic) ||
      semantic.data == NULL || semantic.data->storage_type == NULL)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, path, -1, -1,
                        "CMeta storage descriptor unresolved or gated for schema type '%s'",
                        declared != NULL ? declared : "<unknown>");
  db_error_clear(error);
  *out_data = semantic.data;
  return DATA_BIND_OK;
}

size_t data_bind_schema_enum_count(DataBind *codec) {
  Node *enums;
  if (codec == NULL || codec->schema_root == NULL) return 0;
  enums = find_child(codec->schema_root, "enums");
  return enums != NULL && enums->type == NODE_LIST ? enums->data.list.count : 0;
}

int data_bind_schema_enum_at(DataBind *codec, size_t index, DataBindSchemaType *out) {
  Node *enums;
  if (codec == NULL || codec->schema_root == NULL || out == NULL) return 0;
  enums = find_child(codec->schema_root, "enums");
  if (enums == NULL || enums->type != NODE_LIST || index >= enums->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  return fill_schema_type(enums->data.list.items[index], "enums", out);
}

size_t data_bind_schema_enum_item_count(DataBind *codec, const char *enum_name) {
  Node *record;
  Node *items;
  if (codec == NULL || codec->schema_root == NULL || enum_name == NULL) return 0;
  record = find_named_record(codec->schema_root, "enums", enum_name);
  items = items_node_for_enum(record);
  return items != NULL ? items->data.list.count : 0;
}

int data_bind_schema_enum_item_at(DataBind *codec, const char *enum_name, size_t index,
                                  DataBindSchemaEnumItem *out) {
  Node *record;
  Node *items;
  Node *item;
  size_t out_size;
  if (codec == NULL || codec->schema_root == NULL || enum_name == NULL || out == NULL) return 0;
  record = find_named_record(codec->schema_root, "enums", enum_name);
  items = items_node_for_enum(record);
  if (items == NULL || index >= items->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  item = items->data.list.items[index];
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  DB_REFLECT_SET(DataBindSchemaEnumItem, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaEnumItem, out, out_size, name,
                 get_string_val(find_child(item, "name")));
  DB_REFLECT_SET(DataBindSchemaEnumItem, out, out_size, value,
                 get_string_val(find_child(item, "value")));
  return get_string_val(find_child(item, "name")) != NULL;
}

const char *data_bind_schema_name(DataBind *codec) {
  Node *schema;
  if (codec == NULL || codec->schema_root == NULL) return NULL;
  schema = find_child(codec->schema_root, "schema");
  return get_string_val(find_child(schema, "name"));
}

size_t data_bind_schema_attribute_count(DataBind *codec) {
  Node *schema;
  Node *attrs;
  if (codec == NULL || codec->schema_root == NULL) return 0;
  schema = find_child(codec->schema_root, "schema");
  attrs = find_child(schema, "attributes");
  return attrs != NULL && attrs->type == NODE_LIST ? attrs->data.list.count : 0;
}

int data_bind_schema_attribute_at(DataBind *codec, size_t index, DataBindSchemaAttribute *out) {
  Node *schema;
  Node *attrs;
  Node *attr;
  size_t out_size;
  if (codec == NULL || codec->schema_root == NULL || out == NULL) return 0;
  schema = find_child(codec->schema_root, "schema");
  attrs = find_child(schema, "attributes");
  if (attrs == NULL || attrs->type != NODE_LIST || index >= attrs->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  attr = attrs->data.list.items[index];
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  DB_REFLECT_SET(DataBindSchemaAttribute, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaAttribute, out, out_size, name,
                 get_string_val(find_child(attr, "name")));
  DB_REFLECT_SET(DataBindSchemaAttribute, out, out_size, value,
                 get_string_val(find_child(attr, "value")));
  return get_string_val(find_child(attr, "name")) != NULL;
}

const char *data_bind_schema_attribute_get(DataBind *codec, const char *name) {
  size_t i;
  size_t count;
  if (codec == NULL || name == NULL) return NULL;
  count = data_bind_schema_attribute_count(codec);
  for (i = 0; i < count; i++) {
    DataBindSchemaAttribute attr = DATA_BIND_SCHEMA_ATTRIBUTE_INIT;
    if (data_bind_schema_attribute_at(codec, i, &attr) && attr.name != NULL &&
        strcmp(attr.name, name) == 0)
      return attr.value;
  }
  return NULL;
}

const char *data_bind_status_name(DataBindStatus status) {
  switch (status) {
  case DATA_BIND_OK:
    return "ok";
  case DATA_BIND_ERR_INVALID_ARG:
    return "invalid_arg";
  case DATA_BIND_ERR_IO:
    return "io";
  case DATA_BIND_ERR_PARSE:
    return "parse";
  case DATA_BIND_ERR_SCHEMA:
    return "schema";
  case DATA_BIND_ERR_TYPE_NOT_FOUND:
    return "type_not_found";
  case DATA_BIND_ERR_TYPE_MISMATCH:
    return "type_mismatch";
  case DATA_BIND_ERR_OOM:
    return "oom";
  case DATA_BIND_ERR_RUNTIME:
    return "runtime";
  case DATA_BIND_ERR_LIMIT:
    return "limit";
  case DATA_BIND_ERR_BUFFER_TOO_SMALL:
    return "buffer_too_small";
  case DATA_BIND_ERR_CANCELED:
    return "canceled";
  default:
    return "unknown";
  }
}

int data_bind_library_version(void) { return DATA_BIND_VERSION; }

int data_bind_abi_version(void) { return DATA_BIND_ABI_VERSION; }

const char *data_bind_version_string(void) { return "3.0.0"; }

const char *data_bind_format_name(DataBindFormat format) {
  switch (format) {
  case DATA_BIND_FORMAT_BINARY: return "bin";
  case DATA_BIND_FORMAT_JSON: return "json";
  case DATA_BIND_FORMAT_YAML: return "yaml";
  case DATA_BIND_FORMAT_CSV: return "csv";
  case DATA_BIND_FORMAT_XML: return "xml";
  default: return NULL;
  }
}
