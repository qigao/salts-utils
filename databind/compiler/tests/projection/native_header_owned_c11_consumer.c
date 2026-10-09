#include "native_owned_fixture.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
  OwnedPacket a, b, moved;
  OwnedPacket_init(&a);
  OwnedPacket_init(&b);
  OwnedPacket_init(&moved);
  a.has_title = true;
  a.title.size = 5u;
  a.title.data = (char *)malloc(6u);
  a.payload.size = 3u;
  a.payload.data = (unsigned char *)malloc(3u);
  if (!a.title.data || !a.payload.data) return 1;
  memcpy(a.title.data, "hello", 6u);
  memcpy(a.payload.data, "abc", 3u);
  a.count = 9u;
  if (OwnedPacket_clone(&b, &a) != 0 ||
      b.title.data == a.title.data || b.payload.data == a.payload.data ||
      b.title.size != 5u || b.payload.size != 3u ||
      strcmp(b.title.data, "hello") != 0 ||
      memcmp(b.payload.data, "abc", 3u) != 0 || b.count != 9u)
    return 2;
  a.title.data[0] = 'H';
  a.payload.data[0] = 'A';
  if (b.title.data[0] != 'h' || b.payload.data[0] != 'a') return 3;
  if (OwnedPacket_move(&moved, &b) != 0 ||
      b.title.data || b.payload.data || b.count ||
      strcmp(moved.title.data, "hello") != 0) return 4;
  /* Failed deep clone leaves initialized destination untouched. */
  {
    OwnedPacket broken = a;
    broken.payload.data = NULL;
    if (OwnedPacket_clone(&moved, &broken) == 0 ||
        strcmp(moved.title.data, "hello") != 0 ||
        moved.payload.size != 3u || moved.payload.data == NULL) return 5;
  }
  if (OwnedPacket_clone(&moved, &moved) != 0 ||
      OwnedPacket_move(&moved, &moved) != 0) return 6;
  OwnedPacket_clear(&a);
  OwnedPacket_clear(&b);
  OwnedPacket_clear(&moved);
  OwnedPacket_clear(&moved);
  return a.title.data || moved.payload.data ? 7 : 0;
}
