#include "generated_owned_buffers.h"
#include "data_bind_native_binding.h"

#include <cstdio>
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
  if (!generic_constructor_is(&value.headers, "cstl.Vec", 1u)) {
    const cmeta_generic_desc *constructor =
        cmeta_container_type_constructor(&value.headers);
    std::fprintf(stderr,
                 "generated Vec generic contract failed: constructor=%s arity=%zu valid=%d\n",
                 constructor != nullptr && constructor->stable_id != nullptr
                     ? constructor->stable_id
                     : "<null>",
                 cmeta_container_type_arity(&value.headers),
                 cmeta_container_type_application_valid(&value.headers) ? 1 : 0);
    NativeHeaderPolicy_clear(&value);
    return 6;
  }
  if (!cmeta_type_equal(
          cmeta_container_type_argument(&value.headers, 0u),
          &NativeHeader_CMETA_TYPE)) {
    const cmeta_type_desc *argument =
        cmeta_container_type_argument(&value.headers, 0u);
    std::fprintf(stderr,
                 "generated Vec argument mismatch: actual=%s expected=%s\n",
                 argument != nullptr && argument->name != nullptr
                     ? argument->name
                     : "<null>",
                 NativeHeader_CMETA_TYPE.name);
    NativeHeaderPolicy_clear(&value);
    return 6;
  }

  NativeHeaderPolicy_clear(&value);
  if (value.headers.cmeta.descriptor != nullptr)
    return 7;

  NativeHeaderMap_init(&map_owner);
  if (map_owner.headers.cmeta.descriptor == nullptr) {
    std::fprintf(stderr, "generated Map contract failed: descriptor is null\n");
    NativeHeaderMap_clear(&map_owner);
    return 80;
  }
  if (map_owner.headers.raw.key_type == nullptr) {
    std::fprintf(stderr, "generated Map contract failed: key_type is null\n");
    NativeHeaderMap_clear(&map_owner);
    return 81;
  }
  if (map_owner.headers.raw.value_type == nullptr) {
    std::fprintf(stderr, "generated Map contract failed: value_type is null\n");
    NativeHeaderMap_clear(&map_owner);
    return 82;
  }
  if (!cmeta_type_equal(
          map_owner.headers.raw.key_type, SALTS_TSTR_CMETA_TYPE_REF)) {
    std::fprintf(stderr,
                 "generated Map contract failed: key type mismatch actual=%s expected=%s\n",
                 map_owner.headers.raw.key_type->name,
                 SALTS_TSTR_CMETA_TYPE_REF->name);
    NativeHeaderMap_clear(&map_owner);
    return 83;
  }
  if (!cmeta_type_equal(
          map_owner.headers.raw.value_type, &NativeHeader_CMETA_TYPE)) {
    std::fprintf(stderr,
                 "generated Map contract failed: value type mismatch actual=%s expected=%s\n",
                 map_owner.headers.raw.value_type->name,
                 NativeHeader_CMETA_TYPE.name);
    NativeHeaderMap_clear(&map_owner);
    return 84;
  }
  if (!generic_constructor_is(&map_owner.headers, "cstl.Map", 2u)) {
    const cmeta_generic_desc *constructor =
        cmeta_container_type_constructor(&map_owner.headers);
    std::fprintf(stderr,
                 "generated Map generic contract failed: constructor=%s arity=%zu valid=%d\n",
                 constructor != nullptr && constructor->stable_id != nullptr
                     ? constructor->stable_id
                     : "<null>",
                 cmeta_container_type_arity(&map_owner.headers),
                 cmeta_container_type_application_valid(&map_owner.headers) ? 1 : 0);
    NativeHeaderMap_clear(&map_owner);
    return 9;
  }
  if (!cmeta_type_equal(
          cmeta_container_type_argument(&map_owner.headers, 0u),
          SALTS_TSTR_CMETA_TYPE_REF)) {
    std::fprintf(stderr, "generated Map generic key argument mismatch\n");
    NativeHeaderMap_clear(&map_owner);
    return 9;
  }
  if (!cmeta_type_equal(
          cmeta_container_type_argument(&map_owner.headers, 1u),
          &NativeHeader_CMETA_TYPE)) {
    std::fprintf(stderr, "generated Map generic value argument mismatch\n");
    NativeHeaderMap_clear(&map_owner);
    return 9;
  }

  NativeHeaderMap_clear(&map_owner);
  return map_owner.headers.cmeta.descriptor == nullptr &&
                 map_owner.headers.raw.impl == nullptr
             ? 0
             : 10;
}
