#include "native_assoc_fixture.h"
#include <stdint.h>
int main(void) {
  AssocPacket a, b, moved;
  const uint32_t k1 = 7u, k2 = 11u;
  const int32_t v1 = 19, v2 = 23;
  AssocPacket_init(&a); AssocPacket_init(&b); AssocPacket_init(&moved);
  if (map_raw_init(&a.lookup, &cmeta_type_uint32, &cmeta_type_int32, 32u) != STL_OK ||
      set_raw_init(&a.unique, &cmeta_type_uint32, 32u) != STL_OK ||
      map_put(&a.lookup, &k1, &v1) != STL_OK ||
      map_put(&a.lookup, &k2, &v2) != STL_OK ||
      set_add(&a.unique, &k1) != STL_OK ||
      set_add(&a.unique, &k2) != STL_OK) return 1;
  if (AssocPacket_clone(&b, &a) != 0 ||
      map_size(&b.lookup) != 2u || set_size(&b.unique) != 2u ||
      b.lookup.impl == a.lookup.impl || b.unique.map.impl == a.unique.map.impl ||
      !map_contains(&b.lookup, &k1) || !set_contains(&b.unique, &k2) ||
      *(const int32_t *)map_get_const(&b.lookup, &k2) != v2) return 2;
  if (AssocPacket_move(&moved, &b) != 0 ||
      b.lookup.impl || b.unique.map.impl ||
      !set_contains(&moved.unique, &k2) ||
      map_size(&moved.lookup) != 2u) return 3;
  if (AssocPacket_clone(&moved, &moved) || AssocPacket_move(&moved, &moved))
    return 4;
  {
    AssocPacket incorrect = a;
    incorrect.lookup.key_type = &cmeta_type_int16;
    if (AssocPacket_clone(&b, &incorrect) == 0 ||
        b.lookup.impl || b.unique.map.impl) return 5;
  }
  AssocPacket_clear(&a); AssocPacket_clear(&b); AssocPacket_clear(&moved);
  AssocPacket_clear(&moved);
  return a.lookup.impl || moved.unique.map.impl ? 6 : 0;
}
