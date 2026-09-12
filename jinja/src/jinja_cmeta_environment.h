#ifndef JINJA_CMETA_ENVIRONMENT_H
#define JINJA_CMETA_ENVIRONMENT_H

#include "jinja_cmeta.h"
#include "parser/jinja_template_lexer.h"

enum { JINJA_CMETA_CONFIG_STRING_COUNT = JINJA_TEMPLATE_DELIMITER_COUNT + 3u };

typedef struct JINJA_CMETA_ENV_GLOBAL {
  vstr name;
  JINJA_CMETA_CALL_VALUE value;
} JINJA_CMETA_ENV_GLOBAL;

/* Immutable after create. Runtime instances and their budgets belong to render,
 * not to the environment; extension registries are copied at environment creation. */
struct JINJA_CMETA_ENV {
  JINJA_CMETA_ENV_OPTIONS options;
  char strings[JINJA_CMETA_CONFIG_STRING_COUNT][JINJA_TEMPLATE_MAX_DELIMITER_BYTES + 1u];
  JINJA_CMETA_CALLABLE *functions;
  size_t function_count;
  JINJA_CMETA_ENV_GLOBAL *globals;
  size_t global_count;
  JINJA_CMETA_TEMPLATE *cache[JINJA_CMETA_MAX_CACHED_TEMPLATES];
  uint64_t cache_source_hash[JINJA_CMETA_MAX_CACHED_TEMPLATES];
  size_t cache_source_bytes[JINJA_CMETA_MAX_CACHED_TEMPLATES];
  size_t cache_count;
};

const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_function(
    const JINJA_CMETA_ENV *env, vstr name);
const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_filter(
    const JINJA_CMETA_ENV *env, vstr name);
const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_test(
    const JINJA_CMETA_ENV *env, vstr name);
const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_tag(
    const JINJA_CMETA_ENV *env, vstr name);
const JINJA_CMETA_ENV_GLOBAL *jinja_cmeta_env_find_global(
    const JINJA_CMETA_ENV *env, vstr name);

/* Render-local admission. Successful compilation consumes source bytes; every
 * successful loader lease is released, including quota/compile failures. */
JINJA_CMETA_TEMPLATE *jinja_cmeta_env_load_bounded(JINJA_CMETA_ENV *env, vstr name,
    size_t remaining_source_bytes, size_t *source_bytes, JINJA_CMETA_ERROR *error);

#endif
