#include <type_traits>

#include "data_bind_binding_plan.h"
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
static_assert(std::is_standard_layout_v<DataBindNativeErrorBinding>);
static_assert(std::is_standard_layout_v<DataBindServiceNativeBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingOutcome>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanEntry>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanDiagnostic>);

static_assert(std::is_standard_layout_v<DataBindBindingProjection>);
static_assert(std::is_standard_layout_v<DataBindNativeStateBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingCallFrame>);
static_assert(std::is_standard_layout_v<DataBindBindingProvider>);

static_assert(std::is_standard_layout_v<DataBindHttpFieldProjection>);
static_assert(std::is_standard_layout_v<DataBindHttpErrorMapping>);
static_assert(std::is_standard_layout_v<DataBindHttpProjectionConfig>);
static_assert(std::is_standard_layout_v<DataBindRpcFieldProjection>);
static_assert(std::is_standard_layout_v<DataBindRpcErrorMapping>);
static_assert(std::is_standard_layout_v<DataBindRpcProjectionConfig>);
static_assert(std::is_standard_layout_v<DataBindFormatPlanInfo>);
static_assert(std::is_standard_layout_v<DataBindTransportPlanInfo>);
