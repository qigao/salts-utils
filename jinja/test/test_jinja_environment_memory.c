/* Compile the environment owner in isolation to inject allocation failures. */
#include "jinja_cmeta_environment.h"
#include "jinja_cmeta_internal.h"
#include "jinja_cmeta_artifact.h"
#include "tinytest.h"
#include <stdlib.h>

static size_t allocation_calls, fail_call, live_allocations;
static void *test_malloc(size_t bytes) {
  if (++allocation_calls == fail_call) return NULL;
  void *memory = malloc(bytes);
  if (memory != NULL) ++live_allocations;
  return memory;
}
static void *test_calloc(size_t count, size_t bytes) {
  if (++allocation_calls == fail_call) return NULL;
  void *memory = calloc(count, bytes);
  if (memory != NULL) ++live_allocations;
  return memory;
}
static void test_free(void *memory) {
  if (memory != NULL) --live_allocations;
  free(memory);
}
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#include "../src/jinja_cmeta_environment.c"
#undef malloc
#undef calloc
#undef free

static JINJA_CMETA_STATUS test_count_arguments(void *userdata,
    const JINJA_CMETA_CALL_CONTEXT *context, JINJA_CMETA_CALL_RESULT *result) {
  (void)userdata;
  result->value = (JINJA_CMETA_CALL_VALUE){.kind = JINJA_CMETA_CALL_VALUE_INTEGER,
      .integer = (int64_t)context->argument_count};
  return JINJA_CMETA_OK;
}

spec("Jinja environment allocation rollback") {
  static JINJA_CMETA_REGISTRY *registry;
  static JINJA_CMETA_ENV *env;
  static JINJA_CMETA_ERROR error;
  before_each() {
    allocation_calls = fail_call = live_allocations = 0u;
    registry = NULL;
    env = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
  }
  after_each() {
    jinja_cmeta_env_destroy(env);
    jinja_cmeta_registry_destroy(registry);
    check_equal(live_allocations, (size_t)0u);
  }
  it("releases every initialized snapshot entry on allocation failure") {
    for (int with_functions = 0; with_functions <= 1; ++with_functions) {
      registry = jinja_cmeta_registry_create(&error);
      check_not_null(registry);
      JINJA_CMETA_GLOBAL first = {.name = vstr_from_cstr("first"),
          .value = {.kind = JINJA_CMETA_CALL_VALUE_STRING, .string = vstr_from_cstr("one")}};
      JINJA_CMETA_GLOBAL second = {.name = vstr_from_cstr("second"),
          .value = {.kind = JINJA_CMETA_CALL_VALUE_STRING, .string = vstr_from_cstr("two")}};
      check_equal(jinja_cmeta_registry_register_global(registry, &first, &error), JINJA_CMETA_OK);
      check_equal(jinja_cmeta_registry_register_global(registry, &second, &error), JINJA_CMETA_OK);
      if (with_functions) {
        JINJA_CMETA_CALLABLE callable = {.name = vstr_from_cstr("argc"), .invoke = test_count_arguments};
        check_equal(jinja_cmeta_registry_register(registry, &callable, &error), JINJA_CMETA_OK);
      }
      const size_t baseline = live_allocations;
      allocation_calls = 0u;
      env = jinja_cmeta_env_create_with_registry(NULL, registry, &error);
      check_not_null(env);
      const size_t required = allocation_calls;
      check_equal(env->global_count, (size_t)2u);
      check_true(vstr_eq(env->globals[1].value.string, second.value.string));
      jinja_cmeta_env_destroy(env);
      env = NULL;
      check_equal(live_allocations, baseline);
      for (size_t failure = 1u; failure <= required; ++failure) {
        allocation_calls = 0u;
        fail_call = failure;
        env = jinja_cmeta_env_create_with_registry(NULL, registry, &error);
        check_null(env);
        check_equal(error.status, JINJA_CMETA_ERR_OUT_OF_MEMORY);
        check_equal(live_allocations, baseline);
        check_equal(allocation_calls, failure);
        fail_call = 0u;
        env = jinja_cmeta_env_create_with_registry(NULL, registry, NULL);
        check_not_null(env);
        check_true(vstr_eq(env->globals[1].value.string, second.value.string));
        jinja_cmeta_env_destroy(env);
        env = NULL;
        check_equal(live_allocations, baseline);
      }
      fail_call = 0u;
      jinja_cmeta_registry_destroy(registry);
      registry = NULL;
    }
  }
}
