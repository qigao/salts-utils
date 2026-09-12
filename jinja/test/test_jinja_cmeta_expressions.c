#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta collections and runtime: expressions 3") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("selects elif branches and falls through to else") {
    static const char source[] =
        "{% if active %}active{% elif user.age %}aged{% elif user.name %}named{% else %}empty"
        "{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "aged");
    free(output);

    root.user.age = 0;
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "named");
    free(output);

    root.user.name = vstr_from_cstr("");
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "empty");
    free(output);
  }

  it("negates if and elif dotted paths") {
    static const char source[] =
        "{% if not active %}inactive{% else %}active{% endif %}:"
        "{% if active %}active{% elif not user.age %}missing{% else %}aged{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "inactive:missing");
    free(output);
  }

  it("groups dotted paths in conditions") {
    static const char source[] =
        "{% if (active) %}active{% elif (user.age) %}aged{% else %}empty{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "aged");
    free(output);
  }

  it("applies repeated unary not from right to left") {
    static const char source[] = "{% if not not active %}active{% else %}inactive{% endif %}:"
                                 "{% if not (not user.age) %}aged{% else %}empty{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "active:aged");
    free(output);
  }

  it("evaluates true and false condition literals") {
    static const char source[] = "{% if true %}true{% else %}bad{% endif %}:"
                                 "{% if false %}bad{% else %}false{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "true:false");
    free(output);
  }

  it("preserves lookup scope inside boolean condition sections") {
    static const char source[] =
        "{% if false %}bad{% elif true %}{{ user.name }}{% endif %}:"
        "{% for item in users %}{% if true %}{{ item.name }}{% endif %}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "root:AdaLin");
    free(output);
  }

  it("negates grouped boolean condition literals") {
    static const char source[] = "{% if not true %}bad{% else %}true{% endif %}:"
                                 "{% if not(false) %}true{% else %}bad{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "true:true");
    free(output);
  }

  it("evaluates a scalar condition without allocating a section context") {
    static const char source[] = "{% if true %}true{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "true");
    free(output);
  }

  it("renders boolean literals with Jinja text semantics") {
    static const char source[] = "{{ true }}|{{ false }}|{{ not(false) }}|{{ (true) | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|True");
    free(output);
  }

  it("renders CMeta booleans with Jinja text semantics") {
    static const char source[] = "{{ active }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }

  it("charges interpolated boolean literals to the render node budget") {
    static const char source[] = "{{ true }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("renders signed decimal integer literals") {
    static const char source[] = "{{ 0 }}|{{ 42 }}|{{ -7 }}|{{ +8 }}|{{ 9223372036854775807 }}|"
                                 "{{ (-9223372036854775808) }}|{{ (+8) | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "0|42|-7|8|9223372036854775807|-9223372036854775808|8");
    free(output);
  }

  it("renders title-case booleans and based integer literals") {
    static const char source[] =
        "{{ True }}|{{ False }}|{{ 0b1010 }}|{{ 0o17 }}|{{ 0x2a }}|{{ 0Xf_f }}|"
        "{{ -0x10 }}|{{ -0x8000000000000000 }}|{{ 0b_1 }}|{{ 0x_A }}|{{ -0o_7 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|10|15|42|255|-16|-9223372036854775808|1|10|-7");
    free(output);
  }

  it("composes based integers with arithmetic comparisons lookup and conditions") {
    static const char source[] = "{{ 0x20 + 0o10 }}|{{ 0b10 * 0x3 == 0o6 }}|"
                                 "{{ users[0b1].name }}|"
                                 "{% if True and 0x1 %}yes{% endif %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "40|True|Lin|yes");
    free(output);
  }

  it("rejects malformed and overflowing based integers") {
    static const char *const malformed[] = {"{{ 0b2 }}", "{{ 0o8 }}", "{{ 0xg }}", "{{ 0b__1 }}",
                                            "{{ 0x1_ }}"};
    static const char *const overflowing[] = {"{{ 0x8000000000000000 }}",
                                              "{{ -0x8000000000000001 }}"};
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
    for (i = 0u; i < sizeof(overflowing) / sizeof(overflowing[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(overflowing[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    }
  }

  it("uses integer literal truthiness in conditions") {
    static const char source[] = "{% if 0 %}bad{% else %}zero{% endif %}:"
                                 "{% if -1 %}nonzero{% else %}bad{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "zero:nonzero");
    free(output);
  }

  it("applies not to integer literals") {
    static const char source[] = "{{ not 0 }}|{{ not -1 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False");
    free(output);
  }

  it("compares boolean and integer literals with Jinja semantics") {
    static const char source[] =
        "{{ 1 == 1 }}|{{ 1 != 1 }}|{{ 2 > 1 }}|{{ 1 > 1 }}|{{ 2 >= 1 }}|"
        "{{ 1 >= 1 }}|{{ 1 < 2 }}|{{ 1 < 1 }}|{{ 1 <= 2 }}|{{ 1 <= 1 }}|"
        "{{ true == 1 }}|{{ false < 1 }}|{{ true > false }}|{{ false >= 0 }}|"
        "{{ -1 < 0 }}|{{ -9223372036854775808 < 9223372036854775807 }}|{{ -1 < false }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|False|True|True|True|False|True|True|True|True|True|True|"
                        "True|True|True");
    free(output);
  }

  it("compares string literals with all six Jinja comparison operators") {
    static const char source[] = "{{ 'a' == \"a\" }}|{{ 'a' != 'b' }}|{{ 'a' < 'b' }}|"
                                 "{{ 'a' <= 'a' }}|{{ 'b' > 'a' }}|{{ 'b' >= 'b' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|True");
    free(output);
  }

  it("compares decoded and Unicode string literal bytes") {
    static const char source[] = "{{ '\\x61' == 'a' }}|{{ '\\u00e9' == '\xc3\xa9' }}|"
                                 "{{ '\xc3\xa9' < '\xe4\xb8\xad' }}|{{ '\\x00A' < '\\x00B' }}|"
                                 "{{ '\xc3\xa9' == 'e\xcc\x81' }}|{{ not 'a' == 'b' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|False|True");
    free(output);
  }

  it("compares runtime scalar paths with all six operators") {
    static const char source[] =
        "{{ user.age == 37 }}|{{ user.age != 0 }}|{{ user.age < 38 }}|"
        "{{ user.age <= 37 }}|{{ user.age > 36 }}|{{ user.age >= 37 }}|"
        "{{ active == true }}|{{ user.name == 'Ada' }}|{{ user.name < 'Bee' }}|"
        "{{ user.age == user.age }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|True|True|True|True|True");
    free(output);
  }

  it("compares signed unsigned and boolean runtime scalars exactly") {
    static const char source[] =
        "{{ signed_value < unsigned_value }}|{{ unsigned_value > signed_value }}|"
        "{{ unsigned_value == boolean_value }}|{{ unsigned_value > 0 }}";
    JinjaTestIntegerRoot root = {-1, SIZE_MAX, true};
    JinjaTestIntegerModel model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_integer_model_init(&model);
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True|True|False|True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("resolves runtime comparisons in loop and parent scopes") {
    static const char source[] =
        "{% if user.age >= 37 %}adult{% else %}minor{% endif %}|{{ not user.age == 0 }}|"
        "{% for item in users %}{% if item.age >= 40 %}{{ item.name }}{% endif %}"
        "{% if user.age == 37 %}!{% endif %}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, false, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "adult|True|!Lin!");
    free(output);
  }

  it("uses Jinja equality for undefined and unlike scalar values") {
    static const char source[] = "{{ missing == 1 }}|{{ missing != 1 }}|{{ missing == absent }}|"
                                 "{{ missing != absent }}|{{ '1' == 1 }}|{{ '1' != 1 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|True|False|False|True");
    free(output);
  }

  it("copies runtime comparison paths and strings into the compiled template") {
    char source[] = "{{ user.name == 'Ada' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("reports unlike scalar ordering as a render error") {
    static const char *const sources[] = {"{{ '1' < 1 }}", "{{ user.name < user.age }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t index;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      char *output = NULL;

      check_equal(jinja_test_render(sources[index], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("charges runtime comparison results to the render node budget") {
    static const char source[] = "{{ user.age == 37 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("uses comparison precedence and folded results in conditions") {
    static const char source[] = "{{ not 1 == 2 }}|{{ not (1 == 2) }}|{{ (not 1) == false }}|"
                                 "{% if 2 >= 1 %}yes{% else %}no{% endif %}|"
                                 "{% if true < false %}bad{% else %}ok{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|yes|ok");
    free(output);
  }

  it("returns the selected operands from logical expressions") {
    static const char source[] = "{{ 0 or 5 }}|{{ 'left' and 'right' }}|{{ '' or user.name }}|"
                                 "{{ user.name and user.age }}|{{ active and true }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "5|right|Ada|37|True");
    free(output);
  }

  it("uses Jinja logical precedence and grouping") {
    static const char source[] = "{{ false or true and false }}|"
                                 "{{ (false or true) and true }}|"
                                 "{{ not (false or true) }}|"
                                 "{{ not false and false or true }}|"
                                 "{{ not not user.name and 'yes' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|False|True|yes");
    free(output);
  }

  it("short circuits logical branches and preserves undefined operands") {
    static const char source[] = "{{ false and ('1' < 1) }}|{{ true or ('1' < 1) }}|"
                                 "{{ missing or 'fallback' }}|{{ missing and 'x' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|fallback|");
    free(output);
  }

  it("fails when a logical branch must evaluate an invalid comparison") {
    static const char *const sources[] = {"{{ true and ('1' < 1) }}", "{{ false or ('1' < 1) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t index;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      char *output = NULL;

      check_equal(jinja_test_render(sources[index], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("resolves logical operands in loop and parent scopes and truthifies conditions once") {
    static const char source[] =
        "{% for item in users %}{% if item.age >= 40 and user.age == 37 %}"
        "{{ item.name }}{% endif %}{% endfor %}|{% if users or active %}once{% endif %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, false, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Lin|once");
    free(output);
  }

  it("charges one logical result to the render node budget") {
    static const char source[] = "{{ user.name and user.age }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 2u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "37");
    free(output);

    options.max_nodes = 1u;
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("copies logical paths and strings into the compiled template") {
    char source[] = "{{ missing or user.name or 'fallback' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "Ada");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("bounds each logical expression tree") {
    JINJA_CMETA_TEMPLATE *templ;
    tstr source = jinja_test_logical_operands(32u);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_logical_operands(33u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("reports leading and trailing logical operators as syntax errors") {
    static const char *const sources[] = {"{{ and true }}", "{{ true or }}"};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[index]), NULL, &error);

      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("evaluates chained comparisons left to right") {
    static const char source[] = "{{ 1 < 2 < 3 }}|{{ 3 > 2 > 1 }}|{{ 1 < 2 > 3 }}|"
                                 "{{ 1 == true == 1 }}|{{ 'a' < 'b' < 'c' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|False|True|True");
    free(output);
  }

  it("supports runtime chains in interpolation and conditions") {
    static const char source[] =
        "{{ 36 < user.age <= 37 }}|{% if 30 < user.age < 40 %}adult{% else %}other{% endif %}|"
        "{{ missing == absent == missing }}|"
        "{% for item in users %}{% if 30 < item.age < 40 %}{{ item.name }}{% endif %}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|adult|True|Ada");
    free(output);
  }

  it("copies chain operands and charges one final render node") {
    char source[] = "{{ 'A' < user.name < 'Z' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);

    options.max_nodes = 2u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);

    options.max_nodes = 1u;
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    jinja_cmeta_release(templ);
  }

  it("short circuits a chain before an invalid later operand") {
    static const char skipped[] = "{{ 3 < 2 < ('1' < 1) }}";
    static const char evaluated[] = "{{ 1 < 2 < ('1' < 1) }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(skipped, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False");
    free(output);

    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(evaluated, &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("evaluates parenthesized comparisons as nested operands") {
    static const char source[] = "{{ (user.age == 37) == true }}|"
                                 "{{ (missing == absent) == true }}|"
                                 "{{ (1 < 2) == (3 > 2) }}|{{ (1 < 2) < (2 < 1) }}|"
                                 "{{ not (1 < 2 < 3) }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|False|False");
    free(output);
  }

  it("bounds comparison chains and rejects an incomplete tail") {
    JINJA_CMETA_TEMPLATE *templ;
    tstr source = jinja_test_comparison_operands(16u);

    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_comparison_operands(64u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ 1 < 2 < }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("evaluates string membership with comparison precedence") {
    static const char source[] = "{{ 'da' in user.name }}|{{ 'x' not in user.name }}|"
                                 "{{ not 'x' in user.name }}|{{ 'A' in user.name == 'Ada' }}|"
                                 "{{ '\u00e9' in 'caf\u00e9' }}|"
                                 "{{ 'A' in user.name and 'yes' }}|"
                                 "{% for item in users %}{% if item.name in user.name %}"
                                 "{{ item.name }}{% endif %}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|yes|Ada");
    free(output);
  }

  it("checks scalar membership in borrowed CMeta sequences") {
    const int ages[] = {21, 37, 42};
    const vstr names[] = {vstr_from_cstr("Ada"), vstr_from_cstr("Lin")};
    JinjaTestMembershipRoot root = {{ages, 3u, sizeof(ages[0]), &cmeta_data_int},
                                    {names, 2u, sizeof(names[0]), jinja_cmeta_vstr_data()},
                                    {NULL, 0u, 0u, NULL}};
    JinjaTestMembershipModel model;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(
        vstr_from_cstr("{{ 37 in ages }}|{{ 9 not in ages }}|{{ 'Ada' in names }}|"
                       "{{ 'Max' in names }}|{{ 1 in empty }}"), NULL,
        &error);
    char *output = NULL;

    jinja_test_membership_model_init(&model);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True|True|True|False|False");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("copies membership operands and bounds sequence scans") {
    const int ages[] = {21, 37, 42};
    JinjaTestMembershipRoot root = {
        {ages, 3u, sizeof(ages[0]), &cmeta_data_int}, {NULL, 0u, 0u, NULL}, {NULL, 0u, 0u, NULL}};
    JinjaTestMembershipModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char source[] = "{{ 37 in ages }}";
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    char *output = NULL;

    jinja_test_membership_model_init(&model);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    options.max_nodes = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    root.ages.count = 2u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("rejects invalid membership operand types and sequence metadata") {
    JinjaTestRoot string_root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel string_model;
    JinjaTestMembershipRoot sequence_root = {
        {NULL, 1u, sizeof(int), &cmeta_data_int}, {NULL, 0u, 0u, NULL}, {NULL, 0u, 0u, NULL}};
    JinjaTestMembershipModel sequence_model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&string_model);
    string_root.users.element = &string_model.user_desc;
    check_equal(jinja_test_render("{{ 1 in user.name }}", &string_model, &string_root, NULL,
                                  &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);

    jinja_test_membership_model_init(&sequence_model);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ 1 in ages }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &sequence_model.desc, &sequence_root, NULL,
                                          &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
    jinja_cmeta_release(templ);
  }
}
