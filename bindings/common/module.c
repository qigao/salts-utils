#include <salts/bindings/module.h>
#include <string.h>

static const char *export_name(const salts_binding_module *module, size_t index) {
  return index < module->function_count ? module->functions[index].name
      : module->objects[index - module->function_count].name;
}

const cmeta_function_data_desc *salts_binding_function_data(const salts_binding_function *function) {
  if (function == NULL) return NULL;
  if (function->native != NULL && function->invokable == NULL) return function->native->data;
  if (function->native == NULL && cmeta_invokable_valid(function->invokable)) return function->invokable->data;
  return NULL;
}

cmeta_status salts_binding_function_validate(const salts_binding_function *function) {
  const cmeta_function_data_desc *data = salts_binding_function_data(function);
  size_t i;
  if (!cmeta_function_data_desc_valid(data) ||
      (function->native != NULL && function->native->invoke == NULL))
    return CMETA_INVALID_ARGUMENT;
  if ((data->function->effects & CMETA_EFFECT_ASYNC) != 0)
    return CMETA_TRAIT_MISSING;
  for (i = 0; i < data->param_count; ++i) {
    const cmeta_param_desc *param = cmeta_function_param(data->function, i);
    if ((param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_IN ||
        (param->flags & CMETA_PARAM_OWNED) != 0)
      return CMETA_TRAIT_MISSING;
  }
  return CMETA_OK;
}

cmeta_status salts_binding_function_invoke(const salts_binding_function *function,
    void *result, const void *const *arguments, size_t count) {
  cmeta_status status = salts_binding_function_validate(function);
  const cmeta_function_data_desc *data;
  size_t i;
  if (status != CMETA_OK) return status;
  data = salts_binding_function_data(function);
  if (count != data->param_count || (count != 0 && arguments == NULL) ||
      (data->return_data != NULL && result == NULL)) return CMETA_INVALID_ARGUMENT;
  for (i = 0; i < count; ++i)
    if (arguments[i] == NULL) return CMETA_INVALID_ARGUMENT;
  return function->native != NULL
      ? function->native->invoke(function->context, result, arguments, count)
      : cmeta_invokable_invoke(function->invokable, result, arguments);
}

static const char *native_member_name(const salts_binding_native_object *object, size_t index) {
  return index < object->method_count ? object->methods[index].name
      : object->properties[index - object->method_count].name;
}

cmeta_status salts_binding_native_object_validate(const salts_binding_native_object *object,
    size_t max_members) {
  size_t i, j, count;
  if (object == NULL || object->instance == NULL ||
      (object->method_count != 0 && object->methods == NULL) ||
      (object->property_count != 0 && object->properties == NULL)) return CMETA_INVALID_ARGUMENT;
  if (object->method_count > max_members ||
      object->property_count > max_members - object->method_count) return CMETA_CAPACITY_EXCEEDED;
  count = object->method_count + object->property_count;
  for (i = 0; i < count; ++i) {
    const char *name = native_member_name(object, i);
    if (name == NULL || *name == 0) return CMETA_INVALID_ARGUMENT;
    for (j = 0; j < i; ++j)
      if (strcmp(name, native_member_name(object, j)) == 0) return CMETA_INVALID_ARGUMENT;
  }
  for (i = 0; i < object->method_count; ++i) {
    cmeta_status status = salts_binding_function_validate(&object->methods[i]);
    if (status != CMETA_OK) return status;
    if (object->methods[i].native == NULL) return CMETA_INVALID_ARGUMENT;
  }
  for (i = 0; i < object->property_count; ++i) {
    const salts_binding_property *property = &object->properties[i];
    salts_binding_function get = {property->name, NULL, property->get, object->instance};
    cmeta_status status = salts_binding_function_validate(&get);
    if (status != CMETA_OK) return status;
    if (property->get->data->param_count != 0 || property->get->data->return_data == NULL)
      return CMETA_INVALID_ARGUMENT;
    if (property->set != NULL) {
      salts_binding_function set = {property->name, NULL, property->set, object->instance};
      status = salts_binding_function_validate(&set);
      if (status != CMETA_OK) return status;
      if (property->set->data->param_count != 1 || property->set->data->return_data != NULL ||
          !cmeta_data_desc_equal(property->get->data->return_data, property->set->data->params[0]))
        return CMETA_INVALID_ARGUMENT;
    }
  }
  return CMETA_OK;
}

cmeta_status salts_binding_module_validate(
    const salts_binding_module *module, size_t max_exports) {
  size_t i, j, count;
  if (module == NULL ||
      (module->function_count != 0 && module->functions == NULL) ||
      (module->object_count != 0 && module->objects == NULL))
    return CMETA_INVALID_ARGUMENT;
  if (module->function_count > max_exports ||
      module->object_count > max_exports - module->function_count)
    return CMETA_CAPACITY_EXCEEDED;
  count = module->function_count + module->object_count;
  for (i = 0; i < count; ++i) {
    const char *name = export_name(module, i);
    if (name == NULL || name[0] == '\0') return CMETA_INVALID_ARGUMENT;
    for (j = 0; j < i; ++j)
      if (strcmp(name, export_name(module, j)) == 0)
        return CMETA_INVALID_ARGUMENT;
  }
  for (i = 0; i < module->function_count; ++i) {
    cmeta_status status = salts_binding_function_validate(&module->functions[i]);
    if (status != CMETA_OK) return status;
  }
  for (i = 0; i < module->object_count; ++i) {
    const cmeta_object_ref *object = module->objects[i].object;
    const cmeta_data_struct_shape *shape;
    if (module->objects[i].native != NULL) {
      cmeta_status status;
      if (object != NULL) return CMETA_INVALID_ARGUMENT;
      status = salts_binding_native_object_validate(module->objects[i].native, max_exports);
      if (status != CMETA_OK) return status;
      continue;
    }
    if (!cmeta_object_ref_valid(object))
      return CMETA_INVALID_ARGUMENT;
    shape = object->data->kind == CMETA_DATA_STRUCT ? object->data->shape : NULL;
    if (shape != NULL && shape->field_count > max_exports)
      return CMETA_CAPACITY_EXCEEDED;
    if (object->operation_provider != NULL) {
      size_t k;
      if (object->operations->operation_count > max_exports)
        return CMETA_CAPACITY_EXCEEDED;
      for (j = 0; j < object->operations->operation_count; ++j) {
        cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
        const cmeta_receiver_operation *method = &object->operations->operations[j];
        cmeta_status status = cmeta_object_operation_invokable_bind(object, method, &invokable);
        if (status != CMETA_OK) return status;
        {
          salts_binding_function binding = {method->name, &invokable, NULL, NULL};
          status = salts_binding_function_validate(&binding);
        }
        if (status != CMETA_OK) return status;
        if (shape != NULL)
          for (k = 0; k < shape->field_count; ++k)
            if (strcmp(method->name, shape->fields[k].name) == 0)
              return CMETA_TYPE_MISMATCH;
      }
    }
  }
  return CMETA_OK;
}

cmeta_status salts_binding_object_borrow(
    const cmeta_object_ref *source, cmeta_object_ref *out) {
  if (!cmeta_object_ref_valid(source) || out == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (source->field_provider != NULL || source->operation_provider != NULL)
    return cmeta_object_borrow_with_providers(out, source->object, source->data,
        source->field_provider, source->operation_provider);
  return cmeta_object_borrow(out, source->object, source->data, source->operations);
}
