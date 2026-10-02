#include "installed_message_native.h"
#include <data_bind_native_binding.h>
#include <type_traits>

static_assert(std::is_standard_layout<DataBindMessageNativeArtifact>::value,
              "MESSAGE artifact must remain C-compatible");

int main() {
  const DataBindMessageNativeArtifact *config = Config_native_artifact();
  const DataBindMessageNativeArtifact *headers = HeaderPolicy_native_artifact();
  return data_bind_message_native_artifact_valid(config) &&
                 data_bind_message_native_artifact_valid(headers)
             ? 0
             : 1;
}
