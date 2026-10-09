#include "native_owned_containers_fixture.h"

#include <stdint.h>
#include <string.h>

static int payload_matches(const databind_native_bytes *bytes,
                           const unsigned char *expected, size_t count) {
  return bytes != NULL && bytes->size == count &&
         (count == 0u || (bytes->data && memcmp(bytes->data, expected, count) == 0));
}

int main(void) {
  OwnedContainers a, b, moved;
  char name_data[] = "alpha";
  unsigned char bytes_data[] = {4u, 5u, 6u};
  const unsigned char expected[] = {4u, 5u, 6u};
  const databind_native_text key = {name_data, 5u};
  const databind_native_text query = {(char *)"alpha", 5u};
  const databind_native_bytes blob = {bytes_data, 3u};
  const databind_native_text *label;
  const databind_native_bytes *payload;
  const databind_native_bytes *entry;
  OwnedContainers_init(&a);
  OwnedContainers_init(&b);
  OwnedContainers_init(&moved);

  if (cmeta_type_require_traits(&databind_native_text_cmeta_type,
          CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
          CMETA_TRAIT_COMPARE) != CMETA_OK ||
      cmeta_type_require_traits(&databind_native_bytes_cmeta_type,
          CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) != CMETA_OK)
    return 1;

  if (vec_raw_init(&a.labels, &databind_native_text_cmeta_type, 16u) != STL_OK ||
      vec_raw_init(&a.buffers, &databind_native_bytes_cmeta_type, 16u) != STL_OK)
    return 2;
  a.attrs.key_type = &databind_native_text_cmeta_type;
  a.attrs.value_type = &databind_native_bytes_cmeta_type;
  a.tags.element_type = &databind_native_text_cmeta_type;
  if (map_init(&a.attrs, 16u) != STL_OK ||
      set_init(&a.tags, 16u) != STL_OK ||
      vec_push(&a.labels, &key) != STL_OK ||
      vec_push(&a.buffers, &blob) != STL_OK ||
      map_put(&a.attrs, &key, &blob) != STL_OK ||
      set_add(&a.tags, &key) != STL_OK)
    return 3;

  if (OwnedContainers_clone(&b, &a) != 0 ||
      vec_size(&b.labels) != 1u || vec_size(&b.buffers) != 1u ||
      map_size(&b.attrs) != 1u || set_size(&b.tags) != 1u ||
      b.labels.data == a.labels.data || b.buffers.data == a.buffers.data ||
      b.attrs.impl == a.attrs.impl || b.tags.map.impl == a.tags.map.impl)
    return 4;

  label = (const databind_native_text *)vec_at_const(&b.labels, 0u);
  payload = (const databind_native_bytes *)vec_at_const(&b.buffers, 0u);
  entry = (const databind_native_bytes *)map_get_const(&b.attrs, &query);
  if (!label || !label->data || label->size != 5u ||
      memcmp(label->data, "alpha", 5u) != 0 ||
      !payload_matches(payload, expected, sizeof(expected)) ||
      !payload_matches(entry, expected, sizeof(expected)) ||
      label->data == ((const databind_native_text *)vec_at_const(&a.labels, 0u))->data ||
      payload->data == ((const databind_native_bytes *)vec_at_const(&a.buffers, 0u))->data ||
      !set_contains(&b.tags, &query))
    return 5;

  name_data[0] = 'Z';
  bytes_data[0] = 99u;
  if (!set_contains(&b.tags, &query) ||
      !payload_matches((const databind_native_bytes *)map_get_const(&b.attrs, &query),
                       expected, sizeof(expected)))
    return 6;

  /* Invalid source descriptor must fail before replacing an existing owner. */
  {
    OwnedContainers bad = a;
    bad.attrs.value_type = &cmeta_type_uint32;
    if (OwnedContainers_clone(&b, &bad) == 0 ||
        vec_size(&b.labels) != 1u ||
        !set_contains(&b.tags, &query))
      return 7;
  }
  if (OwnedContainers_move(&moved, &b) != 0 ||
      b.labels.initialized || b.buffers.initialized ||
      b.attrs.impl || b.tags.map.impl ||
      vec_size(&moved.labels) != 1u ||
      !set_contains(&moved.tags, &query))
    return 8;
  if (OwnedContainers_clone(&moved, &moved) != 0 ||
      OwnedContainers_move(&moved, &moved) != 0)
    return 9;
  OwnedContainers_clear(&a);
  OwnedContainers_clear(&b);
  OwnedContainers_clear(&moved);
  OwnedContainers_clear(&moved);
  return moved.attrs.impl || moved.labels.initialized ||
         moved.tags.map.impl || a.buffers.initialized ? 10 : 0;
}
