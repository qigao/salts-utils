#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta attributes") {
  it("attr reads exact properties without falling back to item lookup") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% set ns=namespace(x=2) %}{{ns|attr('x')}}|{{ns|attr(name='missing') is undefined}}", "2|True"},
      {"{{{'x':2}|attr('x') is undefined}}|{{[1]|attr('0') is undefined}}|{{'ab'|attr('0') is undefined}}", "True|True|True"},
      {"{% set ns=namespace({'a.b':3,'':4,'名':5,'a\\u0000b':6}) %}{{ns|attr('a.b')}}|{{ns|attr('')}}|{{ns|attr('名')}}|{{ns|attr('a\\u0000b')}}", "3|4|5|6"},
      {"{{range(2,8,2)|attr('start')}}|{{range(2,8,2)|attr(name='stop')}}|{{range(2,8,2)|attr('step')}}", "2|8|2"},
      {"{{none|attr('no') is undefined}}|{{true|attr('no') is undefined}}|{{3|attr('no')|default('missing')}}", "True|True|missing"},
      {"{% set ns=namespace(x='<'|safe) %}{% autoescape true %}{{ns|attr('x')}}|{{(ns|attr('x')) is escaped}}{% endautoescape %}", "<|True"},
      {"{% set child=namespace(x=1) %}{% set ns=namespace(child=child) %}{% set alias=ns|attr('child') %}{% set alias.x=2 %}{{child.x}}", "2"},
      {"{% set ns=namespace(x=1) %}{{ ns|attr('x'|safe) }}|{{ ns|attr('x.y') is undefined }}", "1|True"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("attr rejects invalid names and binding after evaluating arguments") {
    static const char *sources[] = {"{{none|attr}}", "{{none|attr(1)}}", "{{none|attr(none)}}",
      "{{none|attr(missing)}}", "{{none|attr('x','y')}}", "{{none|attr(other='x')}}",
      "{{none|attr('x',name='y')}}", "{{missing|attr('x')}}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{none|attr(other=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{loop([x])|attr('x')}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("attr reads borrowed CMeta fields and validates complete dynamic names") {
    JinjaTestRoot root = {{vstr_from_cstr("age"), 7}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{user|attr(name=user.name)}}|{{user|attr('missing') is undefined}}|{{users|attr('count') is undefined}}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7|True|True");
    free(output);
    output = NULL;
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{{user|attr(user.name)}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7");
    free(output);
    output = NULL;
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{user|attr(user.name)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{none|attr('abc')}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    static const unsigned char invalid[] = {'a', 0xffu};
    root.user.name = vstr_from_buf((const char *)invalid, sizeof(invalid));
    check_equal(jinja_test_render("{{none|attr(user.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
    model.user_fields[1].name = "名";
    root.user.name = vstr_from_cstr("名");
    check_equal(jinja_test_render("{{user|attr(user.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
  }

}

spec("Jinja CMeta collections and runtime: attributes 4") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("renders numeric dot indexes through the existing item lookup runtime") {
    JinjaTestModel model;
    JinjaTestRoot root = {0};
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ [[10,20]].0.1 }}|{{ 'Aé'.1 }}|{{ [7].0x0 }}", &model,
                                  &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "20|é|7");
    free(output);
  }

  it("renders Unicode digits in decimal tails hexadecimal values and numeric dot indexes") {
    JinjaTestModel model;
    JinjaTestRoot root = {0};
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ 1\u0662 + 0x\u0661f }}|{{ -0X_\uff12A }}|{{ [7,9].0x\u0661 }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "43|-42|9");
    free(output);
  }


  it("rejects compound slice execution explicitly instead of treating slices as tuples") {
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ x[:,1] }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
  }

  it("indexes structs sequences and UTF-8 strings with literal and dynamic keys") {
    static const char source[] =
        "{{ user['name'] }}|{{ user['age'] }}|{{ users[0]['name'] }}|"
        "{{ users[-1]['name'] }}|{{ users[active]['age'] }}|{{ user.name[1] }}|"
        "{{ 'A\xc3\xa9\xf0\x9f\x98\x80'[0] }}|{{ 'A\xc3\xa9\xf0\x9f\x98\x80'[1] }}|"
        "{{ 'A\xc3\xa9\xf0\x9f\x98\x80'[-1] }}";
    static const char expected[] = "Ada|37|Ada|Lin|42|d|A|\xc3\xa9|\xf0\x9f\x98\x80";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);

    root.user.name = vstr_from_cstr("name");
    root.user.age = 1;
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ user[user.name] }}|{{ users[user.age]['name'] }}", &model,
                                  &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "name|Lin");
    free(output);
  }

  it("composes item lookup with arithmetic comparisons conditions and loop scope") {
    static const char source[] = "{{ users[1]['age'] + 1 }}|{{ users[1]['name'][0] }}|"
                                 "{% if users[active]['age'] > user.age %}older{% endif %}|"
                                 "{% for item in users %}{{ item['name'] }}{% endfor %}|"
                                 "{{ users[0]['name'] == 'Ada' }}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "43|L|older|AdaLin|True");
    free(output);
  }

  it("returns Undefined for invalid lookup on defined values and errors on Undefined bases") {
    static const char source[] = "<{{ 1[0] }}>|<{{ users[1.0] }}>|<{{ user.name['x'] }}>|"
                                 "<{{ users[9] }}>|<{{ user['missing'] }}>";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<>|<>|<>|<>|<>");
    free(output);

    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ missing[0] }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("owns lookup source and charges only the final provider node") {
    char source[] = "{{ user['name'] }}";
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

    options.max_nodes = 1u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 2u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "Ada");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("rejects malformed slices and over-capacity lookup chains") {
    static const char *const malformed[] = {"{{ users[0 }}", "{{ users[0]] }}",
                                            "{{ users[:::] }}", "{{ users[1:2:3:4] }}"};
    tstr within_limit = jinja_test_item_lookup_chain(31u);
    tstr beyond_limit = jinja_test_item_lookup_chain(32u);
    JINJA_CMETA_TEMPLATE *templ;
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    }

    templ = jinja_cmeta_compile(vstr_from_cstr("{{ users[0:1] }}"), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);

    check_not_null(within_limit);
    check_not_null(beyond_limit);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_buf(within_limit, tstr_len(within_limit)), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_buf(beyond_limit, tstr_len(beyond_limit)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(within_limit);
    tstr_free(beyond_limit);
  }

  it("rejects invalid sequence and UTF-8 metadata during item lookup") {
    static const char invalid_utf8[] = "\x80";
    JinjaTestRoot root = {
        {vstr_from_cstr("Ada"), 37}, true, {NULL, 1u, sizeof(JinjaTestUser), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ users[0] }}"), NULL, &error);
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
    jinja_cmeta_release(templ);

    root.user.name = vstr_from_buf(invalid_utf8, sizeof(invalid_utf8) - 1u);
    root.users = (cmeta_data_collection_view){NULL, 0u, 0u, NULL};
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ user.name[0] }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
    jinja_cmeta_release(templ);
  }

  it("chains postfix attributes after item and grouped expressions") {
    static const char source[] =
        "{{ users[0].name }}|{{ (user).age }}|{{ users[1].name[0] }}|"
        "{% for item in users %}{{ (item).name }}{% endfor %}|<{{ users[0].missing }}>";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Ada|37|L|AdaLin|<>");
    free(output);
  }

  it("propagates Undefined and rejects malformed postfix attributes") {
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 1u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ users[9].name }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ users[0]. }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("owns postfix attribute names and bounds their expression nodes") {
    char source[] = "{{ users[0].name }}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 1u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    tstr within_limit = jinja_test_attribute_lookup_chain(61u);
    tstr beyond_limit = jinja_test_attribute_lookup_chain(62u);
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
    check_equal(output, "Ada");
    free(output);
    jinja_cmeta_release(templ);

    check_not_null(within_limit);
    check_not_null(beyond_limit);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_buf(within_limit, tstr_len(within_limit)), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_buf(beyond_limit, tstr_len(beyond_limit)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(within_limit);
    tstr_free(beyond_limit);
  }
}
