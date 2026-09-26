#ifndef DATA_BIND_MESSAGE_PLAN_H
#define DATA_BIND_MESSAGE_PLAN_H

#include "data_bind.h"
#include "data_bind_native_binding.h"
#include "data_bind_native.h"

#include <cserde/reader.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_MESSAGE_PLAN_ABI_VERSION = 1u };

typedef struct DataBindMessagePlan DataBindMessagePlan;

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
 * ValidationPlan. It borrows only immutable generated CMeta/native binding
 * metadata. It owns no FunctionDesc, Service operation, format or transport.
 *
 * The current native-validation slice admits direct record constraints only;
 * nested ValidationPlan execution fails closed instead of falling back to
 * schema/reflection lookup at runtime.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_compile(
    DataBind *codec,
    const char *type_name,
    const DataBindNativeTypeBinding *native,
    DataBindMessagePlan **out_plan,
    DataBindMessagePlanDiagnostic *diagnostic);

DATA_BIND_API void data_bind_message_plan_free(DataBindMessagePlan *plan);

DATA_BIND_API const char *
data_bind_message_plan_type_name(const DataBindMessagePlan *plan);

DATA_BIND_API const DataBindNativeTypeBinding *
data_bind_message_plan_native_binding(const DataBindMessagePlan *plan);

DATA_BIND_API size_t
data_bind_message_plan_field_count(const DataBindMessagePlan *plan);

/**
 * Validate one already-normalized complete native message.
 *
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
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_decode_native(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_MESSAGE_PLAN_H */
