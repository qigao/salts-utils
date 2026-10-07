#include "data_bind_typescript.h"

#include <stdio.h>
#include <string.h>

typedef struct TypeScript {
  const DataBindTypeScriptOptions *options;
  DataBindTypeScriptWriteFn write;
  void *context;
  size_t nodes;
  size_t bytes;
  cmeta_status status;
} TypeScript;

static bool emit(TypeScript *out, const char *text) {
  size_t size = strlen(text);
  if (out->status != CMETA_OK) return false;
  if (size > out->options->max_output_bytes - out->bytes) {
    out->status = CMETA_CAPACITY_EXCEEDED;
    return false;
  }
  out->bytes += size;
  if (out->write != NULL && !out->write(out->context, text, size)) {
    out->status = CMETA_CALLBACK_ERROR;
    return false;
  }
  return true;
}

static bool quoted(TypeScript *out, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (text == NULL) { out->status = CMETA_INVALID_ARGUMENT; return false; }
  if (!emit(out, "\"")) return false;
  for (; *p != 0; ++p) {
    char buffer[7];
    if (*p < 0x20 || *p >= 0x7f) {
      /* CMeta export names in this projection use printable ASCII; reject
       * non-ASCII rather than corrupt UTF-8 by escaping individual bytes. */
      if (*p > 0x7f) { out->status = CMETA_TRAIT_MISSING; return false; }
      snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned)*p);
    } else if (*p == '"' || *p == '\\') {
      buffer[0] = '\\'; buffer[1] = (char)*p; buffer[2] = 0;
    } else { buffer[0] = (char)*p; buffer[1] = 0; }
    if (!emit(out, buffer)) return false;
  }
  return emit(out, "\"");
}

static bool type(TypeScript *out, const cmeta_data_desc *data, size_t depth);

static bool fields(TypeScript *out, const cmeta_data_desc *data,
                   size_t depth, bool readonly) {
  const cmeta_data_struct_shape *shape = data->shape;
  size_t i;
  if (shape == NULL) { out->status = CMETA_INVALID_ARGUMENT; return false; }
  for (i = 0; i < shape->field_count; ++i) {
    if (!emit(out, readonly ? "readonly " : "") ||
        !quoted(out, shape->fields[i].name) || !emit(out, ": ") ||
        !type(out, shape->fields[i].value, depth + 1) || !emit(out, "; "))
      return false;
  }
  return true;
}

static bool type(TypeScript *out, const cmeta_data_desc *data, size_t depth) {
  if (depth > out->options->max_depth || out->nodes >= out->options->max_nodes) {
    out->status = CMETA_CAPACITY_EXCEEDED; return false;
  }
  ++out->nodes;
  if (!cmeta_data_desc_valid(data)) {
    out->status = CMETA_INVALID_ARGUMENT; return false;
  }
  switch (data->kind) {
    case CMETA_DATA_BOOL: return emit(out, "boolean");
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT:
      return emit(out, ((const cmeta_data_integer_shape *)data->shape)->bits == 64
          ? "bigint" : "number");
    case CMETA_DATA_FLOAT: return emit(out, "number");
    case CMETA_DATA_STRING: return emit(out, "string");
    case CMETA_DATA_BYTES: return emit(out, "ArrayBuffer");
    case CMETA_DATA_ENUM: {
      const cmeta_data_enum_shape *shape = data->shape;
      size_t i;
      if (shape == NULL || shape->meta == NULL || shape->meta->count == 0) break;
      for (i = 0; i < shape->meta->count; ++i) {
        if (i != 0 && !emit(out, " | ")) return false;
        if (!quoted(out, shape->meta->items[i].symbol)) return false;
      }
      return true;
    }
    case CMETA_DATA_STRUCT:
      return emit(out, "{ ") && fields(out, data, depth, false) && emit(out, "}");
    case CMETA_DATA_SEQUENCE:
    case CMETA_DATA_SET: {
      const cmeta_data_desc *element = cmeta_data_collection_element_data(data);
      if (element == NULL) break;
      return emit(out, "Array<") && type(out, element, depth + 1) && emit(out, ">");
    }
    case CMETA_DATA_MAP: {
      const cmeta_data_desc *key = cmeta_data_map_key_data(data);
      const cmeta_data_desc *value = cmeta_data_map_value_data(data);
      if (key == NULL || value == NULL) break;
      return emit(out, "Array<{ key: ") && type(out, key, depth + 1) &&
          emit(out, "; value: ") && type(out, value, depth + 1) && emit(out, " }>");
    }
    default: break;
  }
  out->status = CMETA_TRAIT_MISSING;
  return false;
}

static bool function(TypeScript *out, const cmeta_function_data_desc *data) {
  size_t i;
  if (!cmeta_function_data_desc_valid(data)) {
    out->status = CMETA_INVALID_ARGUMENT; return false;
  }
  if ((data->function->effects & CMETA_EFFECT_ASYNC) != 0) {
    out->status = CMETA_TRAIT_MISSING; return false;
  }
  if (!emit(out, "(")) return false;
  for (i = 0; i < data->param_count; ++i) {
    const cmeta_param_desc *param = cmeta_function_param(data->function, i);
    char name[32];
    if ((param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_IN ||
        (param->flags & CMETA_PARAM_OWNED) != 0) {
      out->status = CMETA_TRAIT_MISSING; return false;
    }
    /* Positional names avoid reserved TS keywords without renaming exports. */
    snprintf(name, sizeof(name), "arg%zu: ", i);
    if ((i != 0 && !emit(out, ", ")) || !emit(out, name) ||
        !type(out, data->params[i], 0)) return false;
  }
  return emit(out, ") => ") && (data->return_data == NULL
      ? emit(out, "void") : type(out, data->return_data, 0));
}

static bool object(TypeScript *out, const cmeta_object_ref *object) {
  size_t i;
  if (!emit(out, "{ ")) return false;
  if (object->data->kind == CMETA_DATA_STRUCT &&
      !fields(out, object->data, 0, object->field_provider == NULL ||
                                  object->field_provider->assign == NULL))
    return false;
  if (object->operation_provider != NULL) {
    for (i = 0; i < object->operations->operation_count; ++i) {
      cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
      const cmeta_receiver_operation *method = &object->operations->operations[i];
      out->status = cmeta_object_operation_invokable_bind(object, method, &invokable);
      if (out->status != CMETA_OK || !emit(out, "readonly ") ||
          !quoted(out, method->name) || !emit(out, ": ") ||
          !function(out, invokable.data) || !emit(out, "; ")) return false;
    }
  }
  return emit(out, "}");
}

static bool native_object(TypeScript *out, const salts_binding_native_object *object) {
  size_t i;
  if (!emit(out, "{ ")) return false;
  for (i = 0; i < object->property_count; ++i) {
    const salts_binding_property *property = &object->properties[i];
    if ((property->set == NULL && !emit(out, "readonly ")) ||
        !quoted(out, property->name) || !emit(out, ": ") ||
        !type(out, property->get->data->return_data, 0) || !emit(out, "; ")) return false;
  }
  for (i = 0; i < object->method_count; ++i) {
    if (!emit(out, "readonly ") || !quoted(out, object->methods[i].name) ||
        !emit(out, ": ") || !function(out, salts_binding_function_data(&object->methods[i])) ||
        !emit(out, "; ")) return false;
  }
  return emit(out, "}");
}

static cmeta_status module_emit(TypeScript *out, const salts_binding_module *module) {
  size_t i;
  if (!emit(out, "export interface ") || !emit(out, out->options->name_prefix) ||
      !emit(out, "Bindings {\n")) return out->status;
  for (i = 0; i < module->function_count; ++i) {
    if (!emit(out, "  ") || !quoted(out, module->functions[i].name) ||
        !emit(out, ": ") || !function(out, salts_binding_function_data(&module->functions[i])) ||
        !emit(out, ";\n")) return out->status;
  }
  for (i = 0; i < module->object_count; ++i) {
    if (!emit(out, "  ") || !quoted(out, module->objects[i].name) || !emit(out, ": ") ||
        !(module->objects[i].native != NULL ? native_object(out, module->objects[i].native)
                                          : object(out, module->objects[i].object)) ||
        !emit(out, ";\n")) return out->status;
  }
  emit(out, "}\n");
  return out->status;
}

static bool identifier(const char *name) {
  const unsigned char *p = (const unsigned char *)name;
  if (name == NULL || *p == 0) return false;
  for (; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          *p == '_' || (*p >= '0' && *p <= '9' && p != (const unsigned char *)name)))
      return false;
  return true;
}

cmeta_status data_bind_typescript_emit(
    const salts_binding_module *module, const DataBindTypeScriptOptions *options,
    DataBindTypeScriptWriteFn write, void *context) {
  TypeScript out = {0};
  cmeta_status status;
  if (options == NULL || write == NULL || !identifier(options->name_prefix))
    return CMETA_INVALID_ARGUMENT;
  status = salts_binding_module_validate(module, options->max_exports);
  if (status != CMETA_OK) return status;
  out.options = options;
  status = module_emit(&out, module);
  if (status != CMETA_OK) return status;
  out.nodes = out.bytes = 0;
  out.write = write;
  out.context = context;
  return module_emit(&out, module);
}
