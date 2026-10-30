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
    size_t call_count;
    struct {
      char name[64];
      size_t source_offset;
      JINJA_CMETA_EMIT_CALL_KIND kind;
    } calls[8];
    size_t source_range_count;
    size_t intermediate_offset;
    int intermediate_identity;
    struct {
      char name[64];
      size_t source_begin, source_end;
      size_t intermediate_begin, intermediate_end;
      JINJA_CMETA_EMIT_KIND kind;
    } ranges[16];
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
  if (event->active_call_count > 8u ||
      (event->active_call_count != 0u && event->active_calls == NULL))
    return -1;
  sample->events[index].call_count = event->active_call_count;
  for (size_t i = 0u; i < event->active_call_count; ++i) {
    const JINJA_CMETA_EMIT_CALL *call = &event->active_calls[i];
    if (call->template_name.len >= sizeof(sample->events[index].calls[i].name))
      return -1;
    if (call->template_name.len != 0u)
      memcpy(sample->events[index].calls[i].name,
             call->template_name.data, call->template_name.len);
    sample->events[index].calls[i].name[call->template_name.len] = '\0';
    sample->events[index].calls[i].source_offset = call->source_offset;
    sample->events[index].calls[i].kind = call->kind;
  }
  if (event->source_range_count > 16u ||
      (event->source_range_count != 0u && event->source_ranges == NULL))
    return -1;
  sample->events[index].source_range_count = event->source_range_count;
  sample->events[index].intermediate_offset = event->intermediate_offset;
  sample->events[index].intermediate_identity = event->intermediate_identity;
  for (size_t i = 0u; i < event->source_range_count; ++i) {
    const JINJA_CMETA_EMIT_SOURCE_RANGE *range = &event->source_ranges[i];
    if (range->template_name.len >= sizeof(sample->events[index].ranges[i].name))
      return -1;
    if (range->template_name.len != 0u)
      memcpy(sample->events[index].ranges[i].name,
          range->template_name.data, range->template_name.len);
    sample->events[index].ranges[i].name[range->template_name.len] = '\0';
    sample->events[index].ranges[i].source_begin = range->source_begin;
    sample->events[index].ranges[i].source_end = range->source_end;
    sample->events[index].ranges[i].intermediate_begin = range->intermediate_begin;
    sample->events[index].ranges[i].intermediate_end = range->intermediate_end;
    sample->events[index].ranges[i].kind = range->kind;
  }
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


static JINJA_CMETA_STATUS jinja_trace_local_load(void *userdata, vstr name,
    JINJA_CMETA_SOURCE *source, JINJA_CMETA_ERROR *error) {
  (void)userdata; (void)error;
  static char included[] = "<span>{{ user.name }}</span>";
  if (!vstr_eq(name, vstr_from_cstr("fragment"))) return JINJA_CMETA_ERR_NOT_FOUND;
  *source = (JINJA_CMETA_SOURCE){
      vstr_from_buf(included, sizeof(included) - 1u), included};
  return JINJA_CMETA_OK;
}

static void jinja_trace_local_release(void *userdata, JINJA_CMETA_SOURCE *source) {
  (void)userdata; (void)source; /* The known template is static and borrowed. */
}

spec("Jinja preserves active nested template callsites and marks materialized values indirect") {
  it("reports original included-template instructions with parent include callsite") {
    static const char root_source[] = "A{% include 'fragment' %}B";
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){
        jinja_trace_local_load, jinja_trace_local_release, NULL};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_ENV *env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    if (!env) return;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_compile(env,
        vstr_from_cstr("app.ui.xml.jinja"), vstr_from_cstr(root_source), &error);
    check_not_null(templ);
    if (!templ) { jinja_cmeta_env_destroy(env); return; }
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
      static const char expected[] = "A<span>Ada</span>B";
      check_equal(sample.bytes, sizeof(expected) - 1u);
      check_equal(memcmp(sample.output, expected, sizeof(expected) - 1u), 0);
      size_t next = 0u, inside = 0u, outside = 0u;
      const size_t include_at = (size_t)(strstr(root_source, "{% include") - root_source);
      for (size_t i = 0u; i < sample.count; ++i) {
        check_equal(sample.events[i].generated_offset, next);
        next += sample.events[i].generated_bytes;
        if (strcmp(sample.events[i].source, "fragment") == 0) {
          ++inside;
          check_equal(sample.events[i].call_count, 1u);
          check_equal(strcmp(sample.events[i].calls[0].name,
                            "app.ui.xml.jinja"), 0);
          check_equal(sample.events[i].calls[0].source_offset, include_at);
          check(sample.events[i].calls[0].kind == JINJA_CMETA_CALL_INCLUDE);
        } else {
          ++outside;
          check_equal(strcmp(sample.events[i].source,
                             "app.ui.xml.jinja"), 0);
          check_equal(sample.events[i].call_count, 0u);
        }
      }
      check_equal(next, sample.bytes);
      check(inside >= 3u);
      check_equal(outside, 2u);
    }
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
  }

  it("identifies the direct streaming block invocation without attributing its bytes to the caller") {
    static const char source[] = "A{% block section %}inside{% endblock %}B";
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_ENV *env = jinja_cmeta_env_create(NULL, &error);
    check_not_null(env);
    if (!env) return;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_compile(
        env, vstr_from_cstr("blocks.ui.xml.jinja"),
        vstr_from_cstr(source), &error);
    check_not_null(templ);
    if (!templ) { jinja_cmeta_env_destroy(env); return; }
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
      static const char expected[] = "AinsideB";
      check_equal(sample.bytes, sizeof(expected) - 1u);
      check_equal(memcmp(sample.output, expected, sizeof(expected) - 1u), 0);
      const size_t opener = (size_t)(strstr(source, "{% block") - source);
      size_t inside = 0u, outside = 0u;
      for (size_t i = 0u; i < sample.count; ++i) {
        if (sample.events[i].call_count == 1u) {
          ++inside;
          check(sample.events[i].kind == JINJA_CMETA_EMIT_LITERAL);
          check_equal(strcmp(sample.events[i].calls[0].name,
                            "blocks.ui.xml.jinja"), 0);
          check_equal(sample.events[i].calls[0].source_offset, opener);
          check(sample.events[i].calls[0].kind == JINJA_CMETA_CALL_FUNCTION);
        } else {
          ++outside;
          check_equal(sample.events[i].call_count, 0u);
        }
      }
      check(inside > 0u);
      check_equal(outside, 2u);
    }
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
  }

  it("conservatively classifies captured and macro-derived strings as INDIRECT") {
    static const struct {
      const char *source;
      const char *expected;
    } cases[] = {
        {"{% set label %}<b>{% endset %}{{ label }}", "<b>"},
        {"{% macro badge() %}<b>{% endmacro %}{{ badge() }}", "<b>"},
        {"{% filter upper %}<b>x</b>{% endfilter %}", "<B>X</B>"},
        {"{% macro inner() %}x{% endmacro %}{% filter upper %}{{ inner() }}{% endfilter %}", "X"}
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    const JINJA_CMETA_RENDERER renderer = {jinja_trace_sample_write};
    const JINJA_CMETA_SOURCE_TRACE trace = {jinja_trace_sample_event};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ =
          jinja_cmeta_compile(vstr_from_cstr(cases[i].source), NULL, &error);
      check_not_null(templ);
      if (!templ) continue;
      JinjaTraceSample sample = {0};
      const JINJA_CMETA_STATUS status = jinja_cmeta_render_with_trace(
          templ, &model.root_desc, &root, NULL, &renderer, &sample,
          &trace, &sample, &error);
      check_equal(status, JINJA_CMETA_OK);
      if (status == JINJA_CMETA_OK) {
        check_equal(sample.bytes, strlen(cases[i].expected));
        check_equal(memcmp(sample.output, cases[i].expected, sample.bytes), 0);
        check(sample.count > 0u);
        size_t accounted = 0u;
        for (size_t j = 0u; j < sample.count; ++j) {
          check_equal(sample.events[j].generated_offset, accounted);
          accounted += sample.events[j].generated_bytes;
          check(sample.events[j].kind == JINJA_CMETA_EMIT_INDIRECT);
          check_equal(sample.events[j].call_count, 0u);
        }
        check_equal(accounted, sample.bytes);
      }
      jinja_cmeta_release(templ);
    }
  }
}



/* The same real render path: capture provenance survives exited macro frames.
 * Transforms preserve diagnostic coverage, never a fictitious column map. */
spec("Jinja M2 source intervals survive macros, captures and filters") {
  it("keeps an original macro body range across a later set-capture emission") {
    static const char source[] =
        "{% macro badge() %}<i>A</i>{% endmacro %}"
        "{% set label %}{{ badge() }}{% endset %}"
        "<div>{{ label }}</div>";
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_ENV *env = jinja_cmeta_env_create(NULL, &error);
    check_not_null(env);
    if (!env) return;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_compile(env,
        vstr_from_cstr("nested.ui.xml.jinja"), vstr_from_cstr(source), &error);
    check_not_null(templ);
    if (!templ) { jinja_cmeta_env_destroy(env); return; }
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
      static const char expected[] = "<div><i>A</i></div>";
      check_equal(sample.bytes, sizeof(expected) - 1u);
      check_equal(memcmp(sample.output, expected, sizeof(expected) - 1u), 0);
      const size_t body = (size_t)(strstr(source, "<i>A</i>") - source);
      int found = 0;
      for (size_t i = 0u; i < sample.count; ++i) {
        for (size_t j = 0u; j < sample.events[i].source_range_count; ++j) {
          const size_t lo = sample.events[i].ranges[j].source_begin;
          const size_t hi = sample.events[i].ranges[j].source_end;
          if (lo <= body && hi >= body + sizeof("<i>A</i>") - 1u) {
            check_equal(strcmp(sample.events[i].ranges[j].name,
                               "nested.ui.xml.jinja"), 0);
            check(sample.events[i].kind == JINJA_CMETA_EMIT_INDIRECT);
            check(sample.events[i].intermediate_identity != 0);
            found = 1;
          }
        }
      }
      check(found);
    }
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
  }

  it("reports filter input ranges but never claims identity after uppercasing") {
    static const char source[] = "{% filter upper %}ab{% endfilter %}";
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
      check_equal(sample.bytes, 2u);
      check_equal(memcmp(sample.output, "AB", 2u), 0);
      size_t accounted = 0u, origins = 0u;
      for (size_t i = 0u; i < sample.count; ++i) {
        check_equal(sample.events[i].generated_offset, accounted);
        accounted += sample.events[i].generated_bytes;
        check(sample.events[i].kind == JINJA_CMETA_EMIT_INDIRECT);
        check_equal(sample.events[i].intermediate_identity, 0);
        check_equal(sample.events[i].intermediate_offset, SIZE_MAX);
        origins += sample.events[i].source_range_count;
      }
      check_equal(accounted, 2u);
      check(origins > 0u);
    }
    jinja_cmeta_release(templ);
  }
}


/* Existing native test target: no separate consumer, workflow or renderer. */
spec("Jinja M2b copied versus transformed capture provenance") {
  it("keeps author ranges for direct filters and rejects transformed identity") {
    static const struct {
      const char *source;
      const char *expected;
      const char *author_bytes;
      int unchanged;
    } cases[] = {
      {"{% set label %}ab{% endset %}{{ label | upper }}", "AB", "ab", 0},
      {"{% set label %}a&{% endset %}{{ label | escape }}", "a&amp;", "a&", 0},
      {"{% set label %}ab{% endset %}{{ label | safe }}", "ab", "ab", 1},
      {"{% set label %} ab {% endset %}{{ label | trim }}", "ab", " ab ", 0},
      {"{% set label %}ab{% endset %}{% set next %}{{ label | upper }}{% endset %}{{ next }}",
       "AB", "ab", 0}
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    const JINJA_CMETA_RENDERER renderer = {jinja_trace_sample_write};
    const JINJA_CMETA_SOURCE_TRACE trace = {jinja_trace_sample_event};
    for (size_t case_index = 0u; case_index < sizeof(cases) / sizeof(cases[0]); ++case_index) {
      const char *source = cases[case_index].source;
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
      check_not_null(templ);
      if (templ == NULL) continue;
      JinjaTraceSample sample = {0};
      const JINJA_CMETA_STATUS status = jinja_cmeta_render_with_trace(
          templ, &model.root_desc, &root, NULL, &renderer, &sample,
          &trace, &sample, &error);
      check_equal(status, JINJA_CMETA_OK);
      if (status == JINJA_CMETA_OK) {
        check_equal(sample.bytes, strlen(cases[case_index].expected));
        check_equal(memcmp(sample.output, cases[case_index].expected, sample.bytes), 0);
        const char *position = strstr(source, cases[case_index].author_bytes);
        check_not_null(position);
        if (position != NULL) {
          const size_t original = (size_t)(position - source);
          int has_source = 0, has_output = 0;
          size_t next = 0u;
          for (size_t j = 0u; j < sample.count; ++j) {
            check_equal(sample.events[j].generated_offset, next);
            next += sample.events[j].generated_bytes;
            if (sample.events[j].kind != JINJA_CMETA_EMIT_INDIRECT) continue;
            check(sample.events[j].source_range_count != 0u);
            check_equal(sample.events[j].intermediate_identity, cases[case_index].unchanged);
            if (!cases[case_index].unchanged)
              check_equal(sample.events[j].intermediate_offset, SIZE_MAX);
            has_output = 1;
            for (size_t k = 0u; k < sample.events[j].source_range_count; ++k) {
              if (sample.events[j].ranges[k].source_begin <= original &&
                  sample.events[j].ranges[k].source_end >=
                      original + strlen(cases[case_index].author_bytes))
                has_source = 1;
            }
          }
          check_equal(next, sample.bytes);
          check(has_output);
          check(has_source);
        }
      }
      jinja_cmeta_release(templ);
    }
  }
}
