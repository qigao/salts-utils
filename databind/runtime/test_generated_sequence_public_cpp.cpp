#include "generated_owned_buffers.h"
#include "binary_tail_only_generated.h"
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
static_assert(std::is_standard_layout_v<GroupWire_entries_vec_t> &&
              std::is_standard_layout_v<NullableGroupWire_t>,
              "generated GROUP storage and state must remain C-compatible");
static_assert(sizeof(GroupWire_entries_vec_t) > sizeof(vec_t),
              "GROUP must own canonical element semantics");
static_assert(std::is_standard_layout_v<FixedByteEntry_t> &&
              std::is_standard_layout_v<FixedByteGroupWire_entries_vec_t>,
              "fixed inline bytes preserve C layout inside managed GROUP storage");

static bool group_owner_contract() {
  static const char json[] =
      "{\"id\":513,\"entries\":[{\"code\":4660,\"amount\":1}],\"text\":\"cat\"}";
  static const uint8_t expected[] = {
      1u, 2u, 6u, 0u, 1u, 0u, 0x34u, 0x12u, 1u, 0u, 0u, 0u,
      3u, 0u, 0u, 0u, 'c', 'a', 't'};
  DataBind *codec = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  bool valid = false;
  if (Tail_codec_create(&codec, &error) != DATA_BIND_OK) return false;
  {
    Tail_typed::GroupWireOwner source, copy;
    uint8_t wire[sizeof(expected)]{};
    size_t len = 0u;
    valid = source.from_json(codec, json, sizeof(json) - 1u, &error) == DATA_BIND_OK &&
            cmeta_data_value_copy(&GroupWire_CMETA_DATA, copy.get(), source.get()) == CMETA_OK &&
            vec_at_const(&copy->entries.raw, 0u) != vec_at_const(&source->entries.raw, 0u) &&
            copy->text != source->text &&
            GroupWire_to_bin_into(codec, copy.get(), wire, sizeof(wire), &len, &error) == DATA_BIND_OK &&
            len == sizeof(expected) && std::memcmp(wire, expected, sizeof(expected)) == 0;
  }
  data_bind_free(codec);
  return valid;
}

static bool fixed_byte_owner_contract() {
  static const uint8_t expected[] = {
      8u, 0u, 1u, 0u, 'a', 'b', 'c', 'd', 'e', 'f', 0x34u, 0x12u,
      3u, 0u, 0u, 0u, 'c', 'a', 't'};
  DataBind *codec = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  bool valid = false;
  if (Tail_codec_create(&codec, &error) != DATA_BIND_OK) return false;
  {
    Tail_typed::FixedByteGroupWireOwner source, copy;
    uint8_t wire[sizeof(expected)]{};
    size_t len = 0u;
    if (source.from_bin(codec, expected, sizeof(expected), &error) == DATA_BIND_OK &&
        cmeta_data_value_copy(&FixedByteGroupWire_CMETA_DATA, copy.get(), source.get()) == CMETA_OK) {
      const auto *entry = static_cast<const FixedByteEntry_t *>(vec_at_const(&source->entries.raw, 0u));
      const auto *copied = static_cast<const FixedByteEntry_t *>(vec_at_const(&copy->entries.raw, 0u));
      valid = entry != nullptr && copied != nullptr && entry != copied &&
          std::memcmp(entry->digest, copied->digest, sizeof(entry->digest)) == 0 &&
          copy->text != source->text &&
          FixedByteGroupWire_to_bin_into(codec, copy.get(), wire, sizeof(wire), &len, &error) == DATA_BIND_OK &&
          len == sizeof(expected) && std::memcmp(wire, expected, sizeof(expected)) == 0;
    }
  }
  data_bind_free(codec);
  return valid;
}

int main() {
  enum { GROUP_CONTRACT_FAILURE = 11, FIXED_BYTES_CONTRACT_FAILURE = 12 };
  if (!group_owner_contract()) return GROUP_CONTRACT_FAILURE;
  if (!fixed_byte_owner_contract()) return FIXED_BYTES_CONTRACT_FAILURE;
  NativeHeaderPolicy_t value{};
  NativeHeaderMap_t map_owner{};
  const DataBindMessageNativeArtifact *artifact =
      NativeHeaderPolicy_native_artifact();
  const cmeta_data_desc *header_data = nullptr;
  const cmeta_data_desc *policy_data = nullptr;
  const cmeta_data_desc *map_data = nullptr;
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
  if (NativeHeaderPolicy_cmeta_data(&policy_data, &error) != DATA_BIND_OK ||
      policy_data == nullptr ||
      NativeHeaderMap_cmeta_data(&map_data, &error) != DATA_BIND_OK ||
      map_data == nullptr)
    return 9;

  {
    const auto *shape =
        static_cast<const cmeta_data_struct_shape *>(policy_data->shape);
    const cmeta_field_desc *field =
        shape != nullptr && shape->layout != nullptr
            ? cmeta_struct_find_field(shape->layout, "headers")
            : nullptr;
    const cmeta_declared_type *declared =
        field != nullptr ? field->declared_type : nullptr;
    if (declared == nullptr || !cmeta_declared_type_valid(declared) ||
        cmeta_declared_type_constructible(declared) ||
        declared->constructor == nullptr ||
        declared->constructor->stable_id == nullptr ||
        std::strcmp(declared->constructor->stable_id,
                    stl_vec_generic_desc.stable_id) != 0 ||
        declared->arity != 1u ||
        !cmeta_type_equal(cmeta_declared_type_argument(declared, 0u),
                          &NativeHeader_CMETA_TYPE))
      return 10;
  }

  {
    const auto *shape =
        static_cast<const cmeta_data_struct_shape *>(map_data->shape);
    const cmeta_field_desc *field =
        shape != nullptr && shape->layout != nullptr
            ? cmeta_struct_find_field(shape->layout, "headers")
            : nullptr;
    const cmeta_declared_type *declared =
        field != nullptr ? field->declared_type : nullptr;
    if (declared == nullptr || !cmeta_declared_type_valid(declared) ||
        cmeta_declared_type_constructible(declared) ||
        declared->constructor == nullptr ||
        declared->constructor->stable_id == nullptr ||
        std::strcmp(declared->constructor->stable_id,
                    stl_map_generic_desc.stable_id) != 0 ||
        declared->arity != 2u ||
        !cmeta_type_equal(cmeta_declared_type_argument(declared, 0u),
                          SALTS_TSTR_CMETA_TYPE_REF) ||
        !cmeta_type_equal(cmeta_declared_type_argument(declared, 1u),
                          &NativeHeader_CMETA_TYPE))
      return 11;
  }

  NativeHeaderPolicy_init(&value);
  if (value.headers.cmeta.descriptor == nullptr ||
      value.headers.raw.element_type == nullptr ||
      !cmeta_type_equal(value.headers.raw.element_type,
                        &NativeHeader_CMETA_TYPE)) {
    NativeHeaderPolicy_clear(&value);
    return 5;
  }

  NativeHeaderPolicy_clear(&value);
  if (value.headers.cmeta.descriptor != nullptr)
    return 6;

  NativeHeaderMap_init(&map_owner);
  if (map_owner.headers.cmeta.descriptor == nullptr ||
      map_owner.headers.raw.key_type == nullptr ||
      map_owner.headers.raw.value_type == nullptr ||
      !cmeta_type_equal(
          map_owner.headers.raw.key_type, SALTS_TSTR_CMETA_TYPE_REF) ||
      !cmeta_type_equal(
          map_owner.headers.raw.value_type, &NativeHeader_CMETA_TYPE)) {
    NativeHeaderMap_clear(&map_owner);
    return 7;
  }

  NativeHeaderMap_clear(&map_owner);
  return map_owner.headers.cmeta.descriptor == nullptr &&
                 map_owner.headers.raw.impl == nullptr
             ? 0
             : 8;
}
