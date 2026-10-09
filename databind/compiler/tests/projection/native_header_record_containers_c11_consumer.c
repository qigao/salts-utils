#include "native_record_containers_fixture.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int make_item(RecordItem *item) {
  static const unsigned char payload[] = {9u, 8u, 7u};
  RecordItem_init(item);
  item->label.size = 5u;
  item->label.data = (char *)malloc(6u);
  item->payload.size = sizeof(payload);
  item->payload.data = (unsigned char *)malloc(sizeof(payload));
  if (!item->label.data || !item->payload.data) {
    RecordItem_clear(item);
    return -1;
  }
  memcpy(item->label.data, "hello", 6u);
  memcpy(item->payload.data, payload, sizeof(payload));
  return 0;
}

int main(void) {
  RecordItem item;
  RecordHolder a, b, moved;
  const databind_native_text key = {(char *)"key", 3u};
  const RecordItem *src_item, *dst_item, *mapped;
  RecordHolder_init(&a);
  RecordHolder_init(&b);
  RecordHolder_init(&moved);
  if (cmeta_type_require_traits(&RecordItem_native_element_cmeta_type,
          CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) != CMETA_OK)
    return 1;
  if (cmeta_type_require_traits(&RecordItem_native_element_cmeta_type,
          CMETA_TRAIT_COMPARE) == CMETA_OK)
    return 2;
  if (make_item(&item) != 0) return 3;
  a.byname.key_type = &databind_native_text_cmeta_type;
  a.byname.value_type = &RecordItem_native_element_cmeta_type;
  if (vec_raw_init(&a.items, &RecordItem_native_element_cmeta_type, 16u) != STL_OK ||
      map_init(&a.byname, 16u) != STL_OK ||
      vec_push(&a.items, &item) != STL_OK ||
      map_put(&a.byname, &key, &item) != STL_OK)
    return 4;
  RecordItem_clear(&item);
  if (RecordHolder_clone(&b, &a) != 0 ||
      vec_size(&b.items) != 1u || map_size(&b.byname) != 1u ||
      a.items.data == b.items.data || a.byname.impl == b.byname.impl)
    return 5;

  src_item = (const RecordItem *)vec_at_const(&a.items, 0u);
  dst_item = (const RecordItem *)vec_at_const(&b.items, 0u);
  mapped = (const RecordItem *)map_get_const(&b.byname, &key);
  if (!src_item || !dst_item || !mapped ||
      !dst_item->label.data || !mapped->label.data ||
      strcmp(dst_item->label.data, "hello") != 0 ||
      strcmp(mapped->label.data, "hello") != 0 ||
      dst_item->label.data == src_item->label.data ||
      dst_item->payload.data == src_item->payload.data ||
      mapped->label.data == dst_item->label.data)
    return 6;

  ((RecordItem *)vec_at(&a.items, 0u))->label.data[0] = 'H';
  if (strcmp(dst_item->label.data, "hello") != 0)
    return 7;

  /* Clone copies the vector before the Map; a late Map admission error
   * must discard staged nested allocations without changing destination. */
  {
    RecordHolder invalid = a;
    invalid.byname.value_type = &cmeta_type_uint32;
    if (RecordHolder_clone(&b, &invalid) == 0 ||
        vec_size(&b.items) != 1u || map_size(&b.byname) != 1u ||
        strcmp(((const RecordItem *)vec_at_const(&b.items, 0u))->label.data,
               "hello") != 0)
      return 8;
  }

  if (RecordHolder_move(&moved, &b) != 0 ||
      b.items.initialized || b.byname.impl ||
      vec_size(&moved.items) != 1u || map_size(&moved.byname) != 1u)
    return 9;
  if (RecordHolder_clone(&moved, &moved) != 0 ||
      RecordHolder_move(&moved, &moved) != 0)
    return 10;
  RecordHolder_clear(&a);
  RecordHolder_clear(&b);
  RecordHolder_clear(&moved);
  RecordHolder_clear(&moved);
  return a.items.initialized || moved.byname.impl ? 11 : 0;
}
