#include "native_contract_fixture.h"
#include <stddef.h>
#ifdef DATABIND_NATIVE_ENABLE_CMETA
#include <string.h>
#endif

int main(void) {
#ifdef DATABIND_NATIVE_ENABLE_CMETA
  cmeta_data_field_desc fields[3];
  if (Packet_native_cmeta_data_fields(fields, 2u) == 0 ||
      Packet_native_cmeta_data_fields(fields, 3u) != 0 ||
      strcmp(fields[0].stable_id,
             "tbe.native.NativeFixture.v1.Packet.count") != 0 ||
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
      strcmp(view.data.stable_id,
             "tbe.native.NativeFixture.v1.Packet.data") != 0 ||
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
  /* State flags are native storage companions, not logical CMeta values. */
  if (strcmp(Packet_native_cmeta_states[0].name, view.data_fields[0].name) != 0 ||
      Packet_native_cmeta_states[0].presence_offset != offsetof(Packet, has_count) ||
      Packet_native_cmeta_states[0].null_offset != SIZE_MAX ||
      strcmp(Packet_native_cmeta_states[1].name, view.data_fields[1].name) != 0 ||
      Packet_native_cmeta_states[1].presence_offset != SIZE_MAX ||
      Packet_native_cmeta_states[1].null_offset != offsetof(Packet, is_null_delta) ||
      strcmp(Packet_native_cmeta_states[2].name, view.data_fields[2].name) != 0 ||
      Packet_native_cmeta_states[2].presence_offset != offsetof(Packet, has_active) ||
      Packet_native_cmeta_states[2].null_offset != offsetof(Packet, is_null_active))
    return 6;
#endif
#if defined(DATABIND_NATIVE_ENABLE_CMETA) && defined(DATABIND_NATIVE_ENABLE_DATABIND)
  DataBindNativeStateBinding presence[2] = {
      {sizeof(DataBindNativeStateBinding), "sentinel", 777u, 5u},
      {sizeof(DataBindNativeStateBinding), "sentinel", 777u, 5u}};
  DataBindNativeStateBinding nulls[2] = {
      {sizeof(DataBindNativeStateBinding), "sentinel", 777u, 5u},
      {sizeof(DataBindNativeStateBinding), "sentinel", 777u, 5u}};
  if (Packet_native_presence_count != 2u ||
      Packet_native_null_count != 2u ||
      Packet_native_state_bind(presence, 1u, nulls, 2u) == 0 ||
      presence[0].byte_offset != 777u ||
      nulls[0].byte_offset != 777u ||
      Packet_native_state_bind(NULL, 2u, nulls, 2u) == 0 ||
      Packet_native_state_bind(presence, 2u, NULL, 2u) == 0 ||
      Packet_native_state_bind(presence, 2u, nulls, 1u) == 0 ||
      Packet_native_state_bind(presence, 2u, nulls, 2u) != 0 ||
      strcmp(presence[0].field_name, "count") != 0 ||
      strcmp(presence[1].field_name, "active") != 0 ||
      strcmp(nulls[0].field_name, "delta") != 0 ||
      strcmp(nulls[1].field_name, "active") != 0 ||
      presence[0].byte_offset != offsetof(Packet, has_count) ||
      presence[1].byte_offset != offsetof(Packet, has_active) ||
      nulls[0].byte_offset != offsetof(Packet, is_null_delta) ||
      nulls[1].byte_offset != offsetof(Packet, is_null_active) ||
      presence[0].bit || presence[1].bit || nulls[0].bit || nulls[1].bit ||
      presence[0].size != sizeof(DataBindNativeStateBinding))
    return 9;
#endif
  Packet packet;
#ifdef DATABIND_NATIVE_ENABLE_CMETA
  if (Packet_native_cmeta_type.name == NULL ||
      !cmeta_type_desc_valid(&Packet_native_cmeta_type) ||
      Packet_native_cmeta_type.identity != &Packet_native_cmeta_identity ||
      strcmp(Packet_native_cmeta_identity.stable_atom_id,
             "tbe.native.NativeFixture.v1.Packet") != 0 ||
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
#if defined(DATABIND_NATIVE_ENABLE_CMETA) && defined(DATABIND_NATIVE_ENABLE_DATABIND)
  packet.has_count = true;
  packet.has_active = true;
  packet.is_null_delta = true;
  packet.is_null_active = true;
  if (((const unsigned char *)&packet)[presence[0].byte_offset] != 1u ||
      ((const unsigned char *)&packet)[presence[1].byte_offset] != 1u ||
      ((const unsigned char *)&packet)[nulls[0].byte_offset] != 1u ||
      ((const unsigned char *)&packet)[nulls[1].byte_offset] != 1u)
    return 10;
  Packet_clear(&packet);
#endif
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
  Packet copy, moved;
  Packet_init(&copy);
  Packet_init(&moved);
  if (Packet_clone(NULL, &packet) == 0 ||
      Packet_clone(&copy, NULL) == 0 ||
      Packet_clone(&copy, &packet) != 0 ||
      !copy.has_count || copy.count != 42u ||
      Packet_move(NULL, &copy) == 0 ||
      Packet_move(&moved, NULL) == 0 ||
      Packet_move(&moved, &copy) != 0 ||
      !moved.has_count || moved.count != 42u ||
      copy.has_count || copy.count != 0u)
    return 7;
  if (Packet_clone(&moved, &moved) != 0 ||
      Packet_move(&moved, &moved) != 0 ||
      moved.count != 42u)
    return 8;
  Packet_clear(&packet);
  Packet_clear(&copy);
  Packet_clear(&moved);
  Packet_clear(&moved);
  return packet.has_count || packet.count != 0u ||
         moved.has_count || moved.count != 0u;
}
