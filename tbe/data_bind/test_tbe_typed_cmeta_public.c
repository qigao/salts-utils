#include "cmeta_graph_generated.h"
#include <stdio.h>

/* Public-only, release-build-safe checks: no private validator or generated
 * implementation include may make this consumer link accidentally. */
int main(void) {
  const struct cmeta_data_desc *data = NULL;
  const struct cmeta_data_desc *sentinel;
  DataBindError error = DATA_BIND_ERROR_INIT;

  if (Sample_cmeta_data(&data, &error) != DATA_BIND_OK || data == NULL) {
    fputs("public graph getter did not publish a valid graph\n", stderr);
    return 1;
  }
  sentinel = data;
  if (Unsupported_cmeta_data(&data, &error) != DATA_BIND_ERR_SCHEMA) {
    fputs("unsupported public graph request did not fail\n", stderr);
    return 2;
  }
  if (data != sentinel) {
    fputs("failed public graph request changed the output sentinel\n", stderr);
    return 3;
  }
  return 0;
}
