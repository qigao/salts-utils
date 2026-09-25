#include "data_bind_native_descriptor.h"

#include <string.h>

static void native_descriptor_error(DataBindError *error, DataBindStatus code,
                                    const char *message) {
  if (error == NULL) return;
  *error = (DataBindError)DATA_BIND_ERROR_INIT;
  error->code = code;
  if (message != NULL) {
    size_t n = strlen(message);
    if (n >= sizeof(error->message)) n = sizeof(error->message) - 1u;
    memcpy(error->message, message, n);
    error->message[n] = '\0';
  }
}

DataBindStatus data_bind_native_descriptor_validate(
    const DataBindNativeDescriptor *descriptor, DataBindError *error) {
  if (descriptor == NULL) {
    native_descriptor_error(error, DATA_BIND_ERR_INVALID_ARG,
                            "native descriptor is null");
    return DATA_BIND_ERR_INVALID_ARG;
  }
  if (descriptor->struct_size < sizeof(DataBindNativeDescriptor) ||
      descriptor->abi_version != DATA_BIND_NATIVE_DESCRIPTOR_ABI_VERSION) {
    native_descriptor_error(error, DATA_BIND_ERR_SCHEMA,
                            "native descriptor ABI mismatch");
    return DATA_BIND_ERR_SCHEMA;
  }
  if (descriptor->schema_type == NULL || descriptor->schema_type[0] == '\0' ||
      descriptor->native_data == NULL ||
      !cmeta_data_desc_valid(descriptor->native_data)) {
    native_descriptor_error(error, DATA_BIND_ERR_SCHEMA,
                            "native descriptor is incomplete");
    return DATA_BIND_ERR_SCHEMA;
  }
  return DATA_BIND_OK;
}
