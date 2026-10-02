#include "generated_owned_buffers.h"
#include "data_bind_native_binding.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<NativeHeader_t>,
              "generated sequence element must remain C-compatible");
static_assert(std::is_standard_layout_v<NativeHeaderPolicy_t>,
              "generated sequence owner must remain C-compatible");

int main() {
  NativeHeaderPolicy_t value{};
  const DataBindMessageNativeArtifact *artifact =
      NativeHeaderPolicy_native_artifact();

  if (!data_bind_message_native_artifact_valid(artifact))
    return 1;
  if (artifact->type_name == nullptr)
    return 2;
  if (NativeHeaderPolicy_headers_vec_t_size(&value.headers) != 0u)
    return 3;
  if (NativeHeaderPolicy_headers_vec_t_init(&value.headers, 4u) != STL_OK)
    return 4;
  if (value.headers.raw.element_type == nullptr)
    return 5;
  NativeHeaderPolicy_headers_vec_t_destroy(&value.headers);
  if (NativeHeaderPolicy_headers_vec_t_size(&value.headers) != 0u)
    return 6;
  return 0;
}
