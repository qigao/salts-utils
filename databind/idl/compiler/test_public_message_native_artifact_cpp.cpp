#include "message_native_artifact_native.h"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindMessageNativeArtifact>::value,
              "Message native artifact must remain C-compatible");
static_assert(std::is_standard_layout<DataBindNativeTypeBinding>::value,
              "native binding must remain C-compatible");

extern "C" int databind_message_native_artifact_cpp_probe(void) {
  const DataBindMessageNativeArtifact *artifact = Event_native_artifact();
  return data_bind_message_native_artifact_valid(artifact) ? 0 : 1;
}
