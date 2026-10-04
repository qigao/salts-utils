#include "generated_owned_buffers.h"
#include "data_bind_native_binding.h"

#include <cstring>
#include <type_traits>

static_assert(std::is_standard_layout_v<NativeHeader_t>,
              "generated sequence element must remain C-compatible");
static_assert(std::is_standard_layout_v<NativeHeaderPolicy_t>,
              "generated sequence owner must remain C-compatible");
static_assert(std::is_standard_layout_v<NativeHeaderPolicy_headers_vec_t>,
              "generated record Vec declaration must remain C-compatible");
static_assert(sizeof(NativeHeaderPolicy_headers_vec_t) > sizeof(vec_t),
              "record list storage must not preserve raw vec_t ABI");
static_assert(std::is_standard_layout_v<NativeHeaderMap_t>,
              "generated record Map owner must remain C-compatible");
static_assert(std::is_standard_layout_v<NativeHeaderMap_headers_map_t>,
              "generated record Map declaration must remain C-compatible");
static_assert(sizeof(NativeHeaderMap_headers_map_t) > sizeof(map_t),
              "record Map storage must not preserve raw map_t ABI");

static bool generic_constructor_is(
    const void *container, const char *stable_id, size_t arity) {
  const cmeta_generic_desc *constructor =
      cmeta_container_type_constructor(container);
  return constructor != nullptr &&
         constructor->stable_id != nullptr &&
         std::strcmp(constructor->stable_id, stable_id) == 0 &&
         cmeta_container_type_arity(container) == arity &&
         cmeta_container_type_application_valid(container);
}

int main() {
  NativeHeaderPolicy_t value{};
  NativeHeaderMap_t map_owner{};
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
  if (!generic_constructor_is(&value.headers, "cstl.Vec", 1u) ||
      !cmeta_type_equal(
          cmeta_container_type_argument(&value.headers, 0u),
          &NativeHeader_CMETA_TYPE)) {
    NativeHeaderPolicy_clear(&value);
    return 6;
  }

  NativeHeaderPolicy_clear(&value);
  if (value.headers.cmeta.descriptor != nullptr)
    return 7;

  NativeHeaderMap_init(&map_owner);
  if (map_owner.headers.cmeta.descriptor == nullptr ||
      map_owner.headers.raw.key_type == nullptr ||
      map_owner.headers.raw.value_type == nullptr ||
      !cmeta_type_equal(
          map_owner.headers.raw.key_type, SALTS_TSTR_CMETA_TYPE_REF) ||
      !cmeta_type_equal(
          map_owner.headers.raw.value_type, &NativeHeader_CMETA_TYPE)) {
    NativeHeaderMap_clear(&map_owner);
    return 8;
  }
  if (!generic_constructor_is(&map_owner.headers, "cstl.Map", 2u) ||
      !cmeta_type_equal(
          cmeta_container_type_argument(&map_owner.headers, 0u),
          SALTS_TSTR_CMETA_TYPE_REF) ||
      !cmeta_type_equal(
          cmeta_container_type_argument(&map_owner.headers, 1u),
          &NativeHeader_CMETA_TYPE)) {
    NativeHeaderMap_clear(&map_owner);
    return 9;
  }

  NativeHeaderMap_clear(&map_owner);
  return map_owner.headers.cmeta.descriptor == nullptr &&
                 map_owner.headers.raw.impl == nullptr
             ? 0
             : 10;
}
