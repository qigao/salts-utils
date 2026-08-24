#include "tbe_cbind_multitu_fixture.h"

const cmeta_data_desc *tbe_cbind_multitu_external_text_data(void) {
  return &tbe_cbind_multitu_text_data;
}

const cmeta_data_desc *tbe_cbind_multitu_external_string_text_data(void) {
  return &tbe_cbind_multitu_string_text_data;
}

const cmeta_data_desc *tbe_cbind_multitu_external_uuid_data(void) {
  return &turbo_uuid_cmeta_data;
}
