#ifndef DATA_BIND_PROJECTION_PLAN_H
#define DATA_BIND_PROJECTION_PLAN_H

#include "data_bind.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_PROJECTION_PLAN_ABI_VERSION = 1u };

/*
 * Logical value states that one compiled format can preserve for a selected
 * DataBind contract. VALUE is always required. ABSENT and NULL remain distinct.
 */
enum DataBindFormatStateFlags {
  DATA_BIND_FORMAT_STATE_VALUE = UINT32_C(1) << 0,
  DATA_BIND_FORMAT_STATE_ABSENT = UINT32_C(1) << 1,
  DATA_BIND_FORMAT_STATE_NULL = UINT32_C(1) << 2
};

typedef enum DataBindTransportKind {
  DATA_BIND_TRANSPORT_UNKNOWN = 0,
  DATA_BIND_TRANSPORT_HTTP = 1,
  DATA_BIND_TRANSPORT_RPC = 2
} DataBindTransportKind;

typedef struct DataBindFormatPlan DataBindFormatPlan;
typedef struct DataBindTransportPlan DataBindTransportPlan;

/** Size-prefixed immutable snapshot of one compiled FormatPlan. */
typedef struct DataBindFormatPlanInfo {
  size_t size;
  uint32_t abi_version;
  const char *type_name;
  DataBindFormat format;
  uint32_t value_states;
  int has_optional;
  int has_nullable;
} DataBindFormatPlanInfo;

#define DATA_BIND_FORMAT_PLAN_INFO_INIT \
  { sizeof(DataBindFormatPlanInfo), DATA_BIND_PROJECTION_PLAN_ABI_VERSION, \
    NULL, DATA_BIND_FORMAT_BINARY, 0u, 0, 0 }

/** Size-prefixed immutable snapshot of one compiled TransportPlan. */
typedef struct DataBindTransportPlanInfo {
  size_t size;
  uint32_t abi_version;
  DataBindTransportKind kind;
  const char *service_name;
  const char *operation_name;
  const DataBindFormatPlan *ingress;
  const DataBindFormatPlan *egress;
} DataBindTransportPlanInfo;

#define DATA_BIND_TRANSPORT_PLAN_INFO_INIT \
  { sizeof(DataBindTransportPlanInfo), DATA_BIND_PROJECTION_PLAN_ABI_VERSION, \
    DATA_BIND_TRANSPORT_UNKNOWN, NULL, NULL, NULL, NULL }

/*
 * Compile one format representation against canonical DataBind schema
 * semantics. The resulting plan owns all execution facts it needs and performs
 * no schema lookup when queried.
 *
 * JSON/YAML/Binary preserve ABSENT/NULL/VALUE. The current CSV/XML 4.0
 * profiles preserve ABSENT/VALUE but cannot represent explicit logical NULL;
 * a contract containing nullable fields therefore fails admission instead of
 * collapsing NULL into ABSENT or VALUE.
 *
 * CSV additionally admits only a flat message/composite whose fields are
 * scalar-like (including enum/flags). Nested records, unions, groups and
 * list/set/map fields fail closed until an explicit projection mapping is
 * compiled. No implicit flattening or transport-local fallback is performed.
 */
DATA_BIND_API DataBindStatus data_bind_format_plan_compile(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    DataBindFormatPlan **out_plan,
    DataBindError *error);

DATA_BIND_API void data_bind_format_plan_free(DataBindFormatPlan *plan);

DATA_BIND_API int data_bind_format_plan_info(
    const DataBindFormatPlan *plan,
    DataBindFormatPlanInfo *out);

/*
 * Compile the format-neutral transport shell for one Service operation.
 * The transport owns independent ingress/egress FormatPlans for the canonical
 * request/response types. A void side has no FormatPlan.
 */
DATA_BIND_API DataBindStatus data_bind_transport_plan_compile_service(
    DataBind *codec,
    const char *service_name,
    const char *operation_name,
    DataBindTransportKind kind,
    DataBindFormat ingress_format,
    DataBindFormat egress_format,
    DataBindTransportPlan **out_plan,
    DataBindError *error);

DATA_BIND_API void data_bind_transport_plan_free(DataBindTransportPlan *plan);

DATA_BIND_API int data_bind_transport_plan_info(
    const DataBindTransportPlan *plan,
    DataBindTransportPlanInfo *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_BIND_PROJECTION_PLAN_H */
