#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: ranges 7") {
  it("defers positional-only call keyword errors until execution") {
    static const char *sources[] = {
        "{{ range(stop=3) }}", "{{ range(3).count(value=1) }}",
        "{{ range(3).index(value=1) }}",
        "{% for x in [1] %}{{ loop.cycle(value=1) }}{% endfor %}",
        "{% for x in [1] %}{{ loop.changed(value=1) }}{% endfor %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("short circuits calls with keyword arguments") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ true or range(stop=3,) }}|"
                                 "{{ false and range(3).count(value=1) }}|"
                                 "{{ true or 1 is eq range(stop=3) }}|"
                                 "{% for x in [1] %}{{ true or loop.changed(value=1) }}:"
                                 "{{ false and loop.cycle(value=1) }}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|True:False");
    free(output);
  }

  it("evaluates all call arguments before rejecting keyword or arity binding") {
    static const char *sources[] = {
        "{{ range(1,2,3,stop=9223372036854775807+1) }}",
        "{{ range('bad',stop=9223372036854775807+1) }}",
        "{{ range(3).count(1,value=9223372036854775807+1) }}",
        "{{ range(3).index(1,value=9223372036854775807+1) }}",
        "{% for x in [1] %}{{ loop.cycle(value=9223372036854775807+1) }}{% endfor %}",
        "{% for x in [1] %}{{ loop.changed(value=9223372036854775807+1) }}{% endfor %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
    }
  }

  it("rejects malformed call keyword ordering") {
    static const char *sources[] = {"{{ range(stop=3, 1) }}", "{{ range(stop=3, stop=4) }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      jinja_cmeta_release(templ);
    }
  }

  it("renders range loops with positive negative and empty directions") {
    static const char source[] = "{% for i in range(4) %}{{ i }}{% endfor %}|"
                                 "{% for i in range(5,-1,-2) %}{{ i }}{% endfor %}|"
                                 "{% for i in range(3,0) %}X{% else %}empty{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "0123|531|empty");
    free(output);
  }

  it("keeps range representation and large indexing independent of list storage") {
    static const char source[] =
        "{{ range(3) }}|{{ range(1,6,2) }}|{{ range(0,9223372036854775807)[-1] }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "range(0, 3)|range(1, 6, 2)|9223372036854775806");
    free(output);
  }

  it("composes range attributes equality membership metadata and integer boundaries") {
    static const struct {
      const char *source;
      const char *expected;
    } cases[] = {
        {"{{ range(1,7,2)[0] }}|{{ range(1,7,2)[-1] }}|{{ range(1,7,2)[3] is undefined }}|"
         "{{ range(1,7,2).start }}|{{ range(1,7,2).stop }}|{{ range(1,7,2).step }}",
         "1|5|True|1|7|2"},
        {"{{ range(0) == range(3,0) }}|{{ range(1,2,1) == range(1,3,2) }}|"
         "{{ range(0,4,2) == range(0,3,2) }}|{{ range(3) == [0,1,2] }}",
         "True|True|True|False"},
        {"{{ not range(0) }}|{{ 3 in range(1,8,2) }}|{{ 2 in range(1,8,2) }}|"
         "{{ 3.0 in range(1,8,2) }}|{{ true in range(3) }}|{{ range(3) is sequence }}|"
         "{{ range(3) is iterable }}|{{ range(3) is mapping }}",
         "True|True|False|True|True|True|True|False"},
        {"{% for i in range(1,6,2) %}[{{ loop.index }}/{{ loop.length }}:{{ loop.previtem }}:"
         "{{ i }}:{{ loop.nextitem }}]{% endfor %}",
         "[1/3::1:3][2/3:1:3:5][3/3:3:5:]"},
        {"{{ range(-9223372036854775808,9223372036854775807)[-1] }}|"
         "{{ range(9223372036854775807,-9223372036854775808,-9223372036854775808)[-1] }}",
         "9223372036854775806|-1"},
        {"{% for r in [range(3)] %}{{ r }}:{{ r[-1] }}{% endfor %}", "range(0, 3):2"},
        {"{% for r in [range(1,6,2)] %}{{ r.start }}:{{ r.stop }}:{{ r.step }}|"
         "{{ r.start + r.step }}{% endfor %}",
         "1:6:2|3"}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      JINJA_CMETA_STATUS status =
          jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error);
      check_equal(status, JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects invalid range calls and oversized iteration") {
    static const char *sources[] = {"{{ range() }}",
                                    "{{ range(0,1,1,1) }}",
                                    "{{ range(0,3,0) }}",
                                    "{{ range(3.0) }}",
                                    "{{ range('3') }}",
                                    "{{ range(2) < range(3) }}",
                                    "{% for range in [7] %}{{ range(3) }}{% endfor %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_STATUS status =
          jinja_test_render(sources[i], &model, &root, NULL, &output, &error);
      check_equal(status, JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    check_equal(jinja_test_render("{% for i in range(9223372036854775807) %}X{% endfor %}", &model,
                                  &root, NULL, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("counts and locates range elements through direct calls and aliases") {
    static const struct {
      const char *source;
      const char *expected;
    } cases[] = {
        {"{{ range(1,8,2).count(3) }}|{{ range(1,8,2).count(2) }}|{{ range(1,8,2).index(5) }}|"
         "{{ range(5,-2,-2).index(1) }}|{{ range(3).count(true) }}|{{ range(3).index(2.0) }}|"
         "{{ range(0).count(0) }}",
         "1|0|2|2|1|2|0"},
        {"{% for r in [range(1,8,2)] %}{{ r.count(3) }}:{{ r.index(5) }}:{{ (r).index(7) }}"
         "{% endfor %}|{{ [range(3)][0].index(2) }}",
         "1:2:3|2"},
        {"{{ range(3).count(user) }}|{{ user in range(3) }}|{{ range(3).count([]) }}|"
         "{{ range(3).count(1.5) }}",
         "0|False|0|0"},
        {"{{ range(-9223372036854775808,9223372036854775807).count(0) }}|"
         "{{ range(0,9223372036854775807).index(9223372036854775806) }}",
         "1|9223372036854775806"}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      JINJA_CMETA_STATUS status =
          jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error);
      check_equal(status, JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("reports range method argument missing element and integer capacity errors") {
    static const char *sources[] = {"{{ range(0).index(0) }}", "{{ range(3).count() }}",
                                    "{{ range(3).index(1,2) }}", "{{ range(3).index('1') }}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_STATUS status =
          jinja_test_render(sources[i], &model, &root, NULL, &output, &error);
      check_equal(status, JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    check_equal(jinja_test_render("{{ range(-9223372036854775808,9223372036854775807).index(0) }}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }
}
