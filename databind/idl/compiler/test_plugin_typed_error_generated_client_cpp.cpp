#include "error.plugin_client.h"

#include <type_traits>

using Client = databind_plugin_client_11_ErrorPlugin_11_StorePlugin;
using Error = databind_11_ErrorPlugin_5_Store_4_Read__error;
using Call = salts_plugin_status (*)(
    Client *,
    const Request_t *,
    Response_t *,
    Error *,
    int *);

static_assert(std::is_standard_layout_v<Client>,
              "generated typed-error client must remain C-compatible");
static_assert(std::is_standard_layout_v<Error>,
              "generated typed-error envelope must remain C-compatible");
static_assert(
    std::is_same_v<
        decltype(&databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call),
        Call>,
    "typed-error client call must preserve bridge/native/error separation");

int main() {
  Client client = ERRORPLUGIN_STOREPLUGIN_PLUGIN_CLIENT_INIT;
  Error error = databind_11_ErrorPlugin_5_Store_4_Read__ERROR_INIT;
  return client.registry == nullptr &&
                 error.kind ==
                     databind_11_ErrorPlugin_5_Store_4_Read__ERROR_NONE
             ? 0
             : 1;
}
