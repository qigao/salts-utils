#ifndef DATA_BIND_CMETA_ADAPTER_PLAN_H
#define DATA_BIND_CMETA_ADAPTER_PLAN_H

#include <cmeta/function.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const cmeta_function_desc *(*DataBindCMetaFunctionAtFn)(
    void *context, size_t index);

typedef struct DataBindCMetaFunctionManifest {
  size_t count;
  DataBindCMetaFunctionAtFn at;
  void *context;
} DataBindCMetaFunctionManifest;

typedef bool (*DataBindCMetaAdapterPlanWriteFn)(
    void *context, const char *data, size_t size);

typedef struct DataBindCMetaAdapterPlanConfig {
  const char *symbol_prefix;
} DataBindCMetaAdapterPlanConfig;

typedef enum DataBindCMetaAdapterPlanStatus {
  DATA_BIND_CMETA_ADAPTER_PLAN_OK = 0,
  DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_ARGUMENT,
  DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_DESCRIPTOR,
  DATA_BIND_CMETA_ADAPTER_PLAN_UNSUPPORTED_TYPE,
  DATA_BIND_CMETA_ADAPTER_PLAN_WRITE_FAILED,
  DATA_BIND_CMETA_ADAPTER_PLAN_OUT_OF_MEMORY
} DataBindCMetaAdapterPlanStatus;

/*
 * Producer-only projection from an external retained CMeta manifest into one
 * self-contained C11 header with immutable static adapter-plan data.
 *
 * Supported semantic carriers are intentionally narrow:
 *   parameter: uint32, uint64
 *   return:    void, uint32, uint64
 *
 * Admission uses canonical cmeta_function_desc_valid()/cmeta_type_equal()
 * rules and fails closed for every other type. Generated output contains no
 * DataBind runtime object, ownership requirement, or SaltsUtils include.
 *
 * Function order is retained. The emitted rows retain function/parameter
 * names, parameter flags, effects,
 * properties and projected semantic carriers: for the admitted scalar set
 * these are exactly the semantic fields observed by cmeta_function_desc_equal().
 * This lets regeneration/golden qualification detect descriptor drift without
 * retaining CMeta objects in the generated consumer artifact.
 *
 * out_error_index receives SIZE_MAX for non-function-specific failures.
 */
DataBindCMetaAdapterPlanStatus data_bind_cmeta_adapter_plan_emit(
    const DataBindCMetaFunctionManifest *manifest,
    const DataBindCMetaAdapterPlanConfig *config,
    DataBindCMetaAdapterPlanWriteFn write,
    void *write_context,
    size_t *out_error_index);

const char *data_bind_cmeta_adapter_plan_status_name(
    DataBindCMetaAdapterPlanStatus status);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_CMETA_ADAPTER_PLAN_H */
