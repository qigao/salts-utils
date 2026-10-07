#ifndef SALTS_BINDINGS_QUICKJS_MODULE_H
#define SALTS_BINDINGS_QUICKJS_MODULE_H
#include <salts/bindings/module.h>
#include <salts/bindings/quickjs/cmeta.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Create a module object from the same native exports used by declaration tools.
 * Success returns an owned JSValue (JS_FreeValue before context destruction).
 * Failure leaves out_value undefined. No global/module registry is modified.
 * limits.max_items bounds exports. Native lifetimes remain borrowed as specified
 * in salts_binding_module, including when scripts retain extracted functions.
 */
cmeta_status salts_quickjs_push_module(JSContext *context,
    const salts_binding_module *module, salts_quickjs_limits limits,
    JSValue *out_value);

cmeta_status salts_quickjs_call_binding(JSContext *context,
    const salts_binding_function *binding, int argument_count,
    JSValueConst *arguments, salts_quickjs_limits limits,
    JSValue *out_result, bool *out_has_result);

/* Native output must be semantic-zero storage. JS exceptions remain pending
 * for the host to consume; argument/result JSValues are always released. */
cmeta_status salts_quickjs_call_script(JSContext *context, JSValueConst function,
    const cmeta_function_data_desc *signature, void *result,
    const void *const *arguments, size_t count, salts_quickjs_limits limits);

#ifdef __cplusplus
}
#endif
#endif
