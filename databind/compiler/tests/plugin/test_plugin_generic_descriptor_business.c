#include "generic_plugin.plugin.h"

enum { GENERIC_PLUGIN_MAX_ITEMS = 2 };

int databind_13_GenericPlugin_14_GenericService_7_Inspect(const GenericRequest_t *request,
                                                          GenericResponse_t *response) {
  GenericResponse_t staged = {0};
  size_t index;
  size_t count;
  int status = -1;

  if (request == NULL || response == NULL ||
      GenericResponse_values_map_t_size(&response->values) != 0u)
    return status;
  count = GenericRequest_values_vec_t_size(&request->values);
  if (count > GENERIC_PLUGIN_MAX_ITEMS) return status;

  GenericResponse_init(&staged);
  for (index = 0u; index < count; ++index) {
    const GenericItem_t *item = GenericRequest_values_vec_t_at_const(&request->values, index);
    if (item == NULL || item->name == NULL ||
        GenericResponse_values_map_t_put(&staged.values, item->name, *item) != STL_OK)
      goto cleanup;
  }
  if (cmeta_data_value_move(&GenericResponse_CMETA_DATA, response, &staged) == CMETA_OK) status = 0;

cleanup:
  GenericResponse_clear(&staged);
  return status;
}
