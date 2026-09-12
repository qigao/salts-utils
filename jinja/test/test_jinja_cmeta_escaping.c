#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: escaping 3") {
  it("retains Markup representation inside containers but renders direct values as text") {
    static const char source[] = "{{ [user.name|safe] }}|{{ (user.name|safe,) }}|"
      "{{ {'x':user.name|safe}|string }}|{% for x in [user.name|safe] %}{{ [x] }}|{{ x|string }}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[Markup('<b>')]|(Markup('<b>'),)|{'x': Markup('<b>')}|[Markup('<b>')]|<b>");
    free(output);
  }

  it("evaluates autoescape expressions and restores scope through control flow") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% autoescape true if active else false %}{{ user.name }}{% endautoescape %}", "&lt;b&gt;"},
      {"{% autoescape not active %}{{ user.name }}{% endautoescape %}", "<b>"},
      {"{% autoescape true %}{% if false %}{% autoescape false %}bad{% endautoescape %}"
       "{% endif %}{% for x in [1,2] %}{% autoescape false %}{{ user.name }}{% endautoescape %}"
       "{{ user.name }}{% endfor %}{% endautoescape %}{{ user.name }}", "<b>&lt;b&gt;<b>&lt;b&gt;<b>"},
      {"{% autoescape true %}{{ '<a>' ~ '<b>' ~ (user.name|safe) }}{% endautoescape %}",
       "&lt;a&gt;&lt;b&gt;<b>"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("autoescape: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("bounds escaped storage and preserves Unicode and embedded NUL bytes") {
    static const char text[] = {'<', '\0', '\xE4', '\xB8', '\xAD', '&'};
    static const char expected[] = {'&','l','t',';','\0','\xE4','\xB8','\xAD','&','a','m','p',';','\0'};
    JinjaTestRoot root = {{vstr_from_buf(text, sizeof(text)), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_string_bytes = sizeof(expected) - 1u;
    check_equal(jinja_test_render("{{ user.name|escape }}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected, sizeof(expected));
    free(output);
    output = NULL;
    --options.max_string_bytes;
    check_equal(jinja_test_render("{{ user.name|escape }}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("rejects invalid safety filter arguments and mismatched autoescape blocks") {
    static const char *const arguments[] = {"{{ user.name|safe(1) }}", "{{ user.name|escape(x=1) }}", "{{ user.name|forceescape(1) }}"};
    static const char *const blocks[] = {"{% endautoescape %}", "{% autoescape true %}",
      "{% autoescape true %}{% else %}{% endautoescape %}", "{% if true %}{% endautoescape %}",
      "{% autoescape true %}{% endif %}"};
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(arguments) / sizeof(arguments[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(arguments[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    for (size_t i = 0u; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(blocks[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("evaluates safety filters as expressions and avoids double escaping") {
    static const char source[] =
        "{{ user.name|safe|escape }}|{{ user.name|escape|e }}|"
        "{{ user.name|escape|forceescape }}|{{ (user.name|safe) is escaped }}|"
        "{{ user.name is escaped }}|{{ (none|safe) is escaped }}";
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<b>|&lt;b&gt;|&amp;lt;b&amp;gt;|True|False|True");
    free(output);
  }

  it("preserves safety through string operations but not character iteration") {
    static const struct { const char *source; const char *expected; } cases[] = {
        {"{{ ((user.name|safe)[0]) is escaped }}|{{ ((user.name|safe)[::-1]) is escaped }}", "True|True"},
        {"{{ (user.name|safe|first) is escaped }}|{{ (user.name|safe|last) is escaped }}|"
         "{{ (user.name|safe|list|first) is escaped }}", "False|True|False"},
        {"{{ (user.name|safe|trim|center(8)|string) is escaped }}|"
         "{{ ((user.name|safe)*2) is escaped }}|{{ ((user.name|safe)*0) is escaped }}", "True|True|True"},
        {"{% for x in [user.name|safe] %}{{ x is escaped }}|{{ x|escape }}{% endfor %}", "True|<b>"},
        {"{{ (user.name|safe) + '<i>' }}|{{ '<i>' + (user.name|safe) }}", "<b>&lt;i&gt;|&lt;i&gt;<b>"},
        {"{{ (user.name|safe) ~ '<i>' }}|{{ ((user.name|safe) ~ '<i>') is escaped }}", "<b><i>|False"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("safety propagation: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("restores nested autoescape scope and honors value safety") {
    static const char source[] =
        "{{ user.name }}|{% autoescape active %}{{ user.name }}|{{ user.name|safe }}|"
        "{% autoescape false %}{{ user.name }}{% endautoescape %}|{{ user.name }}"
        "{% endautoescape %}|{{ user.name }}";
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<b>|&lt;b&gt;|<b>|<b>|&lt;b&gt;|<b>");
    free(output);
  }

  it("autoescape concatenation escapes unsafe fragments without reescaping markup") {
    static const char source[] =
        "{% autoescape true %}{{ (user.name|safe) ~ '<i>' }}|"
        "{{ '<i>' ~ (user.name|safe) }}|{{ user.name ~ '<i>' }}{% endautoescape %}";
    JinjaTestRoot root = {{vstr_from_cstr("<b>"), 0}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<b>&lt;i&gt;|&lt;i&gt;<b>|&lt;b&gt;&lt;i&gt;");
    free(output);
  }
}
