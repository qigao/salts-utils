#include <data_bind_cmeta_adapter_plan.h>

#include <stdio.h>

static const cmeta_param_desc params[] = {
    {sizeof(cmeta_param_desc), "fd", &cmeta_type_uint32, CMETA_PARAM_IN},
    {sizeof(cmeta_param_desc), "offset", &cmeta_type_uint64, CMETA_PARAM_IN}};

static const cmeta_function_desc function = {
    sizeof(cmeta_function_desc),
    "wasi.fixture",
    &cmeta_type_uint32,
    params,
    2u,
    CMETA_EFFECT_UNKNOWN,
    CMETA_PROP_NONE};

static const cmeta_function_desc *function_at(void *context, size_t index) {
  (void)context;
  return index == 0u ? &function : NULL;
}

static bool file_write(void *context, const char *data, size_t size) {
  return fwrite(data, 1u, size, (FILE *)context) == size;
}

int main(int argc, char **argv) {
  DataBindCMetaFunctionManifest manifest = {1u, function_at, NULL};
  DataBindCMetaAdapterPlanConfig config = {"installed_preview1"};
  DataBindCMetaAdapterPlanStatus status;
  size_t error_index = SIZE_MAX;
  FILE *file;

  if (argc != 2) return 2;
  file = fopen(argv[1], "wb");
  if (file == NULL) return 3;

  status = data_bind_cmeta_adapter_plan_emit(
      &manifest, &config, file_write, file, &error_index);
  if (fclose(file) != 0) return 4;

  return status == DATA_BIND_CMETA_ADAPTER_PLAN_OK &&
                 error_index == SIZE_MAX
             ? 0
             : 5;
}
