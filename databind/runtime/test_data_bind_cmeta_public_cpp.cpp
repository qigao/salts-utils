#include "data_bind_binding_plan.h"
#include "test_data_bind_cmeta_public.c"

static_assert(std::is_standard_layout_v<DataBindBindingProjectionSlot>);
static_assert(std::is_standard_layout_v<DataBindBindingProjection>);
static_assert(std::is_standard_layout_v<DataBindNativeRecordBinding>);
static_assert(std::is_standard_layout_v<DataBindServiceNativeBinding>);
static_assert(std::is_standard_layout_v<DataBindBindingPlanEntry>);
static_assert(std::is_standard_layout_v<DataBindBindingProvider>);
