#include "owned_error.plugin_client.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<
    databind_16_OwnedErrorPlugin_5_Store_4_Read__error>);

int main() {
  databind_16_OwnedErrorPlugin_5_Store_4_Read__error error =
      databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_INIT;
  databind_16_OwnedErrorPlugin_5_Store_4_Read__error_init(&error);
  if (databind_16_OwnedErrorPlugin_5_Store_4_Read__error_select(
          &error,
          databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_1) !=
      DATA_BIND_OK)
    return 1;
  return databind_16_OwnedErrorPlugin_5_Store_4_Read__error_clear(&error) ==
                 DATA_BIND_OK
             ? 0
             : 2;
}
