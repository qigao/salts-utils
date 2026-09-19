#include <data_bind_json_provider.h>

int databind_adapter_consumer(void) {
  return data_bind_json_format_provider() != 0 ? 0 : 1;
}
