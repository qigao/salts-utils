#ifndef SALTS_BINDINGS_MODULE_H
#define SALTS_BINDINGS_MODULE_H

#include <cmeta/invokable.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact adapters are generated in the binding TU from native declarations.
 * context and all descriptor storage are borrowed. No function pointer casts. */
typedef cmeta_status (*salts_binding_invoke_fn)(void *context, void *result,
    const void *const *arguments, size_t count);
typedef struct salts_binding_native {
  const cmeta_function_data_desc *data;
  salts_binding_invoke_fn invoke;
} salts_binding_native;

typedef struct salts_binding_function {
  const char *name;
  /* Exactly one of invokable/native is supplied. Native adapters are generated
   * from real C/C++ types; invokables must be unmodified successful bind results. */
  const cmeta_invokable *invokable;
  const salts_binding_native *native;
  void *context;
} salts_binding_function;

typedef struct salts_binding_property {
  const char *name;
  const salts_binding_native *get;
  const salts_binding_native *set;
} salts_binding_property;

typedef struct salts_binding_native_object {
  /* instance and immutable member arrays outlive all VM proxies/closures.
   * Each native method receives instance as its context. */
  void *instance;
  const salts_binding_function *methods;
  size_t method_count;
  const salts_binding_property *properties;
  size_t property_count;
} salts_binding_native_object;

typedef struct salts_binding_object {
  const char *name;
  const cmeta_object_ref *object;
  const salts_binding_native_object *native;
} salts_binding_object;

/* Query returns borrowed metadata or NULL for an ambiguous/missing adapter. */
const cmeta_function_data_desc *salts_binding_function_data(const salts_binding_function *function);
/* Validation rejects malformed/ambiguous bindings and unsupported directions.
 * Invocation additionally checks count and storage; native errors propagate. */
cmeta_status salts_binding_function_validate(const salts_binding_function *function);
cmeta_status salts_binding_function_invoke(const salts_binding_function *function,
    void *result, const void *const *arguments, size_t count);
cmeta_status salts_binding_native_object_validate(const salts_binding_native_object *object,
    size_t max_members);

/**
 * One native export surface, shared by VM registration and declaration tools.
 * Entries select generated exact native adapters or admitted canonical CMeta
 * bindings. The former derive metadata from external native declarations; the
 * latter must come from unmodified successful bind/borrow operations. This
 * record retains no ownership and declares no parallel lifecycle model.
 *
 * Returned VM functions/proxies borrow native objects, descriptors, providers,
 * callable capture dependencies and any outer Plugin lease. Those owners must
 * outlive every retained VM closure/proxy. The module and entry arrays are only
 * needed during registration; each VM copies its borrowed binding records.
 * Nested native-object member arrays remain borrowed and must stay alive.
 * Registration and calls follow the VM's single-threaded/externally serialized
 * access contract; the module does not synchronize shared native state.
 */
typedef struct salts_binding_module {
  const salts_binding_function *functions;
  size_t function_count;
  const salts_binding_object *objects;
  size_t object_count;
} salts_binding_module;

/** Validate names, duplicate exports, CMeta admission and synchronous IN calls.
 * max_exports bounds total entries and each object's field/method counts.
 * Name collision checks are O(n^2) within these bounds. Method providers are
 * asked to bind metadata, without invoking native methods; they must be stable.
 * Empty modules are valid. Native objects are borrowed, never moved/retained.
 */
cmeta_status salts_binding_module_validate(
    const salts_binding_module *module, size_t max_exports);

/** Make a fresh borrowed handle preserving the canonical object's providers. */
cmeta_status salts_binding_object_borrow(
    const cmeta_object_ref *source, cmeta_object_ref *out);

#ifdef __cplusplus
}
#endif
#endif
