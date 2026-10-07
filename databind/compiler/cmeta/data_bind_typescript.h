#ifndef DATA_BIND_TYPESCRIPT_H
#define DATA_BIND_TYPESCRIPT_H

#include <salts/bindings/module.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*DataBindTypeScriptWriteFn)(void *context, const char *text, size_t size);

typedef struct DataBindTypeScriptOptions {
  /** ASCII identifier prefix; the emitted type is <name_prefix>Bindings. */
  const char *name_prefix;
  size_t max_exports;
  size_t max_depth;
  size_t max_nodes;
  size_t max_output_bytes;
} DataBindTypeScriptOptions;

/**
 * Emit an exported TypeScript interface for the same existing C/C++ CMeta
 * bindings passed to salts_quickjs_push_module. No IDL/schema or VM required.
 * Names/types come from canonical FunctionData, field and operation metadata.
 * Object method providers are asked to bind metadata; native methods are not
 * invoked. Providers/objects must stay immutable and alive throughout emission.
 *
 * Scalars, shape-based ordinary enums, structs and statically described collections/maps
 * are supported. Exact 64-bit integers map to bigint; bytes to ArrayBuffer;
 * maps use QuickJS's array-of-{key,value} representation. Unsupported or
 * recursive shapes fail with TRAIT_MISSING/CAPACITY_EXCEEDED, never `any`.
 * Export/field/enum names currently require ASCII, with controls/quotes escaped;
 * non-ASCII names and enum-bits providers return TRAIT_MISSING before any write.
 * Validation/size preflight precedes writes. A sink failure may leave partial
 * output: the caller owns file staging and atomic publication. No native state
 * is retained. Work is bounded by max_nodes/max_depth/max_output_bytes.
 */
cmeta_status data_bind_typescript_emit(
    const salts_binding_module *module, const DataBindTypeScriptOptions *options,
    DataBindTypeScriptWriteFn write, void *context);

#ifdef __cplusplus
}
#endif
#endif
