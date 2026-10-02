#include "message_native_artifact_native.h"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindMessageNativeArtifact>::value,
              "Message native artifact must remain C-compatible");
static_assert(std::is_standard_layout<DataBindNativeTypeBinding>::value,
              "native binding must remain C-compatible");
static_assert(std::is_standard_layout<Event_values_vec_t>::value,
              "generated scalar Vec must remain C-compatible");
static_assert(std::is_standard_layout<Event_labels_vec_t>::value,
              "generated string Vec must remain C-compatible");
static_assert(sizeof(Event_values_vec_t) > sizeof(vec_t),
              "typed CSTL Vec must not preserve raw vec_t storage ABI");
static_assert(sizeof(Event_labels_vec_t) > sizeof(vec_t),
              "typed CSTL string Vec must not preserve raw vec_t storage ABI");

extern "C" int databind_message_native_artifact_cpp_probe(void) {
  const DataBindMessageNativeArtifact *artifact = Event_native_artifact();
  return data_bind_message_native_artifact_valid(artifact) ? 0 : 1;
}
