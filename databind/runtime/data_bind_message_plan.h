#ifndef DATA_BIND_MESSAGE_PLAN_H
#define DATA_BIND_MESSAGE_PLAN_H

#include "data_bind.h"
#include "data_bind_native_binding.h"
#include "data_bind_native.h"

#include <cserde/reader.h>
#include <cserde/writer.h>
#include <cmeta/object.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_MESSAGE_PLAN_ABI_VERSION = 2u };

typedef struct DataBindMessagePlan DataBindMessagePlan;

#ifndef DATA_BIND_MESSAGE_PLAN_MAX_PREPARED
#define DATA_BIND_MESSAGE_PLAN_MAX_PREPARED 4096u
#endif
#ifndef DATA_BIND_MESSAGE_PLAN_MAX_PREPARED_FIELDS
#define DATA_BIND_MESSAGE_PLAN_MAX_PREPARED_FIELDS 65536u
#endif

typedef enum DataBindMessageObjectFieldState {
  DATA_BIND_MESSAGE_OBJECT_ABSENT = 0,
  DATA_BIND_MESSAGE_OBJECT_VALUE = 1,
  DATA_BIND_MESSAGE_OBJECT_NULL = 2
} DataBindMessageObjectFieldState;

typedef DataBindStatus (*DataBindMessageObjectGetStateFn)(
    void *context, const cmeta_object_ref *object, const char *field_name,
    DataBindMessageObjectFieldState *out_state, DataBindError *error);
typedef DataBindStatus (*DataBindMessageObjectSetStateFn)(
    void *context, cmeta_object_ref *object, const char *field_name,
    DataBindMessageObjectFieldState state, DataBindError *error);

/**
 * Optional DataBind-owned state overlay for dynamic objects.
 *
 * CMeta owns VALUE access through cmeta_object_field_read/assign. This provider
 * owns only DataBind ABSENT/NULL/VALUE state. Required non-nullable fields need
 * no state provider. Provider and diagnostic records require the exact current
 * size and ABI; invalid diagnostics are rejected without modifying them.
 */
typedef struct DataBindMessageObjectStateProvider {
  size_t size;
  uint32_t abi_version;
  void *context;
  DataBindMessageObjectGetStateFn get_state;
  DataBindMessageObjectSetStateFn set_state;
} DataBindMessageObjectStateProvider;

#define DATA_BIND_MESSAGE_OBJECT_STATE_PROVIDER_INIT \
  { sizeof(DataBindMessageObjectStateProvider), DATA_BIND_MESSAGE_PLAN_ABI_VERSION, \
    NULL, NULL, NULL }

typedef struct DataBindMessagePlanDiagnostic {
  size_t size;
  uint32_t abi_version;
  DataBindStatus status;
  char schema_field[96];
  char message[256];
} DataBindMessagePlanDiagnostic;

#define DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT \
  { sizeof(DataBindMessagePlanDiagnostic), DATA_BIND_MESSAGE_PLAN_ABI_VERSION, \
    DATA_BIND_OK, {0}, {0} }

/**
 * Compile one canonical DataBind record against one exact generated native
 * binding.
 *
 * The plan owns DataBind presence/null/default/constraint facts and a compiled
 * ValidationPlan. It copies the native binding record and borrows its immutable
 * CMeta/state metadata. It owns no FunctionDesc, Service operation, format or transport.
 *
 * Native validation executes the compiled ValidationPlan directly. Admitted
 * nested Struct/list/set/map bindings traverse canonical CMeta field and borrow
 * providers; no runtime schema/reflection lookup or dynamic value tree is used.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_compile(
    DataBind *codec,
    const char *type_name,
    const DataBindNativeTypeBinding *native,
    DataBindMessagePlan **out_plan,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Prepare or borrow one codec-owned generated MessagePlan.
 *
 * artifact and its CMeta/state metadata must remain immutable and alive until
 * data_bind_free(codec). The artifact address is a preparation key, not a CMeta
 * type identity. A cold acquisition resolves and compiles that exact binding;
 * a warm acquisition compares only artifact addresses and performs no schema
 * lookup, resolver call or plan allocation. Call before starting the data path
 * to prepare explicitly. A failed candidate publishes nothing and has no
 * alternative execution path.
 *
 * The codec owns the returned plan; never call data_bind_message_plan_free on
 * it. Its address remains stable until codec destruction, which requires all
 * acquisitions and executions to be quiescent. Concurrent acquisition is MPMC:
 * candidates compile outside the codec mutex, and one complete candidate is
 * published per artifact. Duplicate candidates are destroyed outside the lock.
 * Execution can share the immutable plan with separate per-call workspaces.
 *
 * Each codec admits at most MAX_PREPARED plans and MAX_PREPARED_FIELDS total
 * field records. Full returns LIMIT; allocation failure returns OOM. Existing
 * plans remain available at capacity. out_plan is NULL on failure.
 *
 * codec and out_plan must be non-NULL. artifact must contain the complete
 * current ABI record, a non-empty type name and a binding resolver; invalid
 * arguments return INVALID_ARG. Binding and schema failures propagate their
 * status. error is optional and receives the preparation diagnostic.
 * See test_generated_message_plan.c for executable acquisition, concurrent
 * use, ownership and codec-destruction examples.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_acquire_generated(
    DataBind *codec,
    const DataBindMessageNativeArtifact *artifact,
    const DataBindMessagePlan **out_plan,
    DataBindError *error);

/**
 * Compile one canonical DataBind record against a provider-backed CMeta object
 * surface. Field VALUE access is resolved through cmeta_object_field_read/assign;
 * field offsets are descriptive only and may be CMETA_FIELD_DYNAMIC_OFFSET.
 *
 * The plan borrows object_data and copies logical DataBind/validation metadata.
 * It performs no schema lookup after compilation.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_compile_object(
    DataBind *codec,
    const char *type_name,
    const cmeta_data_desc *object_data,
    DataBindMessagePlan **out_plan,
    DataBindMessagePlanDiagnostic *diagnostic);

DATA_BIND_API void data_bind_message_plan_free(DataBindMessagePlan *plan);

DATA_BIND_API const char *
data_bind_message_plan_type_name(const DataBindMessagePlan *plan);

DATA_BIND_API const DataBindNativeTypeBinding *
data_bind_message_plan_native_binding(const DataBindMessagePlan *plan);

/** Return the provider-backed object data surface, or NULL for native plans. */
DATA_BIND_API const cmeta_data_desc *
data_bind_message_plan_object_data(const DataBindMessagePlan *plan);

DATA_BIND_API size_t
data_bind_message_plan_field_count(const DataBindMessagePlan *plan);

/**
 * Validate one already-normalized complete native message.
 *
 * Contradictory ABSENT plus NULL native state returns DATA_BIND_ERR_SCHEMA.
 * ABSENT optional fields and explicit NULL fields skip value constraints.
 * VALUE fields execute the immutable native ValidationPlan binding. No schema
 * lookup or constraint parsing occurs on this runtime path.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_validate_native(
    const DataBindMessagePlan *plan,
    const void *source,
    size_t source_bytes,
    DataBindError *error);

/**
 * Validate one provider-backed object through compiled ValidationPlan rules.
 * Optional/null state is read only from state_provider; required/non-nullable
 * records may pass NULL.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_validate_object(
    const DataBindMessagePlan *plan,
    const cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindError *error);

/**
 * Decode exactly one canonical CSerde MAP into caller-owned native staging.
 *
 * The reader is expected to expose canonical DataBind field names after any
 * format-specific external-name mapping. MessagePlan owns required/optional,
 * nullable, default and validation semantics; it performs no schema/reflection
 * lookup on this runtime path.
 *
 * destination is fresh caller-owned staging/raw storage and must not contain a
 * live native value with provider-owned resources. Reused storage must first be
 * returned to canonical semantic zero through its normal lifecycle. The call
 * initializes staging to semantic zero before decode. On every failure after
 * initialization, the complete native value and DataBind state-overlay bytes
 * are restored to semantic zero. The caller publishes the destination only
 * after DATA_BIND_OK.
 *
 * The function consumes one MAP value and does not probe for a following value
 * or EOF. It uses only the caller-provided DataBindNativeOptions workspace:
 * a bounded field-seen bitmap is reserved from the front, and exact field
 * native decoders use the remaining workspace. No runtime heap allocation is
 * performed by MessagePlan.
 *
 * Lists, fixed arrays and sets admit exact builtin and enum semantics,
 * owned STRING/BYTES providers, or admitted nested Struct descriptors.
 * String-key maps admit the same value contracts. Integer width/signedness
 * and fixed-array cardinality must match the canonical Contract.
 * CSTL/provider collectors own their bounded element storage and cleanup.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_decode_native(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Decode one native message using explicit format token semantics.
 *
 * JSON admits UTF-8 STRING tokens for root fields whose canonical descriptor
 * is BYTES, copying their complete length into provider-owned byte storage.
 * Embedded NUL is preserved. YAML retains the strict canonical token rules of
 * data_bind_message_plan_decode_native(). CSV and XML additionally admit
 * textual leaf scalar coercion through each field's canonical CMeta descriptor:
 * BOOL/SINT/UINT/FLOAT text becomes the corresponding CSerde scalar token
 * before native decode. XML applies this conversion recursively to admitted
 * nested records; XML enum text also accepts decimal integers/canonical unsigned
 * bit patterns, with width and membership enforced by the enum provider.
 * Symbolic enum names remain valid. Binary readers supply scalar tokens directly.
 * String/bytes/container semantics are unchanged; nested bytes do not receive
 * the root-field JSON projection. JSON numeric strings remain invalid.
 * CSV row selection, empty-cell omission and header-name canonicalization are
 * provider/FormatPlan concerns and are not performed by this API.
 *
 * No provider selection or parsing occurs here; the caller supplies the exact
 * CSerde reader for the declared format.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_decode_native_format(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindFormat format,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Encode one complete native message as a canonical CSerde MAP.
 *
 * DataBind optional/null state is read only from the exact generated native
 * binding compiled into the plan. ABSENT optional fields are omitted, explicit
 * NULL writes CSERDE_NULL, and VALUE fields are validated then encoded through
 * their canonical CMeta descriptors.
 *
 * Contradictory ABSENT plus NULL state returns DATA_BIND_ERR_SCHEMA before
 * emitting the first token; the diagnostic identifies the field.
 * The source remains caller-owned and is never mutated. As with the underlying
 * CSerde writer/native encoder, this API does not claim transport-level output
 * rollback after a sink has accepted tokens.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_encode_native(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    const void *source,
    size_t source_bytes,
    cserde_writer *writer,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Decode one canonical CSerde MAP into a fresh provider-backed object.
 *
 * The caller owns object lifetime and must discard/destroy the staging object
 * on failure. This API never claims bytewise rollback for arbitrary dynamic
 * runtimes. Each VALUE is decoded into bounded temporary native storage,
 * validated, then published through cmeta_object_field_assign().
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_decode_object(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Decode one provider-backed object using explicit format semantics.
 *
 * JSON/YAML preserve the strict canonical token rules of
 * data_bind_message_plan_decode_object(). XML additionally admits textual leaf
 * scalar coercion through the target field's canonical CMeta data descriptor:
 * BOOL/SINT/UINT/FLOAT text becomes the corresponding CSerde scalar token
 * before native decode. String/bytes/enum and other domains retain their
 * existing token semantics.
 *
 * No runtime heap allocation is introduced; XML float text temporarily borrows
 * the caller-supplied native workspace before it is converted to a numeric
 * token.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_decode_object_format(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindFormat format,
    cserde_reader *reader,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Encode one provider-backed object as a canonical CSerde MAP. Field VALUE
 * borrows come only from cmeta_object_field_read(); optional/null state comes
 * only from state_provider.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_encode_object(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    const cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    cserde_writer *writer,
    DataBindMessagePlanDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_MESSAGE_PLAN_H */
