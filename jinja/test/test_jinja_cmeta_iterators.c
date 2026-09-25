#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: iterators 8") {
  it("items yields unique dictionary entries as tuples") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pair in {'a':1,'b':2,'a':3}|items %}{{ pair[0] }}={{ pair[1] }};{% endfor %}|"
        "{{ {}|items|list }}|{{ missing|items|list }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a=3;b=2;|[]|[]");
    free(output);
  }

  it("items preserves generator truthiness without consuming invalid inputs") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{{ {}|items is iterable }}|{{ {}|items is sequence }}|{{ {}|items is mapping }}|"
        "{% if {}|items %}truthy{% endif %}|{{ missing|items is defined }}|"
        "{{ 1|items is iterable }}|{% if 1|items %}truthy{% endif %}|"
        "{{ ({'a':1}|items)[0] is undefined }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|False|truthy|True|True|truthy|True");
    free(output);
  }

  it("items shares a one-shot cursor through collection values") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1,'b':2}|items] %}{{ pairs|first|safe }}|{{ pairs|first|safe }}|"
        "{{ pairs|list }}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "('a', 1)|('b', 2)|[]");
    free(output);
  }

  it("items separates empty loop handling from generator truthiness") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% if {}|items %}truthy{% endif %}|{% for pair in {}|items %}unexpected"
        "{% else %}empty{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "truthy|empty");
    free(output);
  }

  it("items equality uses identity without consuming the source") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1}|items] %}{{ pairs == pairs }}|{{ pairs != pairs }}|"
        "{{ pairs == ({}|items) }}|{{ pairs == 1 }}|{{ pairs|first|safe }}{% endfor %}|"
        "{% for pairs in [1|items] %}{{ pairs == pairs }}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|False|False|('a', 1)|True");
    free(output);
  }

  it("items membership consumes through the first equal tuple") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1,'b':2,'c':3}|items] %}{{ ('b',2) in pairs }}|"
        "{{ pairs|first|safe }}|{{ ('b',2) not in pairs }}|{{ pairs|list }}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|('c', 3)|True|[]");
    free(output);
  }

  it("items membership misses exhaust the source without coercing lists to tuples") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1}|items] %}{{ ['a',1] in pairs }}|{{ pairs|list }}"
        "{% endfor %}|{{ 1 in (missing|items) }}|{{ ('a',1) in ({'a':2}|items) }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|[]|False|False");
    free(output);
  }

  it("items identity survives collection membership and dictionary keys") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{}|items] %}{{ pairs in [pairs] }}|{{ pairs in range(3) }}|"
        "{{ pairs in {pairs:1} }}|{{ [pairs] == [pairs] }}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|True");
    free(output);
  }

  it("items membership in borrowed sequences compares identities without scalar conversion") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users = (cmeta_data_collection_view){&root.user, 1u, sizeof(root.user), &model.user_desc};
    check_equal(jinja_test_render(
        "{% for pairs in [{}|items] %}{{ pairs in users }}|{{ pairs not in users }}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
  }

  it("items rejects ordering and propagates membership consumption errors") {
    static const char *sources[] = {
        "{{ ({}|items) < ({}|items) }}", "{{ ({}|items) <= ({}|items) }}",
        "{{ ({}|items) > ({}|items) }}", "{{ ({}|items) >= ({}|items) }}",
        "{{ 1 in (1|items) }}", "{{ 1 not in (none|items) }}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("items conditions evaluate membership once and changed compares iterator identity") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1,'b':2}|items] %}{{ loop.changed(pairs) }}|"
        "{% if ('a',1) in pairs %}yes{% else %}no{% endif %}|{{ pairs|first|safe }}|"
        "{{ loop.changed(pairs) }}{% endfor %}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "True|yes|('b', 2)|False");
    free(output);
  }

  it("items loop metadata preserves cached neighbors") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pair in {'a':1,'b':2}|items %}{{ loop.index }}/{{ loop.length }}:"
        "{{ loop.previtem|default('-')|safe }}:{{ pair|safe }}:"
        "{{ loop.nextitem|default('-')|safe }};{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1/2:-:('a', 1):('b', 2);2/2:('a', 1):('b', 2):-;");
    free(output);
  }

  it("outer iterator aliases preserve consumption timing inside nested loops") {
    static const struct { const char *source; const char *expected; } cases[] = {
        {"{% for pairs in [{'a':1,'b':2,'c':3}|items] %}{% for pair in pairs %}"
         "{{ pair[0] }}:{{ pairs|first|safe }};{% else %}empty{% endfor %}|"
         "{{ pairs|list }}{% endfor %}", "a:('b', 2);c:;|[]"},
        {"{% for pairs in [{'a':1,'b':2,'c':3}|items] %}{% for pair in pairs %}"
         "{{ pair[0] }}:{{ loop.nextitem|safe }}:{{ pairs|first|safe }};{% endfor %}"
         "{% endfor %}", "a:('b', 2):('c', 3);b::;"},
        {"{% for pairs in [{'a':1,'b':2}|items] %}{% for pair in pairs %}"
         "{{ loop.length }}:{{ pair[0] }}:{{ pairs|list }};{% endfor %}{% endfor %}",
         "2:a:[];2:b:[];"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("nested loop aliases shadow and restore without leaking out of their scope") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for outer in [10] %}{% for inner in [20] %}{{ outer }}:{{ inner }}:"
        "{{ outer + inner }}{% endfor %}|{{ outer }}{% endfor %}|"
        "{% for x in [1] %}{{ x }}:{% for x in [2] %}{{ x }}{% endfor %}:{{ x }}"
        "{% endfor %}|{{ x|default('-') }}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "10:20:30|10|1:2:1|-");
    free(output);
  }

  it("loop alias names do not replace explicitly selected struct fields") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for person in [user] %}{% for name in ['shadow'] %}{{ person.name }}:"
        "{{ user.name }}:{% if person.name == 'Ada' %}yes{% endif %}{% endfor %}"
        "{% endfor %}|{% for name in ['shadow'] %}{% for person in [user] %}"
        "{{ person.name|length }}:{{ name }}{% endfor %}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Ada:Ada:yes|3:shadow");
    free(output);
  }

  it("empty loop else uses enclosing bindings instead of an absent iteration") {
    static const struct { const char *source; const char *expected; } cases[] = {
        {"{% for outer in [10] %}{% for inner in [] %}bad{% else %}"
         "{{ outer }}:{{ inner|default(99) }}{% endfor %}{% endfor %}", "10:99"},
        {"{% for user in [] %}bad{% else %}{{ user.name }}{% endfor %}", "Ada"},
        {"{% for outer in [10] %}{% for outer in [] %}bad{% else %}"
         "{% if outer %}{{ outer + 1 }}{% endif %}:{{ loop.index }}{% endfor %}{% endfor %}",
         "11:1"}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("compiled loop aliases survive overwriting the caller source") {
    char source[] = "{% for outer in [10] %}{% for inner in [20] %}"
                    "{{ outer }}:{{ inner }}{% endfor %}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "10:20");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("nested collection iteration preserves the consuming outer scope") {
    static const struct { const char *source; const char *expected; } cases[] = {
        {"{% for outer in [[10]] %}{% for inner in outer %}{{ outer|length }}:"
         "{{ inner }}{% endfor %}{% endfor %}", "1:10"},
        {"{% for outer in [(10,)] %}{% for inner in outer %}{{ outer|length }}:"
         "{{ inner }}{% endfor %}{% endfor %}", "1:10"},
        {"{% for outer in [{'a':10}] %}{% for inner in outer %}{{ outer|length }}:"
         "{{ inner }}{% endfor %}{% endfor %}", "1:a"}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }
}

spec("Jinja CMeta slice filter") {
  it("validates borrowed Unicode before producing even a negative slice result") {
    static const unsigned char invalid[] = {'a', 0xc0u, 0xafu};
    static const char *sources[] = {
      "P{{user.name|slice(2)|list}}Q", "P{{user.name|slice(-1)|list}}Q"
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_buf((const char *)invalid, sizeof(invalid)), 0}, false,
        {NULL, 0u, 0u, NULL}};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_not_null(templ);
      JinjaTestByteSink sink = {{0}, 0u};
      check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                  JINJA_CMETA_ERR_METADATA);
      check_equal(sink.length, (size_t)1u);
      check_equal(sink.bytes[0], (unsigned char)'P');
      jinja_cmeta_release(templ);
    }
  }

  it("preserves embedded NUL and multibyte characters in borrowed slice columns") {
    static const unsigned char input[] = {'a', 0u, 0xe4u, 0xb8u, 0xadu};
    static const unsigned char expected[] = {'P', 'a', 0u, 0xe4u, 0xb8u, 0xadu, 'Q'};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_buf((const char *)input, sizeof(input)), 2}, false,
        {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "P{% for column in user.name|slice(user.age) %}{{column|join}}{% endfor %}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_OK);
    check_equal(sink.length, sizeof(expected));
    check_equal(sink.bytes, expected, sizeof(expected));
    jinja_cmeta_release(templ);
  }

  it("stops slice column output when its sink is full") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% for column in []|slice(40) %}x{% endfor %}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, (size_t)JINJA_TEST_BYTE_SINK_CAPACITY);
    for (size_t i = 0u; i < sink.length; ++i) check_equal(sink.bytes[i], (unsigned char)'x');
    jinja_cmeta_release(templ);
  }

  it("bounds nested slice materialization at the exact iterator depth") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set g=range(4)|slice(2) %}{% set h=g|slice(2) %}{{h|list}}"), NULL, &error);
    check_not_null(templ);
    char *output = NULL;
    options.max_render_depth = 1u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_render_depth = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "[[[0, 1]], [[2, 3]]]");
    free(output);
    jinja_cmeta_release(templ);
  }
  it("rejects invalid slice consumption and parameter binding") {
    static const char *sources[] = {
      "{{42|slice(1000000)|list}}", "{{42|slice(1000000)|first}}",
      "{{[]|slice(0)|list}}", "{{[]|slice(2.0)|list}}", "{{42|slice(-1)|list}}",
      "{{[]|slice|list}}", "{{[]|slice(2,unknown=0)|list}}"
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
      free(output);
    }
  }

  it("counts empty slice columns against the configured node limit") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{{[]|slice(3)|list}}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 3u;
    check_equal(jinja_test_render("{{[]|slice(3)|list}}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "[[], [], []]");
    free(output);
  }
  it("slice_negative_consumes") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set g={'a':1,'b':2}|items %}{{g|slice(-1)|list}}|{{g|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[]|[]");
    free(output);
  }
  it("slice_nested_batch") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{range(5)|slice(2)|batch(1)|list}}|{{range(4)|batch(2)|slice(2)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[[0, 1, 2]], [[3, 4]]]|[[[0, 1]], [[2, 3]]]");
    free(output);
  }
  it("slice_expansion_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set n=namespace(x=1) %}{% set rows=[]|slice(*[2],**{'fill_with':n})|list %}{% set n.x=7 %}{{rows[0][0].x}}|{{rows[1][0].x}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7|7");
    free(output);
  }
  it("slice_columns") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{range(7)|slice(slices=3)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[0, 1, 2], [3, 4], [5, 6]]");
    free(output);
  }
  it("slice_fill_divisible") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{range(4)|slice(2,0)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[0, 1, 0], [2, 3, 0]]");
    free(output);
  }
  it("slice_empty_columns") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{[]|slice(3)|list}}|{{[]|slice(2,'x')|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[], [], []]|[['x'], ['x']]");
    free(output);
  }
  it("slice_unicode") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{'中a🙂'|slice(2)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[['中', 'a'], ['🙂']]");
    free(output);
  }
  it("slice_alias_materialization") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set g={'a':1,'b':2,'c':3}|items %}{% set s=g|slice(2) %}{{s|first}}|{{g|list}}|{{s|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[('a', 1), ('b', 2)]|[]|[[('c', 3)]]");
    free(output);
  }
  it("slice_lazy_invalid") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set s=42|slice(0) %}{{s is iterable}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
}

spec("Jinja CMeta batch") {
  it("admits nested batch consumption exactly at the render depth limit") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set g=range(4)|batch(2) %}{% set h=g|batch(1) %}{{h|list}}"), NULL, &error);
    check_not_null(templ);
    char *output = NULL;
    options.max_render_depth = 1u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_render_depth = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "[[[0, 1]], [[2, 3]]]");
    free(output);
    jinja_cmeta_release(templ);
  }
  it("stops batch rendering when the byte sink rejects a row") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% for row in range(100)|batch(2) %}x{% endfor %}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, (size_t)JINJA_TEST_BYTE_SINK_CAPACITY);
    for (size_t i = 0u; i < sink.length; ++i) check_equal(sink.bytes[i], (unsigned char)'x');
    jinja_cmeta_release(templ);
  }

  it("rejects excessive batch padding while retaining only the streamed prefix") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_nodes = 8u;
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "P{{[1]|batch(9,0)|list}}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_equal(sink.length, (size_t)1u);
    check_equal(sink.bytes[0], (unsigned char)'P');
    char *output = NULL;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 9u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "P[[1, 0, 0, 0, 0, 0, 0, 0, 0]]Q");
    free(output);
    jinja_cmeta_release(templ);
  }
  it("batch_loop_neighbors") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% for row in range(5)|batch(2) %}{{loop.index}}:{{row}}:{{loop.previtem|default('-')}}:{{loop.nextitem|default('-')}}:{{loop.last}}:{{loop.length}};{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:[0, 1]:-:[2, 3]:False:3;2:[2, 3]:[0, 1]:[4]:False:3;3:[4]:[2, 3]:-:True:3;");
    free(output);
  }
  it("batch_loop_alias_after_peek") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set g=range(5)|batch(2) %}{% for row in g %}{{row}}/{{loop.nextitem}}/{{g|list}};{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[0, 1]/[2, 3]/[[4]];[2, 3]//[];");
    free(output);
  }
  it("rejects consumed invalid inputs and invalid padding without publishing output") {
    static const char *sources[] = {
      "{{42|batch(2)|list}}", "{{[1]|batch(2.0,0)|list}}",
      "{{[1]|batch('x',0)|first}}", "{{[1]|batch(2,unknown=0)|list}}"
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
      free(output);
    }
  }
  it("batch_nested") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{range(5)|batch(2)|batch(2)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[[0, 1], [2, 3]], [[4]]]");
    free(output);
  }
  it("batch_alias") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set g=range(5)|batch(2) %}{% set h=g %}{{h|first}}|{{g|list}}|{{h|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[0, 1]|[[2, 3], [4]]|[]");
    free(output);
  }
  it("batch_fill_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set n=namespace(x=1) %}{% set row=[0]|batch(3,n)|first %}{% set n.x=9 %}{{row[1].x}}|{{row[1] == row[2]}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "9|True");
    free(output);
  }
  it("batch_empty_invalid_width") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{[]|batch('x',0)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[]");
    free(output);
  }
  it("does not reserve unused padding for nonpositive widths") {
    static const char *sources[] = {
      "{% for i in range(100) %}{% set x=[1]|batch(-1,0)|first %}{% endfor %}ok",
      "{% for i in range(100) %}{% set x=[1]|batch(0,0)|list %}{% endfor %}ok"
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, "ok");
      free(output);
    }
  }
  it("repeats empty batches without charging the configured maximum per iterator") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% for i in range(100) %}{% set x=[]|batch(2)|list %}{% endfor %}ok",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "ok");
    free(output);
  }
  it("retains short negative-width rows by their actual input bound") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% for i in range(100) %}{% set x=[1]|batch(-1)|first %}{% endfor %}ok",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "ok");
    free(output);
  }
  it("batch_groups_and_fills") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{[1,2,3,4,5]|batch(linecount=2,fill_with=0)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[1, 2], [3, 4], [5, 0]]");
    free(output);
  }

  it("batch_unicode") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{'中a🙂'|batch(2)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[['中', 'a'], ['🙂']]");
    free(output);
  }

  it("batch_zero_negative") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{[1,2]|batch(0)|list}}|{{[1,2]|batch(-1)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[], [1, 2]]|[[1, 2]]");
    free(output);
  }

  it("batch_lazy_invalid") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set g=42|batch(2) %}{{g is iterable}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }

  it("batch_lookahead") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set it={'a':1,'b':2,'c':3,'d':4}|items %}{% set g=it|batch(2) %}{{g|first}}|{{it|list}}|{{g|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[('a', 1), ('b', 2)]|[('d', 4)]|[[('c', 3)]]");
    free(output);
  }

  it("batch_float_linecount") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{[1,2,3]|batch(2.0)|list}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[1, 2], [3]]");
    free(output);
  }
}
