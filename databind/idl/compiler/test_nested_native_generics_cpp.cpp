#include "nested_native_generics_native.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout<Batch_t>::value);
static_assert(sizeof(((Batch_t *)nullptr)->matrix) > sizeof(vec_t));

extern "C" {
std::size_t nested_native_generics_c_size(void);
std::size_t nested_native_generics_c_offset(std::size_t field);
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
