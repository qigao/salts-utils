#include "native_value_fixture.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

int main(void) {
  ValuePacket_native_cmeta_binding view = {0};
  ValuePacket_native_cmeta_binding owner = {0};
  ValuePacket source = {0}, copy = {0}, moved = {0};
  bool is_zero = false;

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
  return 0;
}
