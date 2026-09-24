#include <cmeta/cmeta.h>
#include <data_bind.h>
#include <salts_uuid.h>
#include <tbe_wire.h>

#include <stdint.h>

int main(void) {
  uint8_t storage[4] = {0};
  salts_uuid_t uuid = {{0}};

  tbe_wire_write_u32(storage, 0, 42u);
  return data_bind_abi_version() == DATA_BIND_ABI_VERSION &&
                 tbe_wire_read_u32(storage, 0) == 42u &&
                 uuid.bytes[0] == 0u &&
                 cmeta_type_desc_valid(&cmeta_type_int)
             ? 0
             : 1;
}
