#include <salts/bindings/quickjs/module.h>
#include <limits.h>
#include <stdlib.h>

typedef struct QuickJSFunction {
  salts_binding_function binding;
  cmeta_invokable invokable;
  salts_quickjs_limits limits;
} QuickJSFunction;

static void module_function_release(void *opaque) { free(opaque); }

static JSValue module_function(JSContext *context, JSValueConst self,
    int count, JSValueConst *arguments, int magic, void *opaque) {
  QuickJSFunction *function = opaque;
  JSValue result = JS_UNDEFINED;
  bool has_result = false;
  cmeta_status status;
  (void)self;
  (void)magic;
  status = salts_quickjs_call_binding(context, &function->binding,
      count, arguments, function->limits, &result, &has_result);
  if (status != CMETA_OK)
    return JS_ThrowTypeError(context, "native function invocation failed (%d)", (int)status);
  return has_result ? result : JS_UNDEFINED;
}

static JSValue new_function(JSContext *context, const salts_binding_function *binding,
    salts_quickjs_limits limits) {
  QuickJSFunction *function;
  JSValue value;
  size_t count = salts_binding_function_data(binding)->param_count;
  if (count > INT_MAX) return JS_ThrowRangeError(context, "native arity exceeds VM limit");
  function = malloc(sizeof(*function));
  if (function == NULL) return JS_ThrowOutOfMemory(context);
  function->binding = *binding;
  if (binding->invokable != NULL) {
    function->invokable = *binding->invokable;
    function->binding.invokable = &function->invokable;
  }
  function->limits = limits;
  value = JS_NewCClosure(context, module_function, NULL, module_function_release,
      (int)count, 0, function);
  if (JS_IsException(value)) free(function);
  return value;
}

static JSValue new_native_object(JSContext *context, const salts_binding_native_object *object,
    salts_quickjs_limits limits) {
  JSValue result = JS_NewObjectProto(context, JS_NULL);
  size_t i;
  if (JS_IsException(result)) return result;
  for (i = 0; i < object->method_count; ++i) {
    salts_binding_function binding = object->methods[i];
    JSValue value;
    binding.context = object->instance;
    value = new_function(context, &binding, limits);
    if (JS_IsException(value)) goto fail;
    if (JS_DefinePropertyValueStr(context, result, binding.name, value, JS_PROP_ENUMERABLE) < 0) goto fail;
  }
  for (i = 0; i < object->property_count; ++i) {
    const salts_binding_property *property = &object->properties[i];
    salts_binding_function binding = {property->name, NULL, property->get, object->instance};
    JSValue getter = new_function(context, &binding, limits);
    JSValue setter = JS_UNDEFINED;
    JSAtom atom;
    if (JS_IsException(getter)) goto fail;
    if (property->set != NULL) {
      binding.native = property->set;
      setter = new_function(context, &binding, limits);
      if (JS_IsException(setter)) { JS_FreeValue(context, getter); goto fail; }
    }
    atom = JS_NewAtom(context, property->name);
    if (atom == JS_ATOM_NULL) {
      JS_FreeValue(context, getter); JS_FreeValue(context, setter); goto fail;
    }
    {
      int status = JS_DefinePropertyGetSet(context, result, atom, getter, setter, JS_PROP_ENUMERABLE);
      JS_FreeAtom(context, atom);
      if (status < 0) goto fail;
    }
  }
  if (JS_PreventExtensions(context, result) < 0) goto fail;
  return result;
fail:
  JS_FreeValue(context, result);
  return JS_EXCEPTION;
}

cmeta_status salts_quickjs_push_module(JSContext *context,
    const salts_binding_module *module, salts_quickjs_limits limits,
    JSValue *out_value) {
  JSValue result;
  cmeta_status status;
  size_t i;
  if (out_value == NULL) return CMETA_INVALID_ARGUMENT;
  *out_value = JS_UNDEFINED;
  if (context == NULL) return CMETA_INVALID_ARGUMENT;
  status = salts_binding_module_validate(module, limits.max_items);
  if (status != CMETA_OK) return status;
  result = JS_NewObjectProto(context, JS_NULL);
  if (JS_IsException(result)) return CMETA_OUT_OF_MEMORY;
  for (i = 0; i < module->function_count; ++i) {
    JSValue value = new_function(context, &module->functions[i], limits);
    if (JS_IsException(value)) {
      status = CMETA_OUT_OF_MEMORY;
      goto fail;
    }
    if (JS_DefinePropertyValueStr(context, result, module->functions[i].name,
                                 value, JS_PROP_C_W_E) < 0) {
      status = CMETA_CALLBACK_ERROR;
      goto fail;
    }
  }
  for (i = 0; i < module->object_count; ++i) {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    JSValue value = JS_UNDEFINED;
    if (module->objects[i].native != NULL) {
      value = new_native_object(context, module->objects[i].native, limits);
      status = JS_IsException(value) ? CMETA_CALLBACK_ERROR : CMETA_OK;
    } else {
      status = salts_binding_object_borrow(module->objects[i].object, &object);
      if (status != CMETA_OK) goto fail;
      status = salts_quickjs_push_object(context, &object, limits, &value);
      cmeta_object_release(&object);
    }
    if (status != CMETA_OK) goto fail;
    if (JS_DefinePropertyValueStr(context, result, module->objects[i].name,
                                 value, JS_PROP_C_W_E) < 0) {
      status = CMETA_CALLBACK_ERROR;
      goto fail;
    }
  }
  *out_value = result;
  return CMETA_OK;
fail:
  JS_FreeValue(context, result);
  return status;
}

cmeta_status salts_quickjs_call_script(JSContext *context, JSValueConst function,
    const cmeta_function_data_desc *signature, void *result,
    const void *const *arguments, size_t count, salts_quickjs_limits limits) {
  JSValue *values = NULL, value = JS_UNDEFINED;
  cmeta_status status = CMETA_OK;
  size_t i;
  if (context == NULL || !cmeta_function_data_desc_valid(signature) ||
      count != signature->param_count || (count != 0 && arguments == NULL) ||
      (signature->return_data != NULL && result == NULL) || !JS_IsFunction(context, function))
    return CMETA_INVALID_ARGUMENT;
  if (count > limits.max_items || count > INT_MAX || count > SIZE_MAX / sizeof(JSValue))
    return CMETA_CAPACITY_EXCEEDED;
  for (i = 0; i < count; ++i) {
    const cmeta_param_desc *param = cmeta_function_param(signature->function, i);
    if (arguments[i] == NULL) return CMETA_INVALID_ARGUMENT;
    if ((param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_IN ||
        (param->flags & CMETA_PARAM_OWNED) != 0) return CMETA_TRAIT_MISSING;
  }
  if (count != 0) {
    values = malloc(count * sizeof(*values));
    if (values == NULL) return CMETA_OUT_OF_MEMORY;
    for (i = 0; i < count; ++i) values[i] = JS_UNDEFINED;
  }
  for (i = 0; i < count; ++i) {
    status = salts_quickjs_push_cmeta(context, signature->params[i], arguments[i], limits, &values[i]);
    if (status != CMETA_OK) goto done;
  }
  value = JS_Call(context, function, JS_UNDEFINED, (int)count, values);
  if (JS_IsException(value)) { status = CMETA_CALLBACK_ERROR; goto done; }
  if (signature->return_data != NULL)
    status = salts_quickjs_read_cmeta(context, value, signature->return_data, result, limits);
done:
  JS_FreeValue(context, value);
  for (i = 0; i < count; ++i) JS_FreeValue(context, values[i]);
  free(values);
  return status;
}
