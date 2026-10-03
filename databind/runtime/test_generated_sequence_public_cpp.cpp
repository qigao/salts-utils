#include "generated_owned_buffers.h"
#include "data_bind_native_binding.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<NativeHeader_t>,
              "generated sequence element must remain C-compatible");
static_assert(std::is_standard_layout_v<NativeHeaderPolicy_t>,
              "generated sequence owner must remain C-compatible");
static_assert(std::is_standard_layout_v<NativeHeaderPolicy_headers_vec_t>,
              "generated record Vec declaration must remain C-compatible");
static_assert(sizeof(NativeHeaderPolicy_headers_vec_t) > sizeof(vec_t),
              "record list storage must not preserve raw vec_t ABI");

int main() {
  NativeHeaderPolicy_t value{};
  const DataBindMessageNativeArtifact *artifact =
      NativeHeaderPolicy_native_artifact();
  const cmeta_data_desc *header_data = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;

  if (!data_bind_message_native_artifact_valid(artifact))
    return 1;
  if (artifact->type_name == nullptr)
    return 2;
  if (NativeHeader_cmeta_data(&header_data, &error) != DATA_BIND_OK ||
      header_data == nullptr)
    return 3;
  if (header_data != &NativeHeader_CMETA_DATA ||
      header_data->storage_type != &NativeHeader_CMETA_TYPE)
    return 4;

  NativeHeaderPolicy_init(&value);
  if (value.headers.cmeta.descriptor == nullptr ||
      value.headers.raw.element_type == nullptr ||
      !cmeta_type_equal(value.headers.raw.element_type,
                        &NativeHeader_CMETA_TYPE)) {
    NativeHeaderPolicy_clear(&value);
    return 5;
  }

  NativeHeaderPolicy_clear(&value);
  return value.headers.cmeta.descriptor == nullptr ? 0 : 6;
}
