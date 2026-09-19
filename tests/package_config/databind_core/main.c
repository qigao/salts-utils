#include <data_bind_format_provider.h>

int databind_core_consumer(void) {
  DataBindFormatReader reader = DATA_BIND_FORMAT_READER_INIT;
  return reader.reader == 0 ? 0 : 1;
}
