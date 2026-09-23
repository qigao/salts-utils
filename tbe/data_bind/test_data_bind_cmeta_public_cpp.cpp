#include <type_traits>

#include "data_bind_service_plan.h"
#include "test_data_bind_cmeta_public.c"

static_assert(std::is_standard_layout_v<DataBindServicePlanEntry>);
static_assert(std::is_standard_layout_v<DataBindServiceNativeBinding>);
static_assert(std::is_standard_layout_v<DataBindServicePlanDiagnostic>);
static_assert(std::is_standard_layout_v<DataBindServiceCallFrame>);
static_assert(std::is_standard_layout_v<DataBindServiceProvider>);
