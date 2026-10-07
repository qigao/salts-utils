#include "jinja_cmeta_reflection.h"
#include <cmeta/cmeta.h>
#include <stdlib.h>
#include "tinytest.h"

FunctionDeclResult(stateful, long, CMETA_RESULT_VALUE, reflected_subtract,
    (long, left, CMETA_PARAM_IN), (long, right, CMETA_PARAM_IN));
static size_t native_calls;
typed_any(stateful, long, subtract_adapter, (long left, long right)) {
  ++native_calls;
  return left - right;
}
FunctionDeclResult(value, double, CMETA_RESULT_VALUE, reflected_scale, (double, value, CMETA_PARAM_IN));
typed_any(value, double, scale_adapter, (double value)) { return value * 2.0; }
FunctionDeclResult(value, bool, CMETA_RESULT_VALUE, reflected_positive, (int, value, CMETA_PARAM_IN));
typed_any(value, bool, positive_adapter, (int value)) { return value > 0; }
FunctionDeclResult(value, float, CMETA_RESULT_VALUE, reflected_half, (int, value, CMETA_PARAM_IN));
typed_any(value, float, half_adapter, (int value)) { return value / 2.0f; }

FunctionDecl(value, double, reflected_unknown, (double, value, CMETA_PARAM_IN));
FunctionDeclResult(async, double, CMETA_RESULT_VALUE, reflected_async, (double, value, CMETA_PARAM_IN));
typed_any(async, double, async_adapter, (double value)) { return value; }

spec("Jinja admitted native callables") {
  static JINJA_CMETA_REGISTRY *registry;
  static JINJA_CMETA_ENV *env;
  static JINJA_CMETA_TEMPLATE *templ;
  static JINJA_CMETA_ERROR error;
  static char *output;
  static cmeta_invokable subtract, scale, positive, half;
  static vstr root;
  before_each() {
    env = NULL;
    templ = NULL;
    output = NULL;
    native_calls = 0u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    root = vstr_from_cstr("<root>");
    subtract = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    scale = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    positive = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    half = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    check_equal(cmeta_invokable_bind(FunctionMeta(reflected_subtract), subtract_adapter, &subtract), CMETA_OK);
    check_equal(cmeta_invokable_bind(FunctionMeta(reflected_scale), scale_adapter, &scale), CMETA_OK);
    check_equal(cmeta_invokable_bind(FunctionMeta(reflected_positive), positive_adapter, &positive), CMETA_OK);
    check_equal(cmeta_invokable_bind(FunctionMeta(reflected_half), half_adapter, &half), CMETA_OK);
    registry = jinja_cmeta_registry_create(&error);
    check_not_null(registry);
  }
  after_each() {
    free(output);
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
    jinja_cmeta_registry_destroy(registry);
  }
  it("uses canonical parameter names for positional keyword and filter calls") {
    JINJA_CMETA_CALLABLE callable;
    check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr("sub"), &subtract, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_registry_register(registry, &callable, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_registry_register_filter(registry, &callable, &error), JINJA_CMETA_OK);
    env = jinja_cmeta_env_create_with_registry(NULL, registry, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("native"), vstr_from_cstr(
        "{{sub(9,4)}}|{{sub(right=4,left=9)}}|{{9|sub(right=4)}}"), &error);
    check_not_null(templ);
    JINJA_CMETA_STATUS status = jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error);
    info("native status=%d offset=%zu calls=%zu message=%s", (int)status, error.offset, native_calls, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "5|5|5");
    check_equal(native_calls, (size_t)3u);
  }
  it("converts native numeric results without losing boolean identity") {
    JINJA_CMETA_CALLABLE callable;
    const cmeta_invokable *functions[] = {&scale, &positive, &half};
    const char *names[] = {"scale", "positive", "half"};
    for (size_t i = 0u; i < sizeof(functions) / sizeof(functions[0]); ++i) {
      check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr(names[i]), functions[i], &error), JINJA_CMETA_OK);
      check_equal(jinja_cmeta_registry_register(registry, &callable, &error), JINJA_CMETA_OK);
    }
    env = jinja_cmeta_env_create_with_registry(NULL, registry, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("native"), vstr_from_cstr(
        "{{scale(1.25)}}|{{positive(2)}}|{{half(3)}}"), &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2.5|True|1.5");
  }
  it("rejects wrong duplicate missing and overflowing arguments before native execution") {
    JINJA_CMETA_CALLABLE callable;
    check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr("sub"), &subtract, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_registry_register(registry, &callable, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr("positive"), &positive, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_registry_register(registry, &callable, &error), JINJA_CMETA_OK);
    env = jinja_cmeta_env_create_with_registry(NULL, registry, &error);
    check_not_null(env);
    const char *sources[] = {"{{sub(9,left=4)}}", "{{sub(9)}}", "{{sub(9,other=4)}}", "{{sub('9',4)}}", "{{positive(2147483648)}}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      templ = jinja_cmeta_env_compile(env, vstr_from_cstr("native"), vstr_from_cstr(sources[i]), &error);
      check_not_null(templ);
      JINJA_CMETA_STATUS status = jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error);
      check_equal(status, i + 1u == sizeof(sources) / sizeof(sources[0]) ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_RENDER);
      check_null(output);
      check_equal(native_calls, (size_t)0u);
      jinja_cmeta_release(templ);
      templ = NULL;
    }
  }
  it("zeros rejected adapter outputs") {
    cmeta_invokable invalid = CMETA_INVOKABLE_INIT;
    JINJA_CMETA_CALLABLE callable = {.userdata = &invalid};
    check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr("invalid"), &invalid, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_null(callable.invoke);
    check_null(callable.userdata);
  }
  it("rejects unspecified result ownership and asynchronous signatures") {
    cmeta_invokable unknown = CMETA_INVOKABLE_INIT, asynchronous = CMETA_INVOKABLE_INIT;
    JINJA_CMETA_CALLABLE callable;
    check_equal(cmeta_invokable_bind(FunctionMeta(reflected_unknown), scale_adapter, &unknown), CMETA_OK);
    check_equal(cmeta_invokable_bind(FunctionMeta(reflected_async), async_adapter, &asynchronous), CMETA_OK);
    check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr("unknown"), &unknown, &error), JINJA_CMETA_ERR_UNSUPPORTED);
    check_null(callable.invoke);
    check_equal(jinja_cmeta_callable_from_invokable(&callable, vstr_from_cstr("async"), &asynchronous, &error), JINJA_CMETA_ERR_UNSUPPORTED);
    check_null(callable.invoke);
  }

}
