#include "native_value_fixture.h"
#include "data_bind_native.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

int main(void) {
  ValuePacket_native_cmeta_binding view = {0};
  ValuePacket_native_cmeta_binding owner = {0};
  ValuePacket source = {0}, copy = {0}, moved = {0};
  bool is_zero = false;
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativePlan *plan = NULL;
  ValuePacket executable = {0};
  unsigned char workspace[16384] = {0};

  if (ValuePacket_native_cmeta_bind(&view) != 0 ||
      view.reflection.mode != CMETA_DATA_REFLECTION_VIEW ||
      cmeta_data_value_traits_supported(&view.data))
    return 1;
  if (ValuePacket_native_cmeta_value_bind(NULL) == 0 ||
      ValuePacket_native_cmeta_value_bind(&owner) != 0 ||
      owner.reflection.mode != CMETA_DATA_REFLECTION_VALUE ||
      !cmeta_data_desc_valid(&owner.data) ||
      !cmeta_data_value_traits_supported(&owner.data) ||
      !cmeta_data_struct_constructible(&owner.data))
    return 2;

  if (cmeta_data_value_init_zero(&owner.data, &source) != CMETA_OK ||
      cmeta_data_value_init_zero(&owner.data, &copy) != CMETA_OK ||
      cmeta_data_value_init_zero(&owner.data, &moved) != CMETA_OK)
    return 3;
  source.count = 91u;
  source.delta = -17;
  source.active = true;
  if (cmeta_data_value_copy(&owner.data, &copy, &source) != CMETA_OK ||
      copy.count != 91u || copy.delta != -17 || !copy.active)
    return 4;
  if (cmeta_data_value_move(&owner.data, &moved, &copy) != CMETA_OK ||
      moved.count != 91u || moved.delta != -17 || !moved.active ||
      cmeta_data_value_is_zero(&owner.data, &copy, &is_zero) != CMETA_OK ||
      !is_zero)
    return 5;
  if (cmeta_data_value_restore_zero(&owner.data, &moved) != CMETA_OK ||
      cmeta_data_value_is_zero(&owner.data, &moved, &is_zero) != CMETA_OK ||
      !is_zero ||
      cmeta_data_value_restore_zero(&owner.data, &source) != CMETA_OK)
    return 6;

  /* No caller-invisible snapshot may survive across binding relocations:
   * providers and shape pointers belong to the final owner object. */
  if (owner.data.shape != &owner.reflection ||
      owner.reflection.structure.layout != &owner.layout ||
      owner.reflection.structure.fields != owner.data_fields ||
      owner.layout.fields != owner.layout_fields ||
      strcmp(owner.data.stable_id, "tbe.native.NativeValue.v1.ValuePacket.data") != 0)
    return 7;
  /* Prove the generated VALUE descriptor admits the real native DataBind
   * execution plan, not only CMeta's standalone copy/move facade. Neither
   * BinaryFormatPlan nor the old generated *_native.c participates here. */
  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 128u;
  options.max_owned_bytes = sizeof(workspace);
  if (data_bind_native_plan_compile(
          &options, &owner.data, &plan, &diagnostic) != DATA_BIND_OK ||
      plan == NULL ||
      data_bind_native_plan_data(plan) != &owner.data) {
    data_bind_native_plan_free(plan);
    return 8;
  }
  if (data_bind_native_plan_init(
          plan, &options, &executable, sizeof(executable),
          &diagnostic) != DATA_BIND_OK) {
    data_bind_native_plan_free(plan);
    return 9;
  }
  executable.count = 37u;
  executable.delta = -5;
  executable.active = true;
  if (data_bind_native_plan_clear(
          plan, &options, &executable, sizeof(executable),
          &diagnostic) != DATA_BIND_OK ||
      executable.count != 0u || executable.delta != 0 ||
      executable.active) {
    data_bind_native_plan_free(plan);
    return 10;
  }
  data_bind_native_plan_free(plan);
  return 0;
}
