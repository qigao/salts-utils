#include "native_assoc_fixture.h"
#include <stdint.h>
#include <stdio.h>
#define FAIL(code) do { fprintf(stderr, "native assoc stage %d failed\n", (code)); return (code); } while (0)
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
      set_add(&a.unique, &k2) != STL_OK) FAIL(1);
  if (AssocPacket_clone(&b, &a) != 0) FAIL(20);
  if (map_size(&b.lookup) != 2u) FAIL(21);
  if (set_size(&b.unique) != 2u) FAIL(22);
  if (b.lookup.impl == a.lookup.impl) FAIL(23);
  if (b.unique.map.impl == a.unique.map.impl) FAIL(24);
  if (!map_contains(&b.lookup, &k1)) FAIL(25);
  if (!set_contains(&b.unique, &k2)) FAIL(26);
  if (map_get_const(&b.lookup, &k2) == NULL ||
      *(const int32_t *)map_get_const(&b.lookup, &k2) != v2) FAIL(27);
  if (AssocPacket_move(&moved, &b) != 0 ||
      b.lookup.impl || b.unique.map.impl ||
      !set_contains(&moved.unique, &k2) ||
      map_size(&moved.lookup) != 2u) FAIL(3);
  if (AssocPacket_clone(&moved, &moved) || AssocPacket_move(&moved, &moved))
    FAIL(4);
  {
    AssocPacket incorrect = a;
    incorrect.lookup.key_type = &cmeta_type_int16;
    if (AssocPacket_clone(&b, &incorrect) == 0 ||
        b.lookup.impl || b.unique.map.impl) FAIL(5);
  }
  AssocPacket_clear(&a); AssocPacket_clear(&b); AssocPacket_clear(&moved);
  AssocPacket_clear(&moved);
  return a.lookup.impl || moved.unique.map.impl ? 6 : 0;
}
