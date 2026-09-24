#include "image.plugin_client.h"

#include <type_traits>

using Client = databind_plugin_client_5_Image_14_ImageProcessor;

static_assert(std::is_standard_layout_v<Client>,
              "generated Plugin client must remain C-compatible");

int main() {
  Client client = IMAGE_IMAGEPROCESSOR_PLUGIN_CLIENT_INIT;
  return client.registry == nullptr ? 0 : 1;
}
