#include "jinja_cmeta_environment.h"
#include "jinja_cmeta_internal.h"
#include "jinja_cmeta_artifact.h"

#include <stdlib.h>
#include <string.h>

struct JINJA_CMETA_REGISTRY {
  JINJA_CMETA_CALLABLE entries[JINJA_CMETA_MAX_REGISTERED_FUNCTIONS];
  size_t count;
  JINJA_CMETA_GLOBAL globals[JINJA_CMETA_MAX_REGISTERED_FUNCTIONS];
  size_t global_count;
};

static void jinja_registry_error(JINJA_CMETA_ERROR *error, JINJA_CMETA_STATUS status,
    const char *message) {
  jinja_cmeta_error_set(error, status, 0u, message);
}

static int jinja_registry_name_equal(vstr left, vstr right) {
  return left.len == right.len && (left.len == 0u || memcmp(left.data, right.data, left.len) == 0);
}

static JINJA_CMETA_STATUS jinja_registry_validate(const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error) {
  if (callable == NULL || callable->invoke == NULL || !vstr_is_valid(callable->name) ||
      callable->name.len == 0u || callable->name.len > JINJA_CMETA_MAX_TEMPLATE_BYTES ||
      vstr_utf8_invalid_offset(callable->name) != VSTR_NPOS ||
      memchr(callable->name.data, '.', callable->name.len) != NULL ||
      callable->min_positional > callable->max_positional ||
      (callable->generic != 0 && callable->generic != 1)) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT,
        "invalid registered function descriptor");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (jinja_cmeta_is_builtin_global(callable->name)) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT,
        "registered function name is reserved by a builtin");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_registry_copy_name(JINJA_CMETA_CALLABLE *destination,
    const JINJA_CMETA_CALLABLE *source, JINJA_CMETA_ERROR *error) {
  char *name;
  if (source->name.len == SIZE_MAX) {
    jinja_registry_error(error, JINJA_CMETA_ERR_CAPACITY, "registered function name overflows");
    return JINJA_CMETA_ERR_CAPACITY;
  }
  name = (char *)malloc(source->name.len + 1u);
  if (name == NULL) {
    jinja_registry_error(error, JINJA_CMETA_ERR_OUT_OF_MEMORY,
        "unable to copy registered function name");
    return JINJA_CMETA_ERR_OUT_OF_MEMORY;
  }
  if (source->name.len != 0u) memcpy(name, source->name.data, source->name.len);
  name[source->name.len] = '\0';
  *destination = *source;
  destination->name = vstr_from_buf(name, source->name.len);
  return JINJA_CMETA_OK;
}

JINJA_CMETA_REGISTRY *jinja_cmeta_registry_create(JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_REGISTRY *registry;
  jinja_cmeta_error_clear(error);
  registry = (JINJA_CMETA_REGISTRY *)calloc(1u, sizeof(*registry));
  if (registry == NULL)
    jinja_registry_error(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, "unable to allocate function registry");
  return registry;
}

static JINJA_CMETA_STATUS jinja_registry_write(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_EXTENSION_KIND kind,
    int replace, JINJA_CMETA_ERROR *error) {
  size_t position = SIZE_MAX;
  JINJA_CMETA_CALLABLE copy;
  JINJA_CMETA_STATUS status;
  jinja_cmeta_error_clear(error);
  status = jinja_registry_validate(callable, error);
  if (status != JINJA_CMETA_OK) return status;
  for (size_t i = 0u; i < registry->count; ++i) {
    if (registry->entries[i].extension_kind == kind &&
        jinja_registry_name_equal(registry->entries[i].name, callable->name)) {
      position = i;
      break;
    }
  }
  if (position != SIZE_MAX && !replace) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT,
        "registered callable name already exists in this namespace");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (position == SIZE_MAX && registry->count == JINJA_CMETA_MAX_REGISTERED_FUNCTIONS) {
    jinja_registry_error(error, JINJA_CMETA_ERR_CAPACITY, "callable registry is full");
    return JINJA_CMETA_ERR_CAPACITY;
  }
  copy = *callable;
  copy.extension_kind = kind;
  status = jinja_registry_copy_name(&copy, &copy, error);
  if (status != JINJA_CMETA_OK) return status;
  if (position == SIZE_MAX) registry->entries[registry->count++] = copy;
  else {
    free((void *)registry->entries[position].name.data);
    registry->entries[position] = copy;
  }
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_registry_register(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  if (registry == NULL) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, "registry must not be NULL");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  return jinja_registry_write(registry, callable, JINJA_CMETA_EXTENSION_FUNCTION, 0, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_replace(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  if (registry == NULL) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, "registry must not be NULL");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  return jinja_registry_write(registry, callable, JINJA_CMETA_EXTENSION_FUNCTION, 1, error);
}

static JINJA_CMETA_STATUS jinja_registry_write_kind(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_EXTENSION_KIND kind, int replace,
    JINJA_CMETA_ERROR *error) {
  if (registry == NULL) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, "registry must not be NULL");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  return jinja_registry_write(registry, callable, kind, replace, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_register_filter(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_kind(registry, callable, JINJA_CMETA_EXTENSION_FILTER, 0, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_replace_filter(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_kind(registry, callable, JINJA_CMETA_EXTENSION_FILTER, 1, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_register_test(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_kind(registry, callable, JINJA_CMETA_EXTENSION_TEST, 0, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_replace_test(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_kind(registry, callable, JINJA_CMETA_EXTENSION_TEST, 1, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_register_tag(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_kind(registry, callable, JINJA_CMETA_EXTENSION_KIND_TAG, 0, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_replace_tag(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_CALLABLE *callable, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_kind(registry, callable, JINJA_CMETA_EXTENSION_KIND_TAG, 1, error);
}

static JINJA_CMETA_STATUS jinja_registry_validate_global(const JINJA_CMETA_GLOBAL *global,
    JINJA_CMETA_ERROR *error) {
  if (global == NULL || !vstr_is_valid(global->name) || global->name.len == 0u ||
      global->name.len > JINJA_CMETA_MAX_TEMPLATE_BYTES ||
      vstr_utf8_invalid_offset(global->name) != VSTR_NPOS ||
      memchr(global->name.data, '.', global->name.len) != NULL ||
      global->value.kind > JINJA_CMETA_CALL_VALUE_STRING ||
      (global->value.kind == JINJA_CMETA_CALL_VALUE_STRING &&
       (!vstr_is_valid(global->value.string) ||
        vstr_utf8_invalid_offset(global->value.string) != VSTR_NPOS))) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, "invalid registered global descriptor");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (jinja_cmeta_is_builtin_global(global->name)) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT,
        "registered global name is reserved by a builtin");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_registry_copy_global(JINJA_CMETA_GLOBAL *destination,
    const JINJA_CMETA_GLOBAL *source, JINJA_CMETA_ERROR *error) {
  char *name;
  JINJA_CMETA_CALL_VALUE value;
  if (source->name.len == SIZE_MAX) {
    jinja_registry_error(error, JINJA_CMETA_ERR_CAPACITY, "registered global name overflows");
    return JINJA_CMETA_ERR_CAPACITY;
  }
  name = (char *)malloc(source->name.len + 1u);
  if (name == NULL) {
    jinja_registry_error(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, "unable to copy registered global name");
    return JINJA_CMETA_ERR_OUT_OF_MEMORY;
  }
  if (source->name.len != 0u) memcpy(name, source->name.data, source->name.len);
  name[source->name.len] = '\0';
  value = source->value;
  if (source->value.kind == JINJA_CMETA_CALL_VALUE_STRING) {
    char *string;
    if (source->value.string.len == SIZE_MAX) {
      free(name);
      jinja_registry_error(error, JINJA_CMETA_ERR_CAPACITY, "registered global string overflows");
      return JINJA_CMETA_ERR_CAPACITY;
    }
    string = (char *)malloc(source->value.string.len + 1u);
    if (string == NULL) {
      free(name);
      jinja_registry_error(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, "unable to copy registered global string");
      return JINJA_CMETA_ERR_OUT_OF_MEMORY;
    }
    if (source->value.string.len != 0u)
      memcpy(string, source->value.string.data, source->value.string.len);
    string[source->value.string.len] = '\0';
    value.string = vstr_from_buf(string, source->value.string.len);
  }
  destination->name = vstr_from_buf(name, source->name.len);
  destination->value = value;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_registry_write_global(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_GLOBAL *global, int replace, JINJA_CMETA_ERROR *error) {
  size_t position = SIZE_MAX;
  JINJA_CMETA_GLOBAL copy;
  JINJA_CMETA_STATUS status;
  if (registry == NULL) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, "registry must not be NULL");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  jinja_cmeta_error_clear(error);
  status = jinja_registry_validate_global(global, error);
  if (status != JINJA_CMETA_OK) return status;
  for (size_t i = 0u; i < registry->global_count; ++i) {
    if (jinja_registry_name_equal(registry->globals[i].name, global->name)) {
      position = i;
      break;
    }
  }
  if (position != SIZE_MAX && !replace) {
    jinja_registry_error(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, "registered global name already exists");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (position == SIZE_MAX && registry->global_count == JINJA_CMETA_MAX_REGISTERED_FUNCTIONS) {
    jinja_registry_error(error, JINJA_CMETA_ERR_CAPACITY, "global registry is full");
    return JINJA_CMETA_ERR_CAPACITY;
  }
  status = jinja_registry_copy_global(&copy, global, error);
  if (status != JINJA_CMETA_OK) return status;
  if (position == SIZE_MAX) registry->globals[registry->global_count++] = copy;
  else {
    free((void *)registry->globals[position].name.data);
    if (registry->globals[position].value.kind == JINJA_CMETA_CALL_VALUE_STRING)
      free((void *)registry->globals[position].value.string.data);
    registry->globals[position] = copy;
  }
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_registry_register_global(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_GLOBAL *global, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_global(registry, global, 0, error);
}

JINJA_CMETA_STATUS jinja_cmeta_registry_replace_global(JINJA_CMETA_REGISTRY *registry,
    const JINJA_CMETA_GLOBAL *global, JINJA_CMETA_ERROR *error) {
  return jinja_registry_write_global(registry, global, 1, error);
}

void jinja_cmeta_registry_destroy(JINJA_CMETA_REGISTRY *registry) {
  if (registry == NULL) return;
  for (size_t i = 0u; i < registry->count; ++i) free((void *)registry->entries[i].name.data);
  for (size_t i = 0u; i < registry->global_count; ++i) {
    free((void *)registry->globals[i].name.data);
    if (registry->globals[i].value.kind == JINJA_CMETA_CALL_VALUE_STRING)
      free((void *)registry->globals[i].value.string.data);
  }
  free(registry);
}

void jinja_cmeta_error_name(JINJA_CMETA_ERROR *error, vstr name) {
  if (error == NULL) return;
  size_t length = name.len < sizeof(error->template_name)
      ? name.len : sizeof(error->template_name) - 1u;
  if (length < name.len) {
    size_t partial = vstr_utf8_invalid_offset(vstr_from_buf(name.data, length));
    if (partial != VSTR_NPOS) length = partial;
  }
  if (length != 0u) memcpy(error->template_name, name.data, length);
  error->template_name[length] = '\0';
  error->template_name_length = length;
  error->template_name_truncated = length != name.len;
}

JINJA_CMETA_ENV *jinja_cmeta_env_create(
    const JINJA_CMETA_ENV_OPTIONS *options, JINJA_CMETA_ERROR *error) {
  return jinja_cmeta_env_create_with_registry(options, NULL, error);
}

JINJA_CMETA_ENV *jinja_cmeta_env_create_with_registry(
    const JINJA_CMETA_ENV_OPTIONS *options, const JINJA_CMETA_REGISTRY *registry,
    JINJA_CMETA_ERROR *error) {
  static const JINJA_CMETA_ENV_OPTIONS defaults = JINJA_CMETA_ENV_OPTIONS_INIT;
  const JINJA_CMETA_ENV_OPTIONS *config = options != NULL ? options : &defaults;
  JINJA_TEMPLATE_DELIMITERS delimiters;
  jinja_cmeta_error_clear(error);
  if ((config->loader.load == NULL) != (config->loader.release == NULL)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "loader requires both load and release callbacks");
    return NULL;
  }
  if (jinja_cmeta_compile_config(&config->compile, &delimiters, error) != JINJA_CMETA_OK)
    return NULL;
  if (config->undefined_policy != JINJA_CMETA_UNDEFINED_DEFAULT &&
      config->undefined_policy != JINJA_CMETA_UNDEFINED_STRICT) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "invalid undefined value policy");
    return NULL;
  }
  if ((config->compile.extensions & JINJA_CMETA_EXTENSION_TAG_I18N) != 0u &&
      config->translation == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "i18n extension requires a translation callback");
    return NULL;
  }
  JINJA_CMETA_ENV *env = (JINJA_CMETA_ENV *)calloc(1u, sizeof(*env));
  if (env == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, 0u,
        "unable to allocate template environment");
    return NULL;
  }
  env->options = *config;
  vstr *views[JINJA_CMETA_CONFIG_STRING_COUNT] = {
    &env->options.compile.variable_start_string, &env->options.compile.variable_end_string,
    &env->options.compile.block_start_string, &env->options.compile.block_end_string,
    &env->options.compile.comment_start_string, &env->options.compile.comment_end_string,
    &env->options.compile.line_statement_prefix, &env->options.compile.line_comment_prefix,
    &env->options.compile.newline_sequence
  };
  for (size_t i = 0u; i < JINJA_CMETA_CONFIG_STRING_COUNT; ++i) {
    if (views[i]->data == NULL) continue;
    memcpy(env->strings[i], views[i]->data, views[i]->len);
    views[i]->data = env->strings[i];
  }
  if (env->options.max_loaded_templates == 0u)
    env->options.max_loaded_templates = JINJA_CMETA_DEFAULT_MAX_LOADED_TEMPLATES;
  if (env->options.max_loaded_source_bytes == 0u)
    env->options.max_loaded_source_bytes = JINJA_CMETA_DEFAULT_MAX_LOADED_SOURCE_BYTES;
  if (registry != NULL && registry->count != 0u) {
    env->functions = (JINJA_CMETA_CALLABLE *)calloc(registry->count, sizeof(*env->functions));
    if (env->functions == NULL) {
      jinja_registry_error(error, JINJA_CMETA_ERR_OUT_OF_MEMORY,
          "unable to allocate environment function registry");
      free(env);
      return NULL;
    }
    for (size_t i = 0u; i < registry->count; ++i) {
      JINJA_CMETA_STATUS status = jinja_registry_copy_name(&env->functions[i],
          &registry->entries[i], error);
      if (status != JINJA_CMETA_OK) {
        while (i != 0u) free((void *)env->functions[--i].name.data);
        free(env->functions);
        free(env);
        return NULL;
      }
    }
    env->function_count = registry->count;
    if (registry->global_count != 0u) {
      env->globals = (JINJA_CMETA_ENV_GLOBAL *)calloc(registry->global_count, sizeof(*env->globals));
      if (env->globals == NULL) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, 0u,
            "unable to allocate environment global registry");
        jinja_cmeta_env_destroy(env);
        return NULL;
      }
      for (size_t i = 0u; i < registry->global_count; ++i) {
        JINJA_CMETA_GLOBAL source = registry->globals[i];
        JINJA_CMETA_GLOBAL copy;
        JINJA_CMETA_STATUS status = jinja_registry_copy_global(&copy, &source, error);
        if (status != JINJA_CMETA_OK) {
          jinja_cmeta_env_destroy(env);
          return NULL;
        }
        env->globals[i].name = copy.name;
        env->globals[i].value = copy.value;
      }
      env->global_count = registry->global_count;
    }
  }
  if (registry != NULL && registry->count == 0u && registry->global_count != 0u) {
    env->globals = (JINJA_CMETA_ENV_GLOBAL *)calloc(registry->global_count, sizeof(*env->globals));
    if (env->globals == NULL) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, 0u,
          "unable to allocate environment global registry");
      jinja_cmeta_env_destroy(env);
      return NULL;
    }
    for (size_t i = 0u; i < registry->global_count; ++i) {
      JINJA_CMETA_GLOBAL copy;
      JINJA_CMETA_STATUS status = jinja_registry_copy_global(&copy, &registry->globals[i], error);
      if (status != JINJA_CMETA_OK) {
        jinja_cmeta_env_destroy(env);
        return NULL;
      }
      env->globals[i].name = copy.name;
      env->globals[i].value = copy.value;
    }
    env->global_count = registry->global_count;
  }
  return env;
}

static uint64_t jinja_env_source_hash(vstr source) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (size_t i = 0u; i < source.len; ++i) {
    hash ^= (unsigned char)source.data[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static JINJA_CMETA_TEMPLATE *jinja_env_cache_find(JINJA_CMETA_ENV *env, vstr name,
    vstr source) {
  if (env == NULL) return NULL;
  const uint64_t source_hash = jinja_env_source_hash(source);
  for (size_t i = 0u; i < env->cache_count; ++i) {
    if (vstr_eq(env->cache[i]->name, name) &&
        env->cache_source_bytes[i] == source.len &&
        env->cache_source_hash[i] == source_hash) {
      ++env->cache[i]->references;
      return env->cache[i];
    }
  }
  return NULL;
}

static void jinja_env_cache_add(JINJA_CMETA_ENV *env, JINJA_CMETA_TEMPLATE *templ,
    vstr source) {
  if (env == NULL || templ == NULL) return;
  const uint64_t source_hash = jinja_env_source_hash(source);
  for (size_t i = 0u; i < env->cache_count; ++i) {
    if (!vstr_eq(env->cache[i]->name, templ->name)) continue;
    jinja_cmeta_release(env->cache[i]);
    templ->references = 2u;
    env->cache[i] = templ;
    env->cache_source_hash[i] = source_hash;
    env->cache_source_bytes[i] = source.len;
    return;
  }
  if (env->cache_count == JINJA_CMETA_MAX_CACHED_TEMPLATES) {
    jinja_cmeta_release(env->cache[0]);
    memmove(env->cache, env->cache + 1u,
        (JINJA_CMETA_MAX_CACHED_TEMPLATES - 1u) * sizeof(env->cache[0]));
    memmove(env->cache_source_hash, env->cache_source_hash + 1u,
        (JINJA_CMETA_MAX_CACHED_TEMPLATES - 1u) * sizeof(env->cache_source_hash[0]));
    memmove(env->cache_source_bytes, env->cache_source_bytes + 1u,
        (JINJA_CMETA_MAX_CACHED_TEMPLATES - 1u) * sizeof(env->cache_source_bytes[0]));
    --env->cache_count;
  }
  templ->references = 2u;
  env->cache[env->cache_count] = templ;
  env->cache_source_hash[env->cache_count] = source_hash;
  env->cache_source_bytes[env->cache_count] = source.len;
  ++env->cache_count;
}

void jinja_cmeta_env_cache_clear(JINJA_CMETA_ENV *env) {
  if (env == NULL) return;
  while (env->cache_count != 0u)
    jinja_cmeta_release(env->cache[--env->cache_count]);
}

void jinja_cmeta_env_destroy(JINJA_CMETA_ENV *env) {
  if (env == NULL) return;
  jinja_cmeta_env_cache_clear(env);
  for (size_t i = 0u; i < env->function_count; ++i) free((void *)env->functions[i].name.data);
  for (size_t i = 0u; i < env->global_count; ++i) {
    free((void *)env->globals[i].name.data);
    if (env->globals[i].value.kind == JINJA_CMETA_CALL_VALUE_STRING)
      free((void *)env->globals[i].value.string.data);
  }
  free(env->functions);
  free(env->globals);
  free(env);
}

const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_function(
    const JINJA_CMETA_ENV *env, vstr name) {
  if (env == NULL) return NULL;
  for (size_t i = 0u; i < env->function_count; ++i)
    if (env->functions[i].extension_kind == JINJA_CMETA_EXTENSION_FUNCTION &&
        jinja_registry_name_equal(env->functions[i].name, name)) return &env->functions[i];
  return NULL;
}

const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_filter(
    const JINJA_CMETA_ENV *env, vstr name) {
  if (env == NULL) return NULL;
  for (size_t i = 0u; i < env->function_count; ++i)
    if (env->functions[i].extension_kind == JINJA_CMETA_EXTENSION_FILTER &&
        jinja_registry_name_equal(env->functions[i].name, name)) return &env->functions[i];
  return NULL;
}

const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_test(
    const JINJA_CMETA_ENV *env, vstr name) {
  if (env == NULL) return NULL;
  for (size_t i = 0u; i < env->function_count; ++i)
    if (env->functions[i].extension_kind == JINJA_CMETA_EXTENSION_TEST &&
        jinja_registry_name_equal(env->functions[i].name, name)) return &env->functions[i];
  return NULL;
}

const JINJA_CMETA_CALLABLE *jinja_cmeta_env_find_tag(
    const JINJA_CMETA_ENV *env, vstr name) {
  if (env == NULL) return NULL;
  for (size_t i = 0u; i < env->function_count; ++i)
    if (env->functions[i].extension_kind == JINJA_CMETA_EXTENSION_KIND_TAG &&
        jinja_registry_name_equal(env->functions[i].name, name)) return &env->functions[i];
  return NULL;
}

const JINJA_CMETA_ENV_GLOBAL *jinja_cmeta_env_find_global(
    const JINJA_CMETA_ENV *env, vstr name) {
  if (env == NULL) return NULL;
  for (size_t i = 0u; i < env->global_count; ++i)
    if (jinja_registry_name_equal(env->globals[i].name, name)) return &env->globals[i];
  return NULL;
}

static int jinja_env_admit_name(JINJA_CMETA_ENV *env, vstr name, JINJA_CMETA_ERROR *error) {
  jinja_cmeta_error_clear(error);
  if (env == NULL || !vstr_is_valid(name)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "named compilation requires an environment and valid name view");
    return 0;
  }
  if (name.len > JINJA_CMETA_MAX_TEMPLATE_BYTES) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, 0u,
        "template name exceeds the byte limit");
    return 0;
  }
  if (vstr_utf8_invalid_offset(name) != VSTR_NPOS) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "template name contains malformed UTF-8");
    return 0;
  }
  return 1;
}

/* The public boundary has already admitted name. Compilation takes ownership of
 * its own strings; neither the name view nor a loader lease survives this call. */
static JINJA_CMETA_TEMPLATE *jinja_env_compile(JINJA_CMETA_ENV *env, vstr name,
    vstr source, JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile_with_environment(
      source, &env->options.compile, env, error);
  if (templ != NULL) {
    if (env->options.autoescape_selector != NULL) {
      const int autoescape = env->options.autoescape_selector(
          env->options.autoescape_userdata, name);
      if (autoescape != 0 && autoescape != 1) {
        jinja_cmeta_release(templ);
        templ = NULL;
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
            "autoescape selector must return 0 or 1");
      } else {
        templ->autoescape = autoescape;
      }
    }
    if (templ != NULL) templ->undefined_policy = env->options.undefined_policy;
  }
  if (templ != NULL) {
    char *owned_name = (char *)jinja_cmeta_artifact_allocate(templ, name.len + 1u, sizeof(char));
    if (owned_name != NULL) {
      if (name.len != 0u) memcpy(owned_name, name.data, name.len);
      owned_name[name.len] = '\0';
      templ->name = vstr_from_buf(owned_name, name.len);
      templ->env = env;
    } else {
      const JINJA_CMETA_STATUS status = templ->allocation_status;
      jinja_cmeta_release(templ);
      templ = NULL;
      jinja_cmeta_error_set(error, status, 0u,
          "unable to retain template name");
    }
  }
  if (templ == NULL) jinja_cmeta_error_name(error, name);
  return templ;
}

JINJA_CMETA_TEMPLATE *jinja_cmeta_env_compile(JINJA_CMETA_ENV *env, vstr name,
    vstr source, JINJA_CMETA_ERROR *error) {
  if (!jinja_env_admit_name(env, name, error)) return NULL;
  return jinja_env_compile(env, name, source, error);
}

JINJA_CMETA_TEMPLATE *jinja_cmeta_env_load(JINJA_CMETA_ENV *env, vstr name,
    JINJA_CMETA_ERROR *error) {
  size_t source_bytes = 0u;
  return jinja_cmeta_env_load_bounded(env, name,
      env != NULL ? env->options.max_loaded_source_bytes : 0u, &source_bytes, error);
}

JINJA_CMETA_TEMPLATE *jinja_cmeta_env_load_bounded(JINJA_CMETA_ENV *env, vstr name,
    size_t remaining_source_bytes, size_t *source_bytes, JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_ERROR detail = JINJA_CMETA_ERROR_INIT;
  JINJA_CMETA_SOURCE source = {0};
  JINJA_CMETA_TEMPLATE *templ = NULL;
  if (error == NULL) error = &detail;
  if (!jinja_env_admit_name(env, name, error)) return NULL;
  const JINJA_CMETA_LOADER *loader = &env->options.loader;
  if (loader->load == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_LOADER, 0u, "environment has no template loader");
  } else {
    JINJA_CMETA_STATUS status = loader->load(loader->userdata, name, &source, error);
    if (status != JINJA_CMETA_OK) {
      if (error->message[0] == '\0')
        jinja_cmeta_error_set(error, status, 0u, status == JINJA_CMETA_ERR_NOT_FOUND
            ? "template name not found" : "template loader failed");
      error->status = status;
    } else {
      if (!vstr_is_valid(source.text))
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
            "loader returned an invalid source view");
      else if (source.text.len > env->options.max_loaded_source_bytes ||
               source.text.len > remaining_source_bytes)
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, 0u,
            "loaded template source exceeds the byte budget");
      else {
        /* A render load always consumes a loader lease and source budget; the
         * environment cache only avoids recompiling that admitted source. */
        templ = jinja_env_cache_find(env, name, source.text);
        if (templ == NULL) {
          templ = jinja_env_compile(env, name, source.text, error);
          if (templ != NULL) jinja_env_cache_add(env, templ, source.text);
        }
        if (templ != NULL && source_bytes != NULL) *source_bytes = source.text.len;
      }
      loader->release(loader->userdata, &source);
    }
  }
  if (templ == NULL) jinja_cmeta_error_name(error, name);
  return templ;
}
