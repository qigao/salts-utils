#ifndef JINJA_CMETA_REFLECTION_H
#define JINJA_CMETA_REFLECTION_H

#include "jinja_cmeta.h"
#include <cmeta/invokable.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JINJA_CMETA_MAX_REFLECTED_ARGUMENTS 16u

/** Adapt an unmodified, successfully admitted CMeta invokable for registry use.
 * out receives a zero value on failure. name is borrowed until registration;
 * function, its metadata, capture, and provider must outlive the environment
 * and every compiled template. The adapter neither retains nor moves them.
 *
 * Synchronous IN parameters only, up to MAX_REFLECTED_ARGUMENTS. Native nodes
 * borrow exact matching storage; template scalars convert to bool, int, long,
 * float or double. Integer ranges and float overflow are checked. No coercion
 * between integer, float and bool categories, defaults or ownership transfer.
 * All arguments are required; keywords use canonical parameter names.
 * Results support those scalar types plus void, with VALUE ownership.
 * Accepted signatures must also belong to the installed CMeta callable universe.
 *
 * Returns INVALID_ARGUMENT for invalid metadata/name/output, UNSUPPORTED for
 * unsupported types/direction/ownership/effects, CAPACITY for too many params.
 * At invocation, bad/duplicate/missing arguments return RENDER, integer range
 * failures return CAPACITY, and CMeta execution failures are propagated as the
 * corresponding allocation/capacity error or RENDER.
 *
 * Complete executable usage and rejection examples are maintained in
 * jinja/test/test_jinja_reflection.c. Register the produced callable through
 * jinja_cmeta_registry_register or jinja_cmeta_registry_register_filter.
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_callable_from_invokable(
    JINJA_CMETA_CALLABLE *out, vstr name, const cmeta_invokable *function,
    JINJA_CMETA_ERROR *error);

#ifdef __cplusplus
}
#endif
#endif
