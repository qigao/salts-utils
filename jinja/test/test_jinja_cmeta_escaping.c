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


/* A native test of the real Jinja renderer's ORIGINAL instruction metadata,
 * not an after-the-fact substring match or a fabricated generated XML map. */
typedef struct JinjaTraceSample {
  char output[512];
  size_t bytes;
  struct {
    char source[64];
    size_t source_offset;
    size_t generated_offset;
    size_t generated_bytes;
    JINJA_CMETA_EMIT_KIND kind;
  } events[64];
  size_t count;
  int reject_trace;
} JinjaTraceSample;

static int jinja_trace_sample_write(const char *bytes, size_t count, void *opaque) {
  JinjaTraceSample *sample = (JinjaTraceSample *)opaque;
  if (sample == NULL || sample->bytes > sizeof(sample->output) ||
      count > sizeof(sample->output) - sample->bytes) return -1;
  if (count != 0u) memcpy(sample->output + sample->bytes, bytes, count);
  sample->bytes += count;
  return 0;
}

static int jinja_trace_sample_event(const JINJA_CMETA_EMIT_ANCHOR *event, void *opaque) {
  JinjaTraceSample *sample = (JinjaTraceSample *)opaque;
  if (sample == NULL || event == NULL || sample->reject_trace ||
      sample->count >= sizeof(sample->events) / sizeof(sample->events[0]) ||
      event->template_name.len >= sizeof(sample->events[0].source))
    return -1;
  /* The source identity is borrowed only for this callback. */
  const size_t index = sample->count++;
  if (event->template_name.len != 0u)
    memcpy(sample->events[index].source, event->template_name.data, event->template_name.len);
  sample->events[index].source[event->template_name.len] = '\0';
  sample->events[index].source_offset = event->source_offset;
  sample->events[index].generated_offset = event->generated_offset;
  sample->events[index].generated_bytes = event->generated_bytes;
  sample->events[index].kind = event->kind;
  return 0;
}

spec("Jinja emits bounded original instruction anchors without inventing output source maps") {
  it("reports contiguous written ranges across XML escapes and original named source offsets") {
    static const char source[] = "A{{ user.name | escape }}B";
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_ENV *env = jinja_cmeta_env_create(NULL, &error);
    check_not_null(env);
    if (!env) return;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_compile(env,
        vstr_from_cstr("studio.ui.xml.jinja"), vstr_from_cstr(source), &error);
    check_not_null(templ);
    if (!templ) { jinja_cmeta_env_destroy(env); return; }
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("<Ada>"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTraceSample sample = {0};
    const JINJA_CMETA_RENDERER renderer = {jinja_trace_sample_write};
    const JINJA_CMETA_SOURCE_TRACE trace = {jinja_trace_sample_event};
    const JINJA_CMETA_STATUS status = jinja_cmeta_render_with_trace(
        templ, &model.root_desc, &root, NULL, &renderer, &sample,
        &trace, &sample, &error);
    check_equal(status, JINJA_CMETA_OK);
    if (status == JINJA_CMETA_OK) {
      check_equal(sample.bytes, sizeof("A&lt;Ada&gt;B") - 1u);
      check_equal(memcmp(sample.output, "A&lt;Ada&gt;B", sample.bytes), 0);
      check(sample.count >= 3u);
      size_t next = 0u, expression_bytes = 0u, literals = 0u;
      const size_t expression_at = 1u;
      const size_t last_literal_at = sizeof(source) - 2u;
      for (size_t i = 0u; i < sample.count; ++i) {
        check_equal(sample.events[i].generated_offset, next);
        next += sample.events[i].generated_bytes;
        check_equal(strcmp(sample.events[i].source, "studio.ui.xml.jinja"), 0);
        if (sample.events[i].kind == JINJA_CMETA_EMIT_EXPRESSION) {
          check_equal(sample.events[i].source_offset, expression_at);
          expression_bytes += sample.events[i].generated_bytes;
        } else if (sample.events[i].kind == JINJA_CMETA_EMIT_LITERAL) {
          check(sample.events[i].source_offset == 0u ||
                sample.events[i].source_offset == last_literal_at);
          ++literals;
        } else {
          check_false(true); /* no indirect attribution in this simple source */
        }
      }
      check_equal(next, sample.bytes);
      check_equal(expression_bytes, sizeof("&lt;Ada&gt;") - 1u);
      check_equal(literals, 2u);
    }
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
  }

  it("keeps repeated loop emission anchors at their original instruction byte") {
    static const char source[] = "{% for i in range(2) %}[{{ user.name }}]{% endfor %}";
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    if (!templ) return;
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTraceSample sample = {0};
    const JINJA_CMETA_RENDERER renderer = {jinja_trace_sample_write};
    const JINJA_CMETA_SOURCE_TRACE trace = {jinja_trace_sample_event};
    const JINJA_CMETA_STATUS status = jinja_cmeta_render_with_trace(
        templ, &model.root_desc, &root, NULL, &renderer, &sample,
        &trace, &sample, &error);
    check_equal(status, JINJA_CMETA_OK);
    if (status == JINJA_CMETA_OK) {
      check_equal(sample.bytes, sizeof("[Ada][Ada]") - 1u);
      check_equal(memcmp(sample.output, "[Ada][Ada]", sample.bytes), 0);
      size_t count = 0u, expected_offset = 0u;
      const size_t print_offset = (size_t)(strstr(source, "{{ user.name }}") - source);
      for (size_t i = 0u; i < sample.count; ++i) {
        check_equal(sample.events[i].generated_offset, expected_offset);
        expected_offset += sample.events[i].generated_bytes;
        if (sample.events[i].kind == JINJA_CMETA_EMIT_EXPRESSION) {
          ++count;
          check_equal(sample.events[i].source_offset, print_offset);
        }
      }
      check_equal(count, 2u);
      check_equal(expected_offset, sample.bytes);
    }
    jinja_cmeta_release(templ);
  }

  it("fails closed for a missing trace callback and stops on trace sink refusal") {
    static const char source[] = "A{{ user.name }}B";
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    if (!templ) return;
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTraceSample sample = {0};
    const JINJA_CMETA_RENDERER renderer = {jinja_trace_sample_write};
    const JINJA_CMETA_SOURCE_TRACE invalid = {NULL};
    check_equal(jinja_cmeta_render_with_trace(
        templ, &model.root_desc, &root, NULL, &renderer, &sample,
        &invalid, &sample, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(sample.bytes, 0u);
    const JINJA_CMETA_SOURCE_TRACE trace = {jinja_trace_sample_event};
    sample.reject_trace = 1;
    check_equal(jinja_cmeta_render_with_trace(
        templ, &model.root_desc, &root, NULL, &renderer, &sample,
        &trace, &sample, &error), JINJA_CMETA_ERR_RENDER);
    check(sample.bytes > 0u);
    jinja_cmeta_release(templ);
  }
}
