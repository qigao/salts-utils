#ifndef SALTS_BINDINGS_NATIVE_H
#define SALTS_BINDINGS_NATIVE_H

#include <salts/bindings/module.h>
#include <cmeta/invoke_decl.h>

/* C11 declarations live in a separate binding TU. Native headers/definitions
 * remain ordinary C. Each (type, name) row supplies the type C cannot infer. */
#ifndef __cplusplus
#define SALTS_BIND_C_DATA(T) _Generic((T *)0, void *: NULL, bool *: &cmeta_data_bool, \
    int *: &cmeta_data_int, long *: &cmeta_data_long, float *: &cmeta_data_float, double *: &cmeta_data_double)
#define SALTS_BIND_C_ROW(row, ignored) \
    (CMETA_PP_TUPLE_GET_0(row), CMETA_PP_TUPLE_GET_1(row), CMETA_PARAM_IN)
#define SALTS_BIND_C_VALUE(row, ignored) SALTS_BIND_C_DATA(CMETA_PP_TUPLE_GET_0(row))
#define SALTS_BIND_C_ARG(index, row, ignored) native_args[index] = (void *)args[index];
#define SALTS_BIND_C_DECLARE_I(R, F, ...) FunctionInvokeDecl(unknown, R, F, __VA_ARGS__)

#define SALTS_BIND_C_FUNCTION(R, F, ...) \
    SALTS_BIND_C_DECLARE_I(R, F, CMETA_PP_MAP_COMMA(SALTS_BIND_C_ROW, ~, __VA_ARGS__)); \
    static const cmeta_data_desc *const F##__binding_params[] = { \
        CMETA_PP_MAP_COMMA(SALTS_BIND_C_VALUE, ~, __VA_ARGS__) }; \
    static const cmeta_function_data_desc F##__binding_data = { \
        sizeof(cmeta_function_data_desc), FunctionMeta(F), SALTS_BIND_C_DATA(R), \
        F##__binding_params, CMETA_PP_NARG(__VA_ARGS__) }; \
    static cmeta_status F##__binding_invoke(void *context, void *out, const void *const *args, size_t count) { \
        void *native_args[CMETA_PP_NARG(__VA_ARGS__)]; \
        (void)context; \
        if (count != CMETA_PP_NARG(__VA_ARGS__) || args == NULL) return CMETA_INVALID_ARGUMENT; \
        CMETA_PP_FOR_EACH_I(SALTS_BIND_C_ARG, ~, __VA_ARGS__) \
        return FunctionInvoke(F)(out, native_args, count) ? CMETA_OK : CMETA_CALLBACK_ERROR; \
    } \
    static const salts_binding_native F##__binding = { &F##__binding_data, F##__binding_invoke }

#define SALTS_BIND_C_FUNCTION0(R, F) \
    Function0InvokeDecl(unknown, R, F); \
    static const cmeta_function_data_desc F##__binding_data = { \
        sizeof(cmeta_function_data_desc), FunctionMeta(F), SALTS_BIND_C_DATA(R), NULL, 0 }; \
    static cmeta_status F##__binding_invoke(void *context, void *out, const void *const *args, size_t count) { \
        (void)context; (void)args; \
        return FunctionInvoke(F)(out, NULL, count) ? CMETA_OK : CMETA_INVALID_ARGUMENT; \
    } \
    static const salts_binding_native F##__binding = { &F##__binding_data, F##__binding_invoke }

#define SALTS_BIND_C_EXPORT(F) { #F, NULL, &F##__binding, NULL }

/* Mutable scalar fields of an existing C struct; exact member type is checked
 * at compile time. The resulting property table and instance remain borrowed. */
#define SALTS_BIND_C_FIELD(Name, Object, Field, Type) \
    _Static_assert(_Generic(&((Object *)0)->Field, Type *: 1, default: 0), \
        "Binding field type must exactly match the native mutable member"); \
    static const cmeta_param_desc Name##__param = { \
        sizeof(cmeta_param_desc), "value", CMETA_TYPEOF(Type), CMETA_PARAM_IN }; \
    static const cmeta_data_desc *const Name##__values[] = {SALTS_BIND_C_DATA(Type)}; \
    static const cmeta_function_desc Name##__get_function = { \
        sizeof(cmeta_function_desc), #Name ".get", CMETA_TYPEOF(Type), NULL, 0, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE, CMETA_RESULT_VALUE }; \
    static const cmeta_function_desc Name##__set_function = { \
        sizeof(cmeta_function_desc), #Name ".set", &cmeta_type_void, &Name##__param, 1, CMETA_EFFECT_STATEFUL, CMETA_PROP_NONE, CMETA_RESULT_UNKNOWN }; \
    static const cmeta_function_data_desc Name##__get_data = { \
        sizeof(cmeta_function_data_desc), &Name##__get_function, SALTS_BIND_C_DATA(Type), NULL, 0 }; \
    static const cmeta_function_data_desc Name##__set_data = { \
        sizeof(cmeta_function_data_desc), &Name##__set_function, NULL, Name##__values, 1 }; \
    static cmeta_status Name##__get(void *context, void *out, const void *const *args, size_t count) { \
        (void)args; \
        if (context == NULL || out == NULL || count != 0) return CMETA_INVALID_ARGUMENT; \
        *(Type *)out = ((const Object *)context)->Field; \
        return CMETA_OK; \
    } \
    static cmeta_status Name##__set(void *context, void *out, const void *const *args, size_t count) { \
        (void)out; \
        if (context == NULL || count != 1 || args == NULL || args[0] == NULL) return CMETA_INVALID_ARGUMENT; \
        ((Object *)context)->Field = *(const Type *)args[0]; \
        return CMETA_OK; \
    } \
    static const salts_binding_native Name##__getter = {&Name##__get_data, Name##__get}; \
    static const salts_binding_native Name##__setter = {&Name##__set_data, Name##__set}; \
    static const salts_binding_property Name = {#Field, &Name##__getter, &Name##__setter}
#endif
#endif
