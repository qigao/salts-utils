#include "nested_native_generics_native.h"
#include <cmeta/operation.h>

#include <cstddef>
#include <iterator>
#include <type_traits>

static_assert(std::is_standard_layout<Batch_t>::value);
static_assert(sizeof(((Batch_t *)nullptr)->matrix) > sizeof(vec_t));

extern "C" {
std::size_t nested_native_generics_c_size(void);
std::size_t nested_native_generics_c_offset(std::size_t field);
const cmeta_receiver_operation_set *nested_native_generics_c_generic_methods(std::size_t field);

int nested_native_generics_cpp_generic_owners(void) {
  enum { METADATA_ERROR = 1, OWNER_ERROR, RESOLUTION_ERROR, REJECTION_ERROR };
  const cmeta_data_desc *data = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (Flat_cmeta_data(&data, &error) != DATA_BIND_OK || !cmeta_data_desc_valid(data))
    return METADATA_ERROR;
  const auto *shape = static_cast<const cmeta_data_struct_shape *>(data->shape);
  cmeta_type_desc value_type = cmeta_type_uint32;
  cmeta_type_desc key_type = *SALTS_TSTR_CMETA_TYPE_REF;
  const cmeta_type_desc *value_arguments[] = {&value_type};
  const cmeta_type_desc *map_arguments[] = {&key_type, &value_type};
  struct Operation {
    const cmeta_generic_desc *constructor;
    const char *method;
    const cmeta_type_desc *const *arguments;
    std::size_t arity;
  };
  const Operation operations[] = {
    {&stl_vec_generic_desc, "push", value_arguments, std::size(value_arguments)},
    {&stl_set_generic_desc, "add", value_arguments, std::size(value_arguments)},
    {&stl_map_generic_desc, "put", map_arguments, std::size(map_arguments)}
  };
  if (shape->field_count != std::size(operations)) return METADATA_ERROR;
  for (std::size_t field = 0u; field < std::size(operations); ++field) {
    const auto &operation = operations[field];
    const cmeta_declared_type *declared = shape->layout->fields[field].declared_type;
    const cmeta_receiver_operation_set *methods = nested_native_generics_c_generic_methods(field);
    if (!cmeta_declared_type_valid(declared) || !cmeta_receiver_operation_set_valid(methods) ||
        !cmeta_type_equal(methods->receiver_type, declared->storage_type) ||
        !cmeta_generic_desc_equal(methods->owner, declared->constructor) ||
        !cmeta_generic_desc_equal(methods->owner, operation.constructor))
      return OWNER_ERROR;
    cmeta_type_desc receiver = *declared->storage_type;
    cmeta_generic_desc owner = *operation.constructor;
    owner.display_name = "independent C++ constructor display";
    cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
    if (cmeta_receiver_operation_resolve(methods, &receiver, &owner, operation.method,
          operation.arguments, operation.arity, &resolution) != CMETA_RECEIVER_RESOLVE_OK ||
        resolution.operation != cmeta_receiver_operation_find(methods, operation.method))
      return RESOLUTION_ERROR;
    cmeta_generic_desc wrong_owner = *operation.constructor;
    wrong_owner.stable_id = "test.generic.wrong-owner";
    if (!cmeta_generic_desc_valid(&wrong_owner) ||
        cmeta_receiver_operation_resolve(methods, &receiver, &wrong_owner, operation.method,
          operation.arguments, operation.arity, &resolution) != CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH ||
        resolution.operation != nullptr || resolution.argument_index != CMETA_RECEIVER_ARGUMENT_NONE)
      return REJECTION_ERROR;
  }
  return 0;
}

int nested_native_generics_cpp_recursive_identity(void) {
  enum { METADATA_ERROR = 1, IDENTITY_ERROR, REJECTION_ERROR };
  const cmeta_data_desc *data = nullptr;
  const cmeta_data_desc *user = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (Batch_cmeta_data(&data, &error) != DATA_BIND_OK ||
      User_cmeta_data(&user, &error) != DATA_BIND_OK ||
      !cmeta_data_desc_valid(data) || !cmeta_data_desc_valid(user))
    return METADATA_ERROR;
  const auto *shape = static_cast<const cmeta_data_struct_shape *>(data->shape);
  cmeta_generic_desc vec = stl_vec_generic_desc;
  cmeta_generic_desc map = stl_map_generic_desc;
  const cmeta_type_identity integer = *cmeta_type_identity_of(&cmeta_type_int32);
  const cmeta_type_identity text = *cmeta_type_identity_of(SALTS_TSTR_CMETA_TYPE_REF);
  const cmeta_type_identity record = *cmeta_type_identity_of(user->storage_type);
  const cmeta_type_identity *integers[] = {&integer};
  const cmeta_type_identity *records[] = {&record};
  const cmeta_type_identity integer_list = CMETA_TYPE_ID_APPLY_INIT(&vec, integers);
  const cmeta_type_identity record_list = CMETA_TYPE_ID_APPLY_INIT(&vec, records);
  const cmeta_type_identity *map_arguments[] = {&text, &record};
  const cmeta_type_identity record_map = CMETA_TYPE_ID_APPLY_INIT(&map, map_arguments);
  const cmeta_type_identity *matrix_arguments[] = {&integer_list};
  const cmeta_type_identity *nested_records_arguments[] = {&record_map};
  const cmeta_type_identity *groups_arguments[] = {&text, &record_list};
  const cmeta_type_identity expected[] = {
    CMETA_TYPE_ID_APPLY_INIT(&vec, matrix_arguments),
    CMETA_TYPE_ID_APPLY_INIT(&vec, nested_records_arguments),
    CMETA_TYPE_ID_APPLY_INIT(&map, groups_arguments)
  };
  if (shape->field_count != std::size(expected)) return METADATA_ERROR;
  for (std::size_t field = 0u; field < std::size(expected); ++field) {
    const cmeta_type_identity *actual = cmeta_type_identity_of(shape->fields[field].value->storage_type);
    if (actual == &expected[field] || !cmeta_type_identity_equal(actual, &expected[field]))
      return IDENTITY_ERROR;
  }
  const cmeta_type_identity *wrong_arguments[] = {&record_list};
  const cmeta_type_identity wrong = CMETA_TYPE_ID_APPLY_INIT(&vec, wrong_arguments);
  if (!cmeta_type_identity_valid(&wrong) ||
      cmeta_type_identity_equal(cmeta_type_identity_of(shape->fields[0].value->storage_type), &wrong))
    return REJECTION_ERROR;
  return 0;
}

int nested_native_generics_cpp_inspect(void) {
  enum { LAYOUT_ERROR = 1, METADATA_ERROR, IDENTITY_ERROR };
  if (nested_native_generics_c_size() != sizeof(Batch_t) ||
      nested_native_generics_c_offset(0u) != offsetof(Batch_t, matrix) ||
      nested_native_generics_c_offset(1u) != offsetof(Batch_t, records) ||
      nested_native_generics_c_offset(2u) != offsetof(Batch_t, groups))
    return LAYOUT_ERROR;
  const cmeta_data_desc *data = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (Batch_cmeta_data(&data, &error) != DATA_BIND_OK || !cmeta_data_desc_valid(data))
    return METADATA_ERROR;
  const auto *shape = static_cast<const cmeta_data_struct_shape *>(data->shape);
  const cmeta_data_desc *matrix = shape->fields[0].value;
  const cmeta_data_desc *inner = cmeta_data_collection_element_data(matrix);
  const cmeta_type_identity *inner_identity = cmeta_type_identity_of(inner->storage_type);
  const cmeta_type_identity *arguments[] = {cmeta_type_identity_of(&cmeta_type_int32)};
  const cmeta_type_identity expected_inner =
      CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, arguments);
  if (!cmeta_type_identity_equal(inner_identity, &expected_inner)) return IDENTITY_ERROR;
  for (std::size_t field = 0u; field < shape->field_count; ++field) {
    const cmeta_declared_type *declared = shape->layout->fields[field].declared_type;
    if (!cmeta_declared_type_valid(declared)) return IDENTITY_ERROR;
    const cmeta_type_identity *args[2] = {nullptr, nullptr};
    for (std::size_t argument = 0u; argument < declared->arity; ++argument)
      args[argument] = cmeta_type_identity_of(declared->arguments[argument]);
    const cmeta_type_identity *one_argument[] = {args[0]};
    const cmeta_type_identity expected = declared->arity == 1u
        ? cmeta_type_identity CMETA_TYPE_ID_APPLY_INIT(declared->constructor, one_argument)
        : cmeta_type_identity CMETA_TYPE_ID_APPLY_INIT(declared->constructor, args);
    if (!cmeta_type_identity_equal(cmeta_type_identity_of(shape->fields[field].value->storage_type),
                                   &expected)) return IDENTITY_ERROR;
  }
  return 0;
}
}
