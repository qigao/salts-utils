#include "native_contract_fixture.h"

int main(void) {
  Packet packet;
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
