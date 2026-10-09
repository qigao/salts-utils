#include "contract_value_native.h"
#include "data_bind_native.h"

#include <stdint.h>
#include <string.h>

int main(void) {
  OwnedValue_native_cmeta_binding binding = {0};
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diag = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativePlan *plan = NULL, *invalid_plan = NULL;
  OwnedValue value = {0}, copy = {0};
  cmeta_data_reflection_shape borrowed_view;
  cmeta_data_desc borrowed_data;
  const unsigned char payload[] = {1u, 4u, 9u};
  unsigned char workspace[16384] = {0};

  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 128u;
  options.max_owned_bytes = 4096u;
  if (OwnedValue_native_cmeta_value_bind(&binding) != 0 ||
      binding.reflection.mode != CMETA_DATA_REFLECTION_VALUE ||
      !cmeta_data_desc_valid(&binding.data) ||
      !cmeta_data_value_traits_supported(&binding.data))
    return 1;

  /* View reflection is never a substitutable owned/executable provider. */
  borrowed_view = binding.reflection;
  borrowed_data = binding.data;
  borrowed_view.mode = CMETA_DATA_REFLECTION_VIEW;
  borrowed_data.shape = &borrowed_view;
  if (data_bind_native_plan_compile(
          &options, &borrowed_data, &invalid_plan, &diag) == DATA_BIND_OK ||
      invalid_plan != NULL)
    return 2;

  if (data_bind_native_plan_compile(
          &options, &binding.data, &plan, &diag) != DATA_BIND_OK ||
      plan == NULL)
    return 3;
  if (data_bind_native_plan_init(
          plan, &options, &value, sizeof(value), &diag) != DATA_BIND_OK ||
      data_bind_native_plan_init(
          plan, &options, &copy, sizeof(copy), &diag) != DATA_BIND_OK)
    return 4;
  if (cmeta_data_buffer_assign(
          &databind_native_text_cmeta_data, &value.label,
          (const unsigned char *)"owned", 5u, 64u) != CMETA_OK ||
      cmeta_data_buffer_assign(
          &databind_native_bytes_cmeta_data, &value.payload,
          payload, sizeof(payload), 64u) != CMETA_OK)
    return 5;
  value.count = 42u;
  if (cmeta_data_value_copy(&binding.data, &copy, &value) != CMETA_OK ||
      copy.count != 42u ||
      !copy.label.data || !copy.payload.data ||
      copy.label.data == value.label.data ||
      copy.payload.data == value.payload.data ||
      strcmp(copy.label.data, "owned") != 0 ||
      memcmp(copy.payload.data, payload, sizeof(payload)) != 0)
    return 6;
  if (data_bind_native_plan_clear(
          plan, &options, &value, sizeof(value), &diag) != DATA_BIND_OK ||
      data_bind_native_plan_clear(
          plan, &options, &copy, sizeof(copy), &diag) != DATA_BIND_OK ||
      value.label.data || copy.payload.data ||
      value.payload.size || copy.label.size)
    return 7;
  data_bind_native_plan_free(plan);
  return 0;
}
