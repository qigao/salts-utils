#include "native_contract_fixture.h"
#ifdef DATABIND_NATIVE_ENABLE_CMETA
#include <string.h>
#include <stddef.h>
#endif

int main(void) {
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
  if (packet.has_count || packet.count != 0u ||
      packet.is_null_delta || packet.delta != 0 ||
      packet.has_active || packet.is_null_active || packet.active)
    return 1;
  packet.has_count = true;
  packet.count = 42u;
  Packet_clear(&packet);
  return packet.has_count || packet.count != 0u;
}
