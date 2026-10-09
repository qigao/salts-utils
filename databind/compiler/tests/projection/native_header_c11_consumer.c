#include "native_contract_fixture.h"
#ifdef DATABIND_NATIVE_ENABLE_CMETA
#include <string.h>
#include <stddef.h>
#endif

int main(void) {
#ifdef DATABIND_NATIVE_ENABLE_CMETA
  cmeta_data_field_desc fields[3];
  if (Packet_native_cmeta_data_fields(fields, 2u) == 0 ||
      Packet_native_cmeta_data_fields(fields, 3u) != 0 ||
      fields[0].value != &cmeta_data_uint32 ||
      fields[1].value != &cmeta_data_int16 ||
      fields[2].value != &cmeta_data_bool ||
      fields[0].offset != offsetof(Packet, count))
    return 3;
#endif
#ifdef DATABIND_NATIVE_ENABLE_CMETA
  Packet_native_cmeta_binding view = {0};
  if (Packet_native_cmeta_bind(NULL) == 0 ||
      Packet_native_cmeta_bind(&view) != 0 ||
      !cmeta_data_desc_valid(&view.data) ||
      view.data.abi_version != CMETA_DATA_DESC_REFLECTION_ABI_VERSION ||
      view.data.kind != CMETA_DATA_STRUCT ||
      view.data.storage_type != &Packet_native_cmeta_type ||
      view.data.shape != &view.reflection ||
      view.reflection.mode != CMETA_DATA_REFLECTION_VIEW ||
      view.reflection.structure.layout != &view.layout ||
      view.reflection.structure.fields != view.data_fields ||
      view.reflection.structure.field_count != 3u ||
      view.layout.fields != view.layout_fields ||
      view.layout.field_count != 3u ||
      view.layout_fields[0].type != fields[0].value->storage_type ||
      view.layout_fields[1].type != fields[1].value->storage_type ||
      view.layout_fields[2].type != fields[2].value->storage_type)
    return 4;
#endif
  Packet packet;
#ifdef DATABIND_NATIVE_ENABLE_CMETA
  if (Packet_native_cmeta_type.name == NULL ||
      strcmp(Packet_native_cmeta_type.name, "Packet") != 0 ||
      Packet_native_cmeta_type.size != sizeof(Packet) ||
      Packet_native_cmeta_type.align != _Alignof(Packet) ||
      Packet_native_cmeta_type.kind != CMETA_T_OBJECT ||
      Packet_native_cmeta_layout.field_count != 3u ||
      Packet_native_cmeta_layout.fields != Packet_native_cmeta_fields ||
      Packet_native_cmeta_layout.size != sizeof(Packet) ||
      strcmp(Packet_native_cmeta_fields[0].name, "count") != 0 ||
      Packet_native_cmeta_fields[0].offset != offsetof(Packet, count) ||
      strcmp(Packet_native_cmeta_fields[1].name, "delta") != 0 ||
      Packet_native_cmeta_fields[1].offset != offsetof(Packet, delta) ||
      strcmp(Packet_native_cmeta_fields[2].name, "active") != 0 ||
      Packet_native_cmeta_fields[2].offset != offsetof(Packet, active) ||
      Packet_native_cmeta_fields[0].size != sizeof(packet.count) ||
      Packet_native_cmeta_fields[1].size != sizeof(packet.delta) ||
      Packet_native_cmeta_fields[2].size != sizeof(packet.active))
    return 2;
#endif
  Packet_init(&packet);
#ifdef DATABIND_NATIVE_ENABLE_CMETA
  /* Logical-field reflection is a view: it must not claim that its scalar
   * subset can initialize or clear the separate presence/null flags. */
  if (cmeta_data_value_init_zero(&view.data, &packet) != CMETA_TRAIT_MISSING)
    return 5;
#endif
  if (packet.has_count || packet.count != 0u ||
      packet.is_null_delta || packet.delta != 0 ||
      packet.has_active || packet.is_null_active || packet.active)
    return 1;
  packet.has_count = true;
  packet.count = 42u;
  Packet_clear(&packet);
  return packet.has_count || packet.count != 0u;
}
