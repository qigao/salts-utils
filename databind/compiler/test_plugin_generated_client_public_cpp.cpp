#include "image.plugin.h"

#include <type_traits>

using Client = databind_5_Image_14_ImageProcessor_plugin_client;

static_assert(
    std::is_standard_layout_v<Client>,
    "generated Plugin client must remain a C-compatible standard-layout type");

int main() {
  Client client = databind_5_Image_14_ImageProcessor_PLUGIN_CLIENT_INIT;
  return client.registry == nullptr &&
                 !salts_plugin_lease_valid(client.lease) &&
                 client.exports[0] == nullptr
             ? 0
             : 1;
}
