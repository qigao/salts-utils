#ifndef TBE_CBIND_TBE_CBIND_H
#define TBE_CBIND_TBE_CBIND_H

#include <cbind/cbind.h>
#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbe_cbind_plan tbe_cbind_plan;

enum {
  TBE_CBIND_OPTIONS_ABI_VERSION = 1u,
  TBE_CBIND_ERROR_ABI_VERSION = 1u
};

typedef struct tbe_cbind_plan_options {
  size_t struct_size;
  uint32_t abi_version;
  size_t max_schema_bytes;
  size_t max_types;
  size_t max_fields;
  size_t max_depth;
  size_t max_name_bytes;
  size_t max_plan_bytes;
} tbe_cbind_plan_options;

typedef enum tbe_cbind_status {
  TBE_CBIND_OK = 0,
  TBE_CBIND_INVALID_ARGUMENT,
  TBE_CBIND_INVALID_OPTIONS,
  TBE_CBIND_SCHEMA_ERROR,
  TBE_CBIND_TYPE_NOT_FOUND,
  TBE_CBIND_NATIVE_SHAPE_ERROR,
  TBE_CBIND_TYPE_MISMATCH,
  TBE_CBIND_LIMIT_EXCEEDED,
  TBE_CBIND_UNSUPPORTED,
  TBE_CBIND_OUT_OF_MEMORY
} tbe_cbind_status;

typedef enum tbe_cbind_error_phase {
  TBE_CBIND_PHASE_NONE = 0,
  TBE_CBIND_PHASE_PARSE,
  TBE_CBIND_PHASE_SCHEMA,
  TBE_CBIND_PHASE_NATIVE_SHAPE,
  TBE_CBIND_PHASE_PLAN
} tbe_cbind_error_phase;

typedef struct tbe_cbind_plan_error {
  size_t struct_size;
  uint32_t abi_version;
  tbe_cbind_status status;
  tbe_cbind_error_phase phase;
  int line;
  int column;
  cmeta_status target_status;
  size_t field_index;
  char path[256];
  char message[256];
} tbe_cbind_plan_error;

/** Initialize bounded v1 defaults: 1 MiB schema, 1024 types, 65536 fields,
 * 64 nesting levels, 255-byte names, and a 16 MiB ready plan. */
void tbe_cbind_plan_options_init(tbe_cbind_plan_options *options);

/** Initialize a reusable v1 plan-build diagnostic. */
void tbe_cbind_plan_error_init(tbe_cbind_plan_error *error);

/** Compile a borrowed schema byte slice and borrowed immutable native storage
 * graph into an immutable plan. Input slices need not be NUL terminated and
 * are not retained. On every failure, a non-NULL @p out is set to NULL.
 *
 * The native descriptor graph and its callbacks remain borrowed and must stay
 * immutable, reentrant, and alive until the plan is destroyed. Factory calls
 * must be externally serialized because the TbeSchema parser is control-plane
 * state. */
tbe_cbind_status tbe_cbind_plan_create_from_text(
    const char *schema_text, size_t schema_size, const char *type_name,
    size_t type_name_size, const cmeta_data_desc *native_shape,
    const tbe_cbind_plan_options *options, tbe_cbind_plan **out,
    tbe_cbind_plan_error *error);

/** Destroy a ready plan after all concurrent decode calls have joined. */
void tbe_cbind_plan_destroy(tbe_cbind_plan *plan);

/** Return the plan-owned immutable semantic overlay, or NULL for no plan. */
const cmeta_data_desc *tbe_cbind_plan_shape(const tbe_cbind_plan *plan);

/** Delegate decode to CBind with the plan-owned overlay.
 *
 * The plan may be shared across threads only when each call has independent
 * context/scratch, reader, destination, and error state. cbind_error shape and
 * field pointers may refer into the plan and become invalid at plan destroy.
 * Borrowed STRING values additionally require CSERDE_VIEW_STABLE backing whose
 * owner remains alive until the native destination is cleared. */
cbind_status tbe_cbind_plan_decode(
    const tbe_cbind_plan *plan, const cbind_context *context,
    cserde_reader *reader, void *out, cbind_error *error);

#ifdef __cplusplus
}
#endif

#endif /* TBE_CBIND_TBE_CBIND_H */
