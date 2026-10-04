#ifndef DATA_BIND_PROJECTION_PLAN_H
#define DATA_BIND_PROJECTION_PLAN_H

#include "data_bind.h"

#include <cserde/reader.h>
#include <cserde/writer.h>

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

/*
 * Caller-owned root-record canonicalizing CSerde reader.
 *
 * The wrapper rewrites only MAP key STRING tokens at the selected root record
 * from compiled external primary/alias names to canonical DataBind field names.
 * Value/container token streams are otherwise forwarded unchanged.
 *
 * This first v1 reader intentionally does not canonicalize nested-record field
 * names. FormatPlan compilation fails when nested declared records require a
 * non-identity name/alias mapping, rather than silently accepting partial
 * semantics.
 */
typedef struct DataBindFormatCanonicalReader {
  size_t size;
  uint32_t abi_version;
  const DataBindFormatPlan *plan;
  cserde_reader *source;
  cserde_reader reader;
  size_t value_depth;
  int root_started;
  int expect_root_key;
  int complete;
} DataBindFormatCanonicalReader;

enum { DATA_BIND_FORMAT_CANONICAL_READER_ABI_VERSION = 1u };

#define DATA_BIND_FORMAT_CANONICAL_READER_INIT \
  { sizeof(DataBindFormatCanonicalReader), \
    DATA_BIND_FORMAT_CANONICAL_READER_ABI_VERSION, \
    NULL, NULL, {0}, 0u, 0, 0, 0 }

/*
 * Caller-owned root-record egress CSerde writer.
 *
 * The wrapper accepts canonical root MAP keys and rewrites them to the
 * FormatPlan's compiled primary external names. Input aliases are never emitted.
 * Nested value token streams are forwarded unchanged.
 */
typedef struct DataBindFormatCanonicalWriter {
  size_t size;
  uint32_t abi_version;
  const DataBindFormatPlan *plan;
  cserde_writer *target;
  cserde_writer writer;
  size_t value_depth;
  int root_started;
  int expect_root_key;
  int complete;
} DataBindFormatCanonicalWriter;

enum { DATA_BIND_FORMAT_CANONICAL_WRITER_ABI_VERSION = 1u };

#define DATA_BIND_FORMAT_CANONICAL_WRITER_INIT \
  { sizeof(DataBindFormatCanonicalWriter), \
    DATA_BIND_FORMAT_CANONICAL_WRITER_ABI_VERSION, \
    NULL, NULL, {0}, 0u, 0, 0, 0 }

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
 * compiled.
 *
 * XML admits nested object/scalar structure but has no implicit collection or
 * variant encoding. list/set/map/group fields and union/variant shapes fail
 * closed until an explicit XML projection policy is compiled. Element versus
 * attribute placement, namespaces and nil semantics are projection concerns;
 * none are inferred from canonical IDL.
 *
 * No implicit flattening or transport-local fallback is performed.
 */
DATA_BIND_API DataBindStatus data_bind_format_plan_compile(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    DataBindFormatPlan **out_plan,
    DataBindError *error);

/*
 * Compile the reader side of one format representation.
 *
 * Shape and field-name admission are identical to data_bind_format_plan_compile,
 * but this form does not require the source format to preserve every logical
 * output state. In particular, XML/CSV readers may bind nullable contracts when
 * the concrete input supplies only ABSENT/VALUE; an explicit NULL token is
 * still governed by MessagePlan and the provider's actual token semantics.
 *
 * Use the full compile entry point for egress/publication admission.
 */
DATA_BIND_API DataBindStatus data_bind_format_plan_compile_reader(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    DataBindFormatPlan **out_plan,
    DataBindError *error);

DATA_BIND_API void data_bind_format_plan_free(DataBindFormatPlan *plan);

DATA_BIND_API int data_bind_format_plan_info(
    const DataBindFormatPlan *plan,
    DataBindFormatPlanInfo *out);

/**
 * Initialize a caller-owned CSerde reader that canonicalizes root-record field
 * names according to one immutable FormatPlan.
 *
 * The source reader is borrowed and must outlive the wrapper. No allocation,
 * schema lookup, provider lookup or fallback occurs on this runtime path.
 *
 * On success, pass data_bind_format_canonical_reader_reader(out) to MessagePlan
 * or another canonical-field consumer. The wrapper consumes exactly the same
 * token stream as source except that admitted root MAP keys are replaced by
 * stable plan-owned canonical field-name slices. Unknown external names return
 * CSERDE_INVALID_TOKEN through the wrapper reader.
 */
DATA_BIND_API DataBindStatus data_bind_format_canonical_reader_init(
    const DataBindFormatPlan *plan,
    cserde_reader *source,
    DataBindFormatCanonicalReader *out,
    DataBindError *error);

DATA_BIND_API cserde_reader *data_bind_format_canonical_reader_reader(
    DataBindFormatCanonicalReader *reader);

/**
 * Initialize the symmetric FormatPlan egress wrapper.
 *
 * target is borrowed and remains caller-owned. Finishing this wrapper validates
 * one complete canonical root value but deliberately does not finish target;
 * the format-provider lease owns concrete writer finalization.
 */
DATA_BIND_API DataBindStatus data_bind_format_canonical_writer_init(
    const DataBindFormatPlan *plan,
    cserde_writer *target,
    DataBindFormatCanonicalWriter *out,
    DataBindError *error);

DATA_BIND_API cserde_writer *data_bind_format_canonical_writer_writer(
    DataBindFormatCanonicalWriter *writer);

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
