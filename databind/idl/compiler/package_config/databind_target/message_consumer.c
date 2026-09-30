#include "installed_message_native.h"
#include <data_bind_native_binding.h>
#include <string.h>

int main(void) {
  const DataBindMessageNativeArtifact *artifact = Config_native_artifact();
  DataBindNativeTypeBinding binding = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (!data_bind_message_native_artifact_valid(artifact)) return 1;
  if (strcmp(artifact->type_name, "Config") != 0) return 2;
  if (artifact->native_binding(&binding, &error) != DATA_BIND_OK) return 3;
  if (binding.data == NULL || binding.idl_type_name == NULL) return 4;
  return 0;
}
