#include <data_bind.h>

int main(void) {
  return data_bind_abi_version() == DATA_BIND_ABI_VERSION ? 0 : 1;
}
