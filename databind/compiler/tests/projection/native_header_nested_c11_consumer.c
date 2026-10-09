#include "native_nested_fixture.h"
#include <stdlib.h>
#include <string.h>

static int assign_text(databind_native_text *out, const char *s) {
  const size_t n = strlen(s);
  out->data = (char *)malloc(n + 1u);
  if (!out->data) return -1;
  memcpy(out->data, s, n + 1u);
  out->size = n;
  return 0;
}
static int assign_bytes(databind_native_bytes *out, const char *s, size_t n) {
  out->data = (unsigned char *)malloc(n);
  if (!out->data) return -1;
  memcpy(out->data, s, n);
  out->size = n;
  return 0;
}

int main(void) {
  NativeRoot a, b, dest;
  NativeRoot_init(&a);
  NativeRoot_init(&b);
  NativeRoot_init(&dest);
  if (assign_text(&a.parent.leaf.text, "inside") ||
      assign_bytes(&a.parent.leaf.data, "abc", 3u) ||
      assign_text(&a.parent.label, "outside") ||
      assign_bytes(&a.tail, "xy", 2u)) return 1;
  a.parent.has_label = true;
  if (NativeRoot_clone(&b, &a) != 0 ||
      b.parent.leaf.text.data == a.parent.leaf.text.data ||
      b.parent.leaf.data.data == a.parent.leaf.data.data ||
      b.parent.label.data == a.parent.label.data ||
      b.tail.data == a.tail.data ||
      strcmp(b.parent.leaf.text.data, "inside") ||
      strcmp(b.parent.label.data, "outside") ||
      memcmp(b.tail.data, "xy", 2u) ||
      !b.parent.has_label) return 2;
  a.parent.leaf.text.data[0] = 'I';
  if (b.parent.leaf.text.data[0] != 'i') return 3;
  if (NativeRoot_move(&dest, &b) ||
      b.parent.leaf.text.data || b.parent.leaf.data.data ||
      b.parent.label.data || b.tail.data ||
      strcmp(dest.parent.leaf.text.data, "inside")) return 4;
  /* A late invalid source after nested allocations must roll back clone. */
  {
    NativeRoot invalid = a;
    invalid.tail.data = NULL;
    if (NativeRoot_clone(&dest, &invalid) == 0 ||
        strcmp(dest.parent.leaf.text.data, "inside") ||
        memcmp(dest.tail.data, "xy", 2u)) return 5;
  }
  if (NativeRoot_clone(&dest, &dest) || NativeRoot_move(&dest, &dest))
    return 6;
  NativeRoot_clear(&a);
  NativeRoot_clear(&b);
  NativeRoot_clear(&dest);
  NativeRoot_clear(&dest);
  return a.parent.leaf.text.data || dest.parent.leaf.data.data ||
         dest.parent.label.data || dest.tail.data ? 7 : 0;
}
