#include <salts/plugin_cflow.h>

#include <type_traits>

static_assert(std::is_standard_layout<
                  salts_plugin_cflow_publisher_binding>::value,
              "Publisher binding must remain C-compatible");
static_assert(std::is_standard_layout<
                  salts_plugin_cflow_executor_binding>::value,
              "Executor binding must remain C-compatible");
static_assert(std::is_standard_layout<
                  salts_plugin_cflow_scheduler_binding>::value,
              "Scheduler binding must remain C-compatible");

int main() {
    salts_plugin_cflow_publisher_binding publisher{};
    salts_plugin_cflow_executor_binding executor{};
    salts_plugin_cflow_scheduler_binding scheduler{};
    return publisher.publisher == nullptr &&
                   executor.executor == nullptr &&
                   scheduler.scheduler == nullptr
               ? 0
               : 1;
}
