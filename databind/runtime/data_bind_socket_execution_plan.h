#ifndef DATA_BIND_SOCKET_EXECUTION_PLAN_H
#define DATA_BIND_SOCKET_EXECUTION_PLAN_H

#include "data_bind_format_provider.h"
#include "data_bind_message_plan.h"
#include "data_bind_projection_plan.h"
#include "data_bind_socket_plan.h"

#include <cserde/reader.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DataBindSocketExecutionPlan DataBindSocketExecutionPlan;

/**
 * Compile one generated immutable SocketPlan into a lookup-free runtime plan.
 *
 * The codec is a control-plane input only. The resulting execution plan copies
 * the resolved DataBindNativeTypeBinding record and owns a compiled
 * DataBindMessagePlan; it retains no codec/schema registry.
 *
 * The generated SocketPlan must expose the v2 native-binding resolver and the
 * resolver's idl_type_name must exactly match SocketPlan.message_type.
 */
DATA_BIND_API DataBindStatus data_bind_socket_execution_plan_compile(
    DataBind *codec,
    const DataBindSocketPlan *socket_plan,
    DataBindSocketExecutionPlan **out_plan,
    DataBindError *error);

DATA_BIND_API void data_bind_socket_execution_plan_free(
    DataBindSocketExecutionPlan *plan);

/** Owned immutable copy of the selected SocketPlan control-plane facts. */
DATA_BIND_API const DataBindSocketPlan *
data_bind_socket_execution_plan_socket(
    const DataBindSocketExecutionPlan *plan);

/** Borrowed native binding record owned by the execution plan. */
DATA_BIND_API const DataBindNativeTypeBinding *
data_bind_socket_execution_plan_native_binding(
    const DataBindSocketExecutionPlan *plan);

/**
 * Decode one canonical CSerde message through the compiled MessagePlan.
 *
 * The format/framing layer must already have produced canonical field-name
 * tokens. This hot path performs no codec/schema lookup.
 */
DATA_BIND_API DataBindStatus data_bind_socket_execution_plan_decode_native(
    const DataBindSocketExecutionPlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic);

/**
 * Decode one already-framed Socket payload through the selected FormatPlan.
 *
 * Framing/datagram extraction is owned by CNet/the transport runtime. The
 * caller passes one complete payload and one explicit format provider whose
 * provider.format must exactly equal the generated SocketPlan format. No
 * provider registry lookup, fallback or format substitution occurs.
 *
 * Startup compilation owns the immutable FormatPlan. The hot path opens a
 * provider reader, canonicalizes root field names through that plan, delegates
 * to MessagePlan, and closes the provider lease on every path.
 *
 * format_error reports provider/canonical-reader failures. message_diagnostic
 * reports DataBind state/native/validation failures after canonicalization.
 */
DATA_BIND_API DataBindStatus data_bind_socket_execution_plan_decode_payload(
    const DataBindSocketExecutionPlan *plan,
    const DataBindFormatProvider *provider,
    const void *payload,
    size_t payload_bytes,
    size_t max_depth,
    const DataBindNativeOptions *native_options,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *message_diagnostic,
    DataBindError *format_error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_SOCKET_EXECUTION_PLAN_H */
