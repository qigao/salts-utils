#ifndef TBE_SCHEMA_CMETA_H
#define TBE_SCHEMA_CMETA_H

#include <cmeta/data.h>
#include <cmeta/type_identity.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resolve a schema builtin scalar name to the canonical Salts Core CMeta
 * descriptor. Returns NULL when the name is not a builtin with a canonical
 * descriptor.
 *
 * Integer aliases always resolve to exact-width descriptors. UUID resolves to
 * the process-wide salts_uuid_cmeta_data descriptor. No DataBind-private
 * descriptor is created by this API.
 */
const cmeta_data_desc *schema_cmeta_builtin_data(const char *name);

/**
 * Resolve a schema scalar/structural semantic to its canonical CMeta data kind.
 * Returns non-zero on success. Unsupported/invalid semantics return zero and do
 * not modify *out_kind. Container semantics describe shape only and never select
 * a concrete CSTL implementation.
 */
int schema_cmeta_data_kind(const char *semantic, cmeta_data_kind *out_kind);

/**
 * Build a borrowed CMeta generic type application from a canonical constructor
 * and semantic argument identities.
 *
 * The caller owns the constructor and argument storage, which must outlive the
 * returned identity. Invalid applications return zero without modifying
 * *out_identity. This helper never creates a schema-private generic identity.
 */
int schema_cmeta_generic_identity(cmeta_type_identity *out_identity,
                                  const cmeta_generic_desc *constructor,
                                  const cmeta_type_identity *const *args,
                                  size_t arity);

/**
 * Build a borrowed, allocation-free CMeta struct data descriptor.
 *
 * Structural identity/layout lives in CMeta. Schema-only metadata such as wire
 * names, aliases, validation and fingerprinting is intentionally not embedded in
 * this descriptor. All input metadata must outlive the returned descriptor.
 * Invalid inputs return zero without modifying either output.
 */
int schema_cmeta_struct_data(cmeta_data_desc *out_data,
                             cmeta_data_struct_shape *out_shape,
                             const char *stable_id,
                             const char *display_name,
                             const cmeta_type_desc *storage_type,
                             const cmeta_struct_desc *layout,
                             const cmeta_data_field_desc *fields,
                             size_t field_count);

/**
 * Build a borrowed, allocation-free CMeta enum data descriptor.
 *
 * Enum structural identity lives in CMeta; schema wire aliases/annotations stay
 * in the schema overlay. All input metadata must outlive the returned
 * descriptor. Invalid inputs return zero without modifying either output.
 */
int schema_cmeta_enum_data(cmeta_data_desc *out_data,
                           cmeta_data_enum_shape *out_shape,
                           const char *stable_id,
                           const char *display_name,
                           const cmeta_type_desc *storage_type,
                           const cmeta_enum_desc *meta);

#ifdef __cplusplus
}
#endif

#endif
