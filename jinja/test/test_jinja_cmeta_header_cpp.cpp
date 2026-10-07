#include "jinja_cmeta.h"
#include "jinja_cmeta_reflection.h"
#include "tinytest.hpp"
#include <cmeta/data_reflect.h>
#include <cstdlib>

struct JinjaCppLeaf { int count; };
struct JinjaCppRoot { JinjaCppLeaf child; int private_value; };
cmeta_reflect_data(JinjaCppLeaf, "test.jinja.cpp.Leaf",
    cmeta_field(int, count)
);
cmeta_reflect_data(JinjaCppRoot, "test.jinja.cpp.Root",
    cmeta_data_field(JinjaCppLeaf, child, cmeta_reflected_data(JinjaCppLeaf),
        cmeta_reflected_storage(JinjaCppLeaf))
);

spec("Jinja CMeta C++ header") {
  it("renders a nested read-only projection of existing C++ records") {
    const JinjaCppRoot root = {{7}, 99};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{{child.count}}|{{child|attr('count')}}|{{private_value is undefined}}"),
        nullptr, &error);
    check_not_null(templ);
    char *output = nullptr;
    check_equal(jinja_cmeta_render_string(templ, cmeta_reflected_data(JinjaCppRoot),
        &root, nullptr, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7|7|True");
    std::free(output);
    jinja_cmeta_release(templ);
    check_equal(root.child.count, 7);
    check_equal(root.private_value, 99);
  }

  it("uses an owned environment through C linkage") {
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    JINJA_CMETA_ENV *env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_compile(env, vstr_from_cstr("page"),
        vstr_from_cstr("{{ 1 + 2 }}"), &error);
    check_not_null(templ);
    char *output = nullptr;
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, nullptr,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "3");
    std::free(output);
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
  }

  it("compiles and releases a template through C linkage") {
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.variable_start_string = vstr_from_cstr("[[");
    options.variable_end_string = vstr_from_cstr("]]");
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("Hello [[ name ]]"), &options, &error);

    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
  }

  it("passes independent traversal budgets through the public C ABI") {
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    vstr root = vstr_from_cstr("");
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{[1]==[1]}}"), nullptr, &error);
    check_not_null(templ);
    char *output = nullptr;
    options.max_value_visits = 1u;
    options.max_value_depth = 2u;
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_value_visits = 2u;
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    std::free(output);
    jinja_cmeta_release(templ);
  }
}
