#include <salts_unicode.h>
#include <jinja/jinja_cmeta.h>
#include <cstdint>

static_assert(SALTS_UNICODE_VERSION_MAJOR == 17, "Unicode 17 public API");

int main() {
  std::uint32_t props = 0;
  if (salts_unicode_scalar_properties(0x41u, &props) != SALTS_UNICODE_OK)
    return 1;
  if ((props & SALTS_UNICODE_PROPERTY_XID_START) == 0u) return 2;
  salts_unicode_grapheme_break gcb = SALTS_UNICODE_GRAPHEME_OTHER;
  if (salts_unicode_grapheme_break_class(0x41u, &gcb) != SALTS_UNICODE_OK)
    return 3;
  return 0;
}
