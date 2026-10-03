#include <type_traits>

#include "data_bind_binding_plan.h"
#include "data_bind_format_provider.h"
#include "data_bind_message_plan.h"
#include "data_bind_native_binding.h"
#include "data_bind_method_plan.h"
#include "data_bind_projection_plan.h"
#include "data_bind_validation_plan.h"
#include "test_data_bind_cmeta_public.c"

static_assert(std::is_standard_layout_v<DataBindSchemaConstraint>);
static_assert(std::is_standard_layout_v<DataBindBindingAddress>);
static_assert(std::is_standard_layout_v<DataBindMessagePlanDiagnostic>);
static_assert(std::is_standard_layout_v<DataBindNativeTypeBinding>);
static_assert(std::is_standard_layout_v<DataBindNativeExecution>);
static_assert(std::is_standard_layout_v<DataBindNativeErrorBinding>);
static_assert(std::is_standard_layout_v<DataBindServiceNativeBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingOutcome>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanEntry>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanDiagnostic>);

static_assert(std::is_standard_layout_v<DataBindBindingProjection>);
static_assert(std::is_standard_layout_v<DataBindNativeStateBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingCallFrame>);
static_assert(std::is_standard_layout_v<DataBindBindingProvider>);

using ClientWriteInputs = DataBindStatus (*)(
    const DataBindBindingPlan *,
    const DataBindBindingProvider *,
    const DataBindBindingCallFrame *,
    DataBindBindingPlanDiagnostic *);
using ClientBindOutcome = DataBindStatus (*)(
    const DataBindBindingPlan *,
    const DataBindBindingProvider *,
    const DataBindNativeOptions *,
    DataBindBindingCallFrame *,
    const DataBindBindingOutcome *,
    DataBindBindingPlanDiagnostic *);
using ServiceNativeErrorRestore = DataBindStatus (*)(
    const DataBindServiceNativeBinding *,
    void *,
    size_t,
    DataBindError *);
using MessageNativeEncode = DataBindStatus (*)(
    const DataBindMessagePlan *,
    const DataBindNativeOptions *,
    const void *,
    size_t,
    cserde_writer *,
    DataBindMessagePlanDiagnostic *);
using BuiltinFormatProvider =
    const DataBindFormatProvider *(*)(DataBindFormat);
static_assert(std::is_same_v<
              decltype(&data_bind_binding_plan_write_inputs),
              ClientWriteInputs>);
static_assert(std::is_same_v<
              decltype(&data_bind_binding_plan_bind_outcome),
              ClientBindOutcome>);
static_assert(std::is_same_v<
              decltype(&data_bind_service_native_error_restore_zero),
              ServiceNativeErrorRestore>);
static_assert(std::is_same_v<
              decltype(&data_bind_message_plan_encode_native),
              MessageNativeEncode>);
static_assert(std::is_same_v<
              decltype(&data_bind_builtin_format_provider),
              BuiltinFormatProvider>);

static_assert(std::is_standard_layout_v<DataBindHttpFieldProjection>);
static_assert(std::is_standard_layout_v<DataBindHttpErrorMapping>);
static_assert(std::is_standard_layout_v<DataBindHttpProjectionConfig>);
static_assert(std::is_standard_layout_v<DataBindRpcFieldProjection>);
static_assert(std::is_standard_layout_v<DataBindRpcErrorMapping>);
static_assert(std::is_standard_layout_v<DataBindRpcProjectionConfig>);
static_assert(std::is_standard_layout_v<DataBindFormatProvider>);
static_assert(std::is_standard_layout_v<DataBindFormatWriter>);
static_assert(std::is_standard_layout_v<DataBindFormatPlanInfo>);
static_assert(std::is_standard_layout_v<DataBindFormatCanonicalWriter>);
static_assert(std::is_standard_layout_v<DataBindTransportPlanInfo>);
