#include "data_bind_message_plan.h"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindMessageObjectStateProvider>::value,
              "object state provider must remain C-compatible");
static_assert(std::is_same<
                  decltype(&data_bind_message_plan_compile_object),
                  DataBindStatus (*)(DataBind *, const char *,
                                     const cmeta_data_desc *,
                                     DataBindMessagePlan **,
                                     DataBindMessagePlanDiagnostic *)>::value,
              "object MessagePlan compile signature drift");
static_assert(std::is_same<
                  decltype(&data_bind_message_plan_validate_object),
                  DataBindStatus (*)(const DataBindMessagePlan *,
                                     const cmeta_object_ref *,
                                     const DataBindMessageObjectStateProvider *,
                                     DataBindError *)>::value,
              "object MessagePlan validate signature drift");
static_assert(std::is_same<
                  decltype(&data_bind_message_plan_decode_object),
                  DataBindStatus (*)(const DataBindMessagePlan *,
                                     const DataBindNativeOptions *,
                                     cserde_reader *, cmeta_object_ref *,
                                     const DataBindMessageObjectStateProvider *,
                                     DataBindMessagePlanDiagnostic *)>::value,
              "object MessagePlan decode signature drift");
static_assert(std::is_same<
                  decltype(&data_bind_message_plan_encode_object),
                  DataBindStatus (*)(const DataBindMessagePlan *,
                                     const DataBindNativeOptions *,
                                     const cmeta_object_ref *,
                                     const DataBindMessageObjectStateProvider *,
                                     cserde_writer *,
                                     DataBindMessagePlanDiagnostic *)>::value,
              "object MessagePlan encode signature drift");

int main() {
  DataBindMessageObjectStateProvider provider =
      DATA_BIND_MESSAGE_OBJECT_STATE_PROVIDER_INIT;
  return provider.abi_version == DATA_BIND_MESSAGE_PLAN_ABI_VERSION ? 0 : 1;
}
