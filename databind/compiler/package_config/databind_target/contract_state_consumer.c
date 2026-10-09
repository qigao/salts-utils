#include "contract_state_native.h"
#include "data_bind_native.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
  Packet_native_cmeta_binding view = {0};
  DataBindNativeStateBinding presence[2] = {
      {sizeof(DataBindNativeStateBinding), "unchanged", 777u, 6u},
      {sizeof(DataBindNativeStateBinding), "unchanged", 777u, 6u}};
  DataBindNativeStateBinding nulls[2] = {
      {sizeof(DataBindNativeStateBinding), "unchanged", 777u, 6u},
      {sizeof(DataBindNativeStateBinding), "unchanged", 777u, 6u}};
  Packet source, clone, moved;
  PresenceOnly_native_state_view only_present = {0};
  NullOnly_native_state_view only_null = {0};
  Packet_native_state_view owner = {0};
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativePlan *native_plan = NULL;
  unsigned char workspace[16384] = {0};

  if (Packet_native_cmeta_bind(&view) != 0 ||
      view.reflection.mode != CMETA_DATA_REFLECTION_VIEW ||
      !cmeta_data_desc_valid(&view.data) ||
      cmeta_data_value_traits_supported(&view.data))
    return 1;
  if (Packet_native_presence_count != 2u || Packet_native_null_count != 2u ||
      Packet_native_state_bind(presence, 1u, nulls, 2u) == 0 ||
      presence[0].byte_offset != 777u ||
      nulls[0].byte_offset != 777u ||
      Packet_native_state_bind(NULL, 2u, nulls, 2u) == 0 ||
      Packet_native_state_bind(presence, 2u, NULL, 2u) == 0 ||
      Packet_native_state_bind(presence, 2u, nulls, 1u) == 0)
    return 2;
  if (Packet_native_state_bind(presence, 2u, nulls, 2u) != 0 ||
      strcmp(presence[0].field_name, "count") != 0 ||
      strcmp(presence[1].field_name, "active") != 0 ||
      strcmp(nulls[0].field_name, "delta") != 0 ||
      strcmp(nulls[1].field_name, "active") != 0 ||
      presence[0].byte_offset != offsetof(Packet, has_count) ||
      presence[1].byte_offset != offsetof(Packet, has_active) ||
      nulls[0].byte_offset != offsetof(Packet, is_null_delta) ||
      nulls[1].byte_offset != offsetof(Packet, is_null_active) ||
      presence[0].bit || presence[1].bit ||
      nulls[0].bit || nulls[1].bit)
    return 3;

  /* Validate a *single* caller-owned view graph: its DataBind arrays, CMeta
   * field pointers, and root descriptor all stay in this stable storage.
   * A VIEW is expressly not a substitute for an executable VALUE graph. */
  if (Packet_native_state_view_bind(NULL) == 0 ||
      Packet_native_state_view_bind(&owner) != 0 ||
      owner.binding.data != &owner.metadata.data ||
      owner.binding.presence != owner.presence ||
      owner.binding.nulls != owner.nulls ||
      owner.binding.presence_count != 2u ||
      owner.binding.null_count != 2u ||
      owner.metadata.data.shape != &owner.metadata.reflection ||
      owner.metadata.reflection.structure.fields != owner.metadata.data_fields ||
      owner.metadata.reflection.structure.layout != &owner.metadata.layout ||
      owner.metadata.layout.fields != owner.metadata.layout_fields ||
      owner.metadata.reflection.mode != CMETA_DATA_REFLECTION_VIEW ||
      cmeta_data_value_traits_supported(owner.binding.data) ||
      strcmp(owner.binding.idl_type_name, "Packet") != 0 ||
      owner.binding.presence[0].byte_offset != offsetof(Packet, has_count) ||
      owner.binding.nulls[1].byte_offset != offsetof(Packet, is_null_active))
    return 7;

  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 128u;
  options.max_owned_bytes = sizeof(workspace);
  if (data_bind_native_plan_compile(
          &options, owner.binding.data, &native_plan,
          &diagnostic) == DATA_BIND_OK || native_plan != NULL)
    return 8;

  /* Absent state dimension must produce an exact zero count and NULL
   * public pointer, even though C11 reserves one private array slot. */
  if (PresenceOnly_native_state_view_bind(&only_present) != 0 ||
      NullOnly_native_state_view_bind(&only_null) != 0 ||
      only_present.binding.presence_count != 1u ||
      only_present.binding.null_count != 0u ||
      only_present.binding.presence != only_present.presence ||
      only_present.binding.nulls != NULL ||
      only_null.binding.presence_count != 0u ||
      only_null.binding.null_count != 1u ||
      only_null.binding.presence != NULL ||
      only_null.binding.nulls != only_null.nulls ||
      only_present.presence[0].byte_offset != offsetof(PresenceOnly, has_code) ||
      only_present.presence[0].bit != 0u ||
      only_null.nulls[0].byte_offset != offsetof(NullOnly, is_null_reading) ||
      only_null.nulls[0].bit != 0u ||
      only_present.metadata.reflection.mode != CMETA_DATA_REFLECTION_VIEW ||
      only_null.metadata.reflection.mode != CMETA_DATA_REFLECTION_VIEW ||
      cmeta_data_value_traits_supported(only_present.binding.data) ||
      cmeta_data_value_traits_supported(only_null.binding.data))
    return 9;

  Packet_init(&source);
  Packet_init(&clone);
  Packet_init(&moved);
  source.has_count = true;
  source.count = 29u;
  source.is_null_delta = true;
  source.has_active = true;
  source.is_null_active = true;
  if (((const unsigned char *)&source)[presence[0].byte_offset] != 1u ||
      ((const unsigned char *)&source)[presence[1].byte_offset] != 1u ||
      ((const unsigned char *)&source)[nulls[0].byte_offset] != 1u ||
      ((const unsigned char *)&source)[nulls[1].byte_offset] != 1u)
    return 4;
  if (Packet_clone(&clone, &source) != 0 ||
      !clone.has_count || clone.count != 29u ||
      !clone.is_null_delta || !clone.has_active || !clone.is_null_active ||
      Packet_move(&moved, &clone) != 0 ||
      !moved.has_count || moved.count != 29u ||
      clone.has_count || clone.count)
    return 5;
  Packet_clear(&source);
  Packet_clear(&clone);
  Packet_clear(&moved);
  return moved.has_count || moved.count || moved.is_null_delta ||
         moved.has_active || moved.is_null_active ? 6 : 0;
}
