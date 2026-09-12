#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: loops 4") {
  it("renders CMeta structs, conditionals, and sequences") {
    static const char source[] = "Hello {{ user.name }}! "
                                 "{% if active %}active{% else %}inactive{% endif %} "
                                 "{% for item in users %}[{{ item.name }}:{{ item.age }}]"
                                 "{% else %}empty{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_not_null(output);
    check_equal(output, "Hello Ada! active [Ada:37][Lin:42]");
    free(output);
  }

  it("renders false and empty sequence branches") {
    static const char source[] =
        "{% if active %}active{% else %}inactive{% endif %}:"
        "{% for item in users %}{{ item.name }}{% else %}empty{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "inactive:empty");
    free(output);
  }

  it("iterates supported logical and conditional sequence expressions") {
    static const char source[] =
        "{% for item in users or users %}[{{ item.name }}]{% else %}empty{% endfor %}:"
        "{% for item in (users if active else users) %}{{ item.name }}{% else %}empty{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[Ada][Lin]:AdaLin");
    free(output);

    root.users.data = NULL;
    root.users.count = 0u;
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "empty:empty");
    free(output);
  }

  it("reports malformed for iterable expressions as syntax errors") {
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(
        vstr_from_cstr("{% for item in users or %}{{ item.name }}{% endfor %}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("renders core loop metadata for borrowed sequences") {
    static const char source[] =
        "{% for item in users %}[{{ loop.index0 }},{{ loop.index }},{{ loop.revindex0 }},"
        "{{ loop.revindex }},{{ loop.first }},{{ loop.last }},{{ loop.length }},"
        "{{ loop.depth0 }},{{ loop.depth }}:{{ item.name }}]{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[0,1,1,2,True,False,2,0,1:Ada][1,2,0,1,False,True,2,0,1:Lin]");
    free(output);
  }

  it("uses loop metadata in native expressions and collection loops") {
    static const char source[] = "{% for item in ['a', 'b'] %}{{ loop.index + 10 }}"
                                 "{% if loop.first or loop.last %}!{% endif %}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "11!12!");
    free(output);
  }

  it("resolves nested loop metadata from the nearest loop") {
    static const char source[] =
        "{% for outer in [1, 2] %}O{{ loop.index }}:"
        "{% for inner in ['a', 'b'] %}{{ loop.index }}{{ inner }}{% endfor %};{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "O1:1a2b;O2:1a2b;");
    free(output);
  }

  it("uses unique dict length and leaves unknown loop properties undefined") {
    static const char source[] =
        "{% for key in {'a': 1, 'a': 2, 'b': 3} %}"
        "{{ loop.index }}/{{ loop.length }}:{{ key }}:<{{ loop.missing }}>{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1/2:a:<>2/2:b:<>");
    free(output);
  }

  it("exposes guarded neighbor struct attributes and boundary undefined") {
    static const char source[] =
        "{% for item in users %}[{% if loop.previtem is defined %}{{ loop.previtem.name }}"
        "{% endif %}|{{ item.name }}|{% if loop.nextitem is defined %}{{ loop.nextitem.name }}"
        "{% endif %}|{{ loop.previtem is undefined }}|{{ loop.nextitem is undefined }}]"
        "{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[|Ada|Lin|True|False][Ada|Lin||False|True]");
    free(output);
  }

  it("rejects attribute access through a boundary neighbor undefined") {
    static const char source[] = "{% for item in users %}{{ loop.previtem.name }}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {users, 1u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("selects neighbor items from list tuple and unique dict iteration") {
    static const char source[] =
        "{% for item in [10, 20, 30] %}[{{ loop.previtem }}|{{ item }}|{{ loop.nextitem }}]"
        "{% endfor %}|{% for item in ('a', 'b') %}[{{ loop.previtem }}|{{ item }}|"
        "{{ loop.nextitem }}]{% endfor %}|{% for key in {'a': 1, 'a': 2, 'b': 3} %}"
        "[{{ loop.previtem }}|{{ key }}|{{ loop.nextitem }}]{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[|10|20][10|20|30][20|30|]|[|a|b][a|b|]|[|a|b][a|b|]");
    free(output);
  }

  it("composes neighbor items in native expressions") {
    static const char source[] =
        "{% for item in [10, 20, 30] %}{% if loop.previtem is defined and loop.nextitem is "
        "defined %}{{ loop.previtem + 0 }}/{{ item }}/{{ item + 0 }}/{{ loop.nextitem + 0 }}="
        "{{ loop.previtem + item + loop.nextitem }}{% else %}_"
        "{{ loop.previtem is undefined }}{{ loop.nextitem is undefined }}_{% endif %}"
        "{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "_TrueFalse_10/20/20/30=60_FalseTrue_");
    free(output);
  }

  it("uses nearest neighbor items in nested loops and restores the outer loop") {
    static const char source[] =
        "{% for outer in [1, 2] %}O{{ outer }}({% for inner in ['a', 'b'] %}"
        "{{ loop.previtem }}>{{ inner }}>{{ loop.nextitem }};{% endfor %})"
        "[{{ loop.previtem }}>{{ outer }}>{{ loop.nextitem }}]{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "O1(>a>b;a>b>;)[>1>2]O2(>a>b;a>b>;)[1>2>]");
    free(output);
  }

  it("cycles positional values by the nearest loop index") {
    static const char source[] = "{% for item in [1, 2, 3] %}[{{ loop.cycle('odd', 'even') }}:"
                                 "{{ loop.cycle(item, item + 10,) }}]{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[odd:1][even:12][odd:3]");
    free(output);
  }

  it("reports an empty loop cycle as a render error") {
    static const char source[] = "{% for item in [1] %}{{ loop.cycle() }}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("evaluates every loop cycle argument before selecting a value") {
    static const char source[] = "{% for item in [1] %}{{ loop.cycle(item, 1 // 0) }}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("tracks loop changed values and shares state between call sites") {
    static const char source[] =
        "{% for item in [1, 1, 2, 2] %}{{ loop.changed(item, item > 1) }}{% endfor %}|"
        "{% for item in [1, 2] %}{{ loop.changed(item) }}:{{ loop.changed(item) }};{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "TrueFalseTrueFalse|True:False;True:False;");
    free(output);
  }

  it("evaluates a changed condition once across its else branch") {
    static const char source[] =
        "{% for item in [1, 1, 2] %}{% if loop.changed(item) %}Y{% else %}N{% endif %}"
        "{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "YNY");
    free(output);
  }

  it("keeps changed branch decisions across elif and nested body effects") {
    static const struct {
      const char *source;
      const char *expected;
    } cases[] = {{"{% for item in [1, 1, 2] %}{% if false %}X{% elif loop.changed(item) %}Y"
                  "{% else %}N{% endif %}{% endfor %}",
                  "YNY"},
                 {"{% for item in [1, 1, 2] %}{% if loop.changed(item) %}Y{{ loop.changed(9) }}"
                  "{% elif loop.changed(8) %}E{% else %}N{% endif %}{% endfor %}",
                  "YTrueYTrueYTrue"},
                 {"{% for outer in [1, 1] %}{% if loop.changed(outer) %}O"
                  "{% for inner in [1, 1, 2] %}{% if loop.changed(inner) %}Y{% else %}N"
                  "{% endif %}{% endfor %}{% else %}N{% endif %}{% endfor %}",
                  "OYNYN"}};
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

  it("isolates nested loop changed state and restores the outer state") {
    static const char source[] =
        "{% for outer in [1, 1] %}O{{ loop.changed(outer) }}:"
        "{% for inner in [1, 1, 2] %}{{ loop.changed(inner) }}{% endfor %}:"
        "{{ loop.changed(outer) }};{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "OTrue:TrueFalseTrue:False;OFalse:TrueFalseTrue:False;");
    free(output);
  }

  it("compares collection arguments and permits an empty changed tuple") {
    static const char source[] =
        "{% for item in [[1], [1], [2]] %}{{ loop.changed(item) }}{% endfor %}|"
        "{% for item in [1, 2] %}{{ loop.changed() }}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "TrueFalseTrue|TrueFalse");
    free(output);
  }

  it("evaluates collection calls before truthiness comparison and representation") {
    static const struct {
      const char *source;
      const char *expected;
    } cases[] = {
        {"{% for item in [1] %}{% if [loop.changed(item)] %}Y{% endif %}"
         "{{ loop.changed(item) }}{% endfor %}",
         "YFalse"},
        {"{% for item in [1] %}{{ {'a': loop.changed(item)} | safe }}:"
         "{{ loop.changed(item) }}{% endfor %}",
         "{'a': True}:False"},
        {"{% for item in [1] %}{{ [loop.changed(item)] == [] }}:"
         "{{ loop.changed(item) }}{% endfor %}",
         "False:False"},
        {"{% for item in [1] %}{{ [loop.changed(item), loop.changed(item + 1)][0] }}:"
         "{{ loop.changed(item + 1) }}{% endfor %}",
         "True:False"},
        {"{% for item in [1] %}{{ {'a': loop.changed(item), 'a': loop.changed(item)} | safe }}"
         "{% endfor %}",
         "{'a': False}"},
        {"{% for item in [1] %}{{ (loop.changed(item), loop.changed(item)) | safe }}"
         "{% endfor %}",
         "(True, False)"},
        {"{% for item in [1] %}{{ loop.cycle([loop.changed(item)], [loop.changed(item + 1)]) }}:"
         "{{ loop.changed(item + 1) }}{% endfor %}",
         "[True]:False"},
        {"{% for outer in [1] %}{% for inner in [loop.changed(outer), loop.changed(outer)] %}"
         "{{ inner }}{{ inner }}{% endfor %}{% endfor %}",
         "TrueTrueFalseFalse"}};
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

  it("reports errors in unselected collection elements") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ [1, 1 / 0][0] }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("bounds retained loop changed arguments by the render node budget") {
    static const char source[] =
        "{% for item in [1] %}{{ loop.changed(item) }}{{ loop.changed(item, item) }}"
        "{{ loop.changed(item, item, item) }}{{ loop.changed(item, item, item, item) }}"
        "{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 9u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 10u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "TrueTrueTrueTrue");
    free(output);
  }
}

spec("Jinja CMeta loop controls") {
  it("break and continue unwind scopes and preserve Jinja loop else behavior") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for x in [1,2,3] %}{{x}}{% if x==2 %}{% break %}{% endif %}{% endfor %}Z", "12Z"},
      {"{% for x in [1,2,3] %}{% if x==2 %}{% continue %}{% endif %}{{x}}{% endfor %}", "13"},
      {"{% for x in [1,2] %}{{x}}{% break %}{% else %}E{% endfor %}", "1E"},
      {"{% for x in [1,2] %}{% continue %}{% else %}E{% endfor %}", "E"},
      {"{% for x in [1,2] %}{% if x==2 %}{% break %}{% endif %}{{x}}{% else %}E{% endfor %}", "1"},
      {"{% for x in [] %}{% break %}{% else %}E{% endfor %}", "E"},
      {"{% for x in [1,2] %}{% for y in [3,4] %}{{x}}{{y}}{% break %}{% endfor %}A{% endfor %}", "13A23A"},
      {"{% set x='outer' %}{% for i in [1,2] %}{% with x='inner' %}{% autoescape true %}{{'<'}}{% continue %}{% endautoescape %}{% endwith %}{% endfor %}{{x}}|{{'<'}}", "&lt;&lt;outer|<"},
      {"{% for x in [1,2] %}{% filter trim %}A{% break %}B{% endfilter %}{% else %}E{% endfor %}Z", "EZ"},
      {"{% set s='old' %}{% for x in [1,2] %}{% set s %}A{% continue %}B{% endset %}{% else %}E{% endfor %}{{s}}", "Eold"},
      {"{% for x in [1,2,3,4] if x is even %}{{loop.index}}:{{x}}{% break %}{% else %}E{% endfor %}", "1:2E"},
      {"{% for x in [1,2] %}{% for y in [] %}{% else %}{% break %}{% endfor %}Z{% endfor %}A", "A"},
      {"{% for x in [2,1] recursive %}{{x}}{% if x==2 %}{{loop([0])}}{% endif %}{% break %}{% endfor %}", "20"},
      {"{% for x in [1,2] %}{% filter trim %}{% filter trim %}A{% continue %}{% endfilter %}{% endfilter %}{% else %}E{% endfor %}", "E"}
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

  it("rejects loop controls without a lexical loop or with arguments") {
    static const char *sources[] = {"{% break %}", "{% continue %}", "{% if false %}{% break %}{% endif %}",
      "{% for x in [] %}{% else %}{% continue %}{% endfor %}",
      "{% for x in [1] %}{% for y in [] recursive %}{% else %}{% break %}{% endfor %}Z{% endfor %}",
      "{% for x in [1] %}{% break 1 %}{% endfor %}", "{% for x in [1] %}{% continue() %}{% endfor %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      info("source: %s", sources[i]);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("loop controls retain iterator consumption and do not swallow capacity failures") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set it={'a':1,'b':2}|items %}{% for k,v in it %}{{k}}{% break %}{% endfor %}{{it|list}}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a[('b', 2)]");
    free(output);
    output = NULL;
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render("{% for x in [1] %}{% set s %}ab{% break %}{% endset %}{% endfor %}",
                                 &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 2u;
    options.max_string_bytes = JINJA_CMETA_DEFAULT_MAX_STRING_BYTES;
    check_equal(jinja_test_render("{% for x in range(10) %}{% continue %}{% endfor %}",
                                 &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }
}

spec("Jinja CMeta collections and runtime: loops 11") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("executes Unicode and grouped single loop targets through the shared grammar") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for 项 in [1,2] %}{{ 项 }}{% endfor %}", "12"},
      {"{% for\u3000项\u00a0in\u3000[3,4] %}{{ 项 }}{% endfor %}", "34"},
      {"{% for (项) in [5] %}{{ 项 }}{% endfor %}", "5"},
      {"{% for(项)in[6] %}{{ 项 }}{% endfor %}", "6"},
      {"{% for 项 in [{'名':'甲'}] %}{{ 项.名 }}{% endfor %}", "甲"},
      {"{% for 项 in [1] %}{% for 项 in [2] %}{{项}}{% endfor %}{{项}}{% endfor %}|{{项}}", "21|"},
      {"{% for é in [1] %}{% for e\u0301 in [2] %}{{é}}{{e\u0301}}{% endfor %}{% endfor %}", "12"},
      {"{% for 项 in [] %}bad{% else %}空{% endfor %}", "空"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("unpacks loop targets in each iteration scope") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for a,b in [(1,2),(3,4)] %}{{loop.index}}:{{a}}{{b}};{% endfor %}", "1:12;2:34;"},
      {"{% for 名,(值,尾) in [('甲',(2,3))] %}{{名}}{{值}}{{尾}}{% endfor %}", "甲23"},
      {"{% for a,b in {'x':1,'y':2}|items %}{{a}}{{b}}{% endfor %}", "x1y2"},
      {"{% for a,b in ['甲乙','丙丁'] %}{{b}}{{a}}{% endfor %}", "乙甲丁丙"},
      {"{% for a,a in [(1,2)] %}{{a}}{% endfor %}", "2"},
      {"{% set a=9 %}{% for a,b in [(1,2),(3,4)] %}{{a}}{% set a=8 %}{{a}}{% endfor %}|{{a}}|{{b}}", "1838|9|"},
      {"{% for a,b in [] %}bad{% else %}empty{% endfor %}", "empty"},
      {"{% for (a,) in [(7,)] %}{{a}}{% endfor %}", "7"},
      {"{% for () in [()] %}x{% endfor %}", "x"},
      {"{% for a,b in [({'name':'A'},2)] %}{{a.name}}{% for a,b in [(3,4)] %}{{a}}{{b}}{% endfor %}{{a.name}}{{b}}{% endfor %}", "A34A2"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects loop unpack shape errors before rendering the iteration body") {
    static const char *sources[] = {
      "{% for a,b in [(1,)] %}bad{% endfor %}",
      "{% for a,b in [(1,2,3)] %}bad{% endfor %}",
      "{% for a,(b,c) in [(1,2)] %}bad{% endfor %}",
      "{% for a,b in [(1,2),(3,)] %}{{a}}{{b}}{% endfor %}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("stops loop unpacking at failure without emitting the failed body") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "P{% for a,b in [(1,2),(3,)] %}{{a}}{{b}}{% endfor %}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, (size_t)3u);
    check_equal(memcmp(sink.bytes, "P12", 3u), 0);
    jinja_cmeta_release(templ);
  }

  it("bounds loop unpack materialization with the render node budget") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_nodes = 1u;
    char *output = NULL;
    check_equal(jinja_test_render("{% for a,b in [(1,2)] %}bad{% endfor %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("filters loop candidates before assigning visible loop metadata") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for x in [1,2,3,4] if x is even %}{{loop.index}}:{{x}}/{{loop.length}}/{{loop.last}};{% endfor %}", "1:2/2/False;2:4/2/True;"},
      {"{% for 名,(a,b) in [('甲',(1,2)),('乙',(3,4))] if a>1 %}{{名}}{{b}}{% endfor %}", "乙4"},
      {"{% for k,v in {'a':1,'b':2}|items if v>1 %}{{k}}{{v}}{% endfor %}", "b2"},
      {"{% set x=9 %}{% for x in [1,2] if false %}bad{% else %}{{x}}{% endfor %}|{{x}}", "9|9"},
      {"{% for x in [] if missing.bad %}bad{% else %}empty{% endfor %}", "empty"},
      {"{% for x in [1,2,3,4] if x is even %}{{loop.previtem|default('-')}}:{{x}}:{{loop.nextitem|default('-')}};{% endfor %}", "-:2:4;2:4:-;"},
      {"{% set limit=3 %}{% for x in [1,2,3] if x<limit %}{% set limit=0 %}{{x}}/{{loop.length}};{% endfor %}", "1/2;2/2;"},
      {"{% set ns=namespace(limit=3) %}{% for x in [1,2,3] if x<ns.limit %}{{x}}{% set ns.limit=0 %}{% endfor %}", "1"},
      {"{% for x in [1,2] %}{% for y in [1,2,3] if y>loop.index %}{{x}}{{y}}{% endfor %}{% endfor %}", "121323"},
      {"{% for x in [1,2] if loop is undefined %}{{x}}{% endfor %}", "12"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("retains filtered unpack results without consuming candidate iterators twice") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for a,b in [{'x':1,'y':2}|items] if true %}{{a}}{{b}}{% endfor %}", "('x', 1)('y', 2)"},
      {"{% for a,a in [(1,2),(3,4)] if true %}{{a}}/{{loop.nextitem|default('-')}};{% endfor %}", "2/(4, 4);4/-;"},
      {"{% for a,(b,c) in [(1,{'x':2,'y':3}|items)] if true %}{{a}}{{b}}{{c}}{% endfor %}", "1('x', 2)('y', 3)"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("reports filtered loop failures only when their candidates are consumed") {
    static const struct { const char *source; const char *prefix; } cases[] = {
      {"P{% for x in [1,2] if x==1 or missing.bad %}{{x}}{% endfor %}Q", "P1"},
      {"P{% for x in [1,2] if x==1 or missing.bad %}{{loop.length}}{% endfor %}Q", "P"},
      {"P{% for a,b in [(1,)] if false %}bad{% else %}wrong{% endfor %}", "P"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JinjaTestByteSink sink = {{0}, 0u};
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(cases[i].source), NULL, &error);
      check_not_null(templ);
      check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_equal(sink.length, strlen(cases[i].prefix));
      check_equal(memcmp(sink.bytes, cases[i].prefix, sink.length), 0);
      jinja_cmeta_release(templ);
    }
  }

  it("reserves filtered cache space according to the source bound") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_test_render(
        "{% for a in range(100) %}{% for x in [1] if true %}x{% endfor %}"
        "{% for x in [] if true %}bad{% endfor %}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(strlen(output), (size_t)100u);
    check_equal(strspn(output, "x"), (size_t)100u);
    free(output);
  }

  it("bounds rejected candidate scans and repeated filtered cache reservations") {
    static const char *sources[] = {
      "{% for x in range(100) if false %}bad{% endfor %}",
      "{% for x in range(20) %}{% for y in [1] if true %}{% endfor %}{% endfor %}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_nodes = 32u;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, &options, &output, &error),
                  JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
    }
  }

  it("keeps filtered predicate escaping in its lexical evaluation context") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_test_render(
        "{% for x in ['<','<'] if x ~ (''|safe) == '<' %}"
        "{% autoescape true %}{{loop.length}}{% endautoescape %}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "22");
    free(output);
  }

  it("iterates string loops by Unicode scalar with normal loop semantics") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for ch in 'A中😀é' %}{{loop.index}}/{{loop.length}}={{ch}};{% endfor %}", "1/5=A;2/5=中;3/5=😀;4/5=e;5/5=́;"},
      {"{% for ch in '' %}bad{% else %}empty{% endfor %}", "empty"},
      {"{% for ch in '甲乙甲' if ch!='乙' %}{{loop.previtem|default('-')}}:{{ch}}:{{loop.nextitem|default('-')}}/{{loop.revindex}};{% endfor %}", "-:甲:甲/2;甲:甲:-/1;"},
      {"{% for ch in '甲' recursive %}{{loop.depth}}:{{ch}};{% if ch=='甲' %}{{loop('😀')}}{% endif %}{% endfor %}", "1:甲;2:😀;"},
      {"{% autoescape true %}{% for ch in '<&'|safe %}{{ch}}{% endfor %}{% endautoescape %}", "&lt;&amp;"},
      {"{% for (ch,) in '甲😀' %}{{ch}}{% endfor %}", "甲😀"},
      {"{% for ch in 'ab' if false %}bad{% else %}empty{% endfor %}", "empty"},
      {"{% set ch='outer' %}{% for ch in 'ab' %}{{ch}}{% endfor %}|{{ch}}", "ab|outer"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("streams borrowed string characters without losing NUL or non-BMP bytes") {
    static const unsigned char input[] = {'A', 0u, 0xf0u, 0x9fu, 0x98u, 0x80u};
    static const unsigned char expected[] = {'P', '[', 'A', ']', '[', 0u, ']',
        '[', 0xf0u, 0x9fu, 0x98u, 0x80u, ']', 'Q'};
    JinjaTestRoot root = {{vstr_from_buf((const char *)input, sizeof(input)), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "P{% for ch in user.name %}[{{ch}}]{% endfor %}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_OK);
    check_equal(sink.length, sizeof(expected));
    check_equal(memcmp(sink.bytes, expected, sizeof(expected)), 0);
    jinja_cmeta_release(templ);
  }

  it("validates string iteration bytes and budgets before entering its body") {
    static const unsigned char invalid[] = {'a', 0xf0u, 0x9fu};
    static const struct {
      vstr input;
      size_t nodes, bytes;
      JINJA_CMETA_STATUS status;
      const char *output;
    } cases[] = {
      {{(const char *)invalid, sizeof(invalid)}, 0u, 0u, JINJA_CMETA_ERR_METADATA, "P"},
      {{"abc", 3u}, 2u, 0u, JINJA_CMETA_ERR_CAPACITY, "P"},
      {{"\xf0\x9f\x98\x80", 4u}, 0u, 3u, JINJA_CMETA_ERR_CAPACITY, "P"},
      {{"\xf0\x9f\x98\x80", 4u}, 0u, 4u, JINJA_CMETA_OK, "PxQ"},
      {{"", 0u}, 0u, 0u, JINJA_CMETA_OK, "PQ"}
    };
    JinjaTestRoot root = {{vstr_from_cstr(""), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "P{% for ch in user.name %}x{% endfor %}Q"), NULL, &error);
    check_not_null(templ);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      root.user.name = cases[i].input;
      JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
      options.max_nodes = cases[i].nodes;
      options.max_string_bytes = cases[i].bytes;
      JinjaTestByteSink sink = {{0}, 0u};
      check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
                  cases[i].status);
      check_equal(sink.length, strlen(cases[i].output));
      check_equal(memcmp(sink.bytes, cases[i].output, sink.length), 0);
      if (cases[i].status != JINJA_CMETA_OK) check_equal(error.offset, (size_t)1u);
    }
    jinja_cmeta_release(templ);
  }

  it("renders recursive loops with independent depth and iteration metadata") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for x in [2,1] recursive %}{{loop.depth}}:{{loop.index}}={{x}};{% if x>0 %}{{loop([x-1])}}{% endif %}{% endfor %}", "1:1=2;2:1=1;3:1=0;1:2=1;2:1=0;"},
      {"{% for x in [] recursive %}bad{% else %}empty{% endfor %}", "empty"},
      {"{% for x in [1] recursive %}{{x}}{{loop([])}}{{x}}{% else %}E{% endfor %}", "1E1"},
      {"{% for a,b in [(2,3)] if a>0 recursive %}{{a}}{{b}}{% if a>1 %}{{loop([(a-1,b+1)])}}{% endif %}{% endfor %}", "2314"},
      {"{% set name='outer' %}{% for x in [1] recursive %}{{name}}{% set name='inner' %}{% if x %}{{loop([0])}}{% endif %}{{name}}{% endfor %}|{{name}}", "outerouterinnerinner|outer"},
      {"{% autoescape true %}{% for x in [1] recursive %}<b>{{x}}</b>{% if x %}{{loop([0])}}{% endif %}{% endfor %}{% endautoescape %}", "<b>1</b><b>0</b>"},
      {"{% for x in [1] recursive %}{{loop.depth0}}:{{x}};{% if x %}{{loop(iterable=[0])}}{% endif %}{% endfor %}", "0:1;1:0;"},
      {"{% set ns=namespace(n=0) %}{% for x in [1] recursive %}{% set ns.n=ns.n+1 %}{% if x %}{{loop([0])}}{% endif %}{{ns.n}}{% endfor %}|{{ns.n}}", "22|2"},
      {"{% for x in [1,1] recursive %}{{loop.cycle('a','b')}}{{loop.changed(x)}};{% if x %}{{loop([0,0])}}{% endif %}{% endfor %}", "aTrue;aTrue;bFalse;bFalse;aTrue;bFalse;"},
      {"{% for x in [1] recursive %}{% if x %}{% set result=loop([0]) %}{{result}}/{{result}}{% else %}child{% endif %}{% endfor %}", "child/child"},
      {"{% autoescape true %}{% for x in [1] recursive %}{{'<b>'}}{% if x %}{% autoescape false %}{{loop([0])}}{% endautoescape %}{% endif %}{% endfor %}{% endautoescape %}", "&lt;b&gt;&lt;b&gt;"},
      {"{% for x in [1] recursive %}{{'<b>'}}{% if x %}{% autoescape true %}{{loop([0])}}{% endautoescape %}{% endif %}{% endfor %}", "<b>&lt;b&gt;"},
      {"{% for x in [1,2] if x>0 recursive %}{{loop.previtem|default('-')}}:{{x}}:{{loop.nextitem|default('-')}}/{{loop.length}};{% if x==1 %}{{loop([0,2])}}{% endif %}{% endfor %}", "-:1:2/2;-:2:-/1;1:2:-/2;"},
      {"{% for outer in [7] %}{% for x in [1] recursive %}{{outer}}:{{loop.depth}};{% if x %}{{loop([0])}}{% endif %}{% endfor %}{{loop.depth}}{% endfor %}", "7:1;7:2;1"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("bounds recursive loops and rejects calls without a recursive loop") {
    static const struct { const char *source; JINJA_CMETA_STATUS status; } cases[] = {
      {"{% for x in [1] recursive %}{{loop([x])}}{% endfor %}", JINJA_CMETA_ERR_CAPACITY},
      {"{% for x in [1] recursive %}{{loop.cycle(loop.cycle(loop.cycle(loop.cycle(loop([x])))))}}{% endfor %}", JINJA_CMETA_ERR_CAPACITY},
      {"{% for x in [1] recursive %}{{namespace(a=namespace(a=namespace(a=loop([x]))))}}{% endfor %}", JINJA_CMETA_ERR_CAPACITY},
      {"{{loop([])}}", JINJA_CMETA_ERR_RENDER},
      {"{% for x in [1] %}{{loop([])}}{% endfor %}", JINJA_CMETA_ERR_RENDER},
      {"{% for x in [1] recursive %}{{loop()}}{% endfor %}", JINJA_CMETA_ERR_RENDER},
      {"{% for x in [1] recursive %}{{loop([],[])}}{% endfor %}", JINJA_CMETA_ERR_RENDER},
      {"{% for x in [1] recursive %}{{loop(wrong=[])}}{% endfor %}", JINJA_CMETA_ERR_RENDER},
      {"{% for x in [1] recursive %}{{loop(1)}}{% endfor %}", JINJA_CMETA_ERR_RENDER}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), cases[i].status);
      check_null(output);
    }
  }

  it("honors recursive depth and capture byte limits without leaking failed child output") {
    static const char source[] = "P{% for x in [1] recursive %}ab{{loop([0])}}{% endfor %}Q";
    static const char prefix[] = "P{% for x in [1] recursive %}ab";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    for (size_t i = 0u; i < 2u; ++i) {
      JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
      if (i == 0u) options.max_render_depth = 2u;
      else options.max_string_bytes = 3u;
      JinjaTestByteSink sink = {{0}, 0u};
      check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
                  JINJA_CMETA_ERR_CAPACITY);
      check_equal(sink.length, sizeof("Pab") - 1u);
      check_equal(memcmp(sink.bytes, "Pab", sink.length), 0);
      if (i == 0u) check_equal(error.offset, sizeof(prefix) - 1u);
    }
    jinja_cmeta_release(templ);
  }

  it("bounds expression frames across recursive render calls") {
    static const char source[] = "{% for x in [1] recursive %}{{"
        "''~(''~(''~(''~(''~(''~(''~(''~("
        "''~(''~(''~(''~(''~(''~(''~(''~("
        "''~(''~(''~(''~(''~(''~(''~(''~("
        "loop([x])" "))))))))" "))))))))" "))))))))" "}}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("validates unsupported loop forms before classifying runtime availability") {
    static const struct { const char *source; JINJA_CMETA_STATUS status; } cases[] = {
      {"{% for 项,(值,loop) in [] %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for a, in [(7,)] %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for 项 in [1] if 项 recursive extra %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for 项 in [] recursive recursive %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for 项,值 in [1] if %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for 1 in [1] %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for 项.值 in [1] %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for (loop) in [] %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX},
      {"{% for %}{% endfor %}", JINJA_CMETA_ERR_SYNTAX}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(cases[i].source), NULL, &error);
      check_null(templ);
      check_equal(error.status, cases[i].status);
      check_equal(error.offset, (size_t)0u);
    }
  }

  it("keeps Unicode loop name admission independent of the Windows locale") {
#if defined(_WIN32)
    static const char source[] = "{% for \xc2\xaa in users %}x{% endfor %}";
    const char *current_locale = setlocale(LC_CTYPE, NULL);
    tstr original_locale = current_locale != NULL ? tstr_dup(current_locale) : NULL;
    JINJA_CMETA_TEMPLATE *templ = NULL;
    int locale_available = 0;

    if (original_locale != NULL) {
      locale_available = setlocale(LC_CTYPE, ".1252") != NULL;
      if (locale_available) templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
      setlocale(LC_CTYPE, original_locale);
    }

    check_not_null(original_locale);
    check(locale_available);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);

    jinja_cmeta_release(templ);
    tstr_free(original_locale);
#else
    check(1);
#endif
  }
}
