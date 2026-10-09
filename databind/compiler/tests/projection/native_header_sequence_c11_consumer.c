#include "native_sequence_fixture.h"
#include <stdint.h>
int main(void) {
  SequencePacket a, b, dst;
  const uint32_t v1 = 7u, v2 = 19u;
  SequencePacket_init(&a);
  SequencePacket_init(&b);
  SequencePacket_init(&dst);
  if (vec_raw_init(&a.numbers, &cmeta_type_uint32, 16u) != STL_OK ||
      vec_push(&a.numbers, &v1) != STL_OK ||
      vec_push(&a.numbers, &v2) != STL_OK) return 1;
  if (SequencePacket_clone(&b, &a) != 0 ||
      vec_size(&b.numbers) != 2u ||
      b.numbers.data == a.numbers.data ||
      *(const uint32_t *)vec_at_const(&b.numbers, 0u) != v1 ||
      *(const uint32_t *)vec_at_const(&b.numbers, 1u) != v2) return 2;
  if (SequencePacket_move(&dst, &b) != 0 ||
      b.numbers.initialized || b.numbers.data ||
      vec_size(&dst.numbers) != 2u) return 3;
  if (SequencePacket_clone(&dst, &dst) != 0 ||
      SequencePacket_move(&dst, &dst) != 0) return 4;
  SequencePacket_clear(&a);
  SequencePacket_clear(&b);
  SequencePacket_clear(&dst);
  SequencePacket_clear(&dst);
  return dst.numbers.initialized || dst.numbers.data ? 5 : 0;
}
