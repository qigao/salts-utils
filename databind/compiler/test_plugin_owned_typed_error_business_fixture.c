#include "owned_error.plugin.h"

#include <cstl/byte_buffer.h>
#include <tstr.h>

#include <string.h>

int databind_16_OwnedErrorPlugin_5_Store_4_Read(
    const Request_t *request,
    Response_t *response,
    databind_16_OwnedErrorPlugin_5_Store_4_Read__error *error) {
  static const unsigned char payload[] = {0xdeu, 0xadu, 0xbeu, 0xefu};
  DataBindStatus status;

  if (request == NULL || response == NULL || error == NULL) return -1;
  status = databind_16_OwnedErrorPlugin_5_Store_4_Read__error_clear(error);
  if (status != DATA_BIND_OK) return -2;

  if (request->id == 7u) {
    status = databind_16_OwnedErrorPlugin_5_Store_4_Read__error_select(
        error, databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_1);
    if (status != DATA_BIND_OK) return -3;
    error->payload.error_1.detail = tstr_dup("owned-text");
    if (error->payload.error_1.detail == NULL) {
      (void)databind_16_OwnedErrorPlugin_5_Store_4_Read__error_clear(error);
      return -4;
    }
    return 0;
  }

  if (request->id == 8u) {
    status = databind_16_OwnedErrorPlugin_5_Store_4_Read__error_select(
        error, databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_2);
    if (status != DATA_BIND_OK) return -5;
    if (stl_byte_buffer_resize(
            &error->payload.error_2.payload, sizeof(payload)) != STL_OK) {
      (void)databind_16_OwnedErrorPlugin_5_Store_4_Read__error_clear(error);
      return -6;
    }
    memcpy(stl_byte_buffer_data(&error->payload.error_2.payload),
           payload, sizeof(payload));
    return 0;
  }

  if (request->id == 99u) return -9;

  response->value = request->id * 10u;
  return 0;
}
