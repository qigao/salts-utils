#include <type_traits>

#include "data_bind_binding_plan.h"
#include "test_data_bind_cmeta_public.c"

static_assert(std::is_standard_layout_v<DataBindBindingAddress>);
static_assert(std::is_standard_layout_v<DataBindNativeTypeBinding>);
static_assert(std::is_standard_layout_v<DataBindServiceNativeBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanEntry>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanDiagnostic>);

static_assert(std::is_standard_layout_v<DataBindBindingProjection>);
static_assert(std::is_standard_layout_v<DataBindNativeStateBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingCallFrame>);
static_assert(std::is_standard_layout_v<DataBindBindingProvider>);
