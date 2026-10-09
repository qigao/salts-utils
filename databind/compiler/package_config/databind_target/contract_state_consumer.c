#include "contract_state_native.h"

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
