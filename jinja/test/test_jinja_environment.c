#include "jinja_cmeta.h"
#include "jinja_cmeta_runtime.h"
#include "../src/jinja_cmeta_internal.h"
#include "tinytest.h"
#include <stdlib.h>
#include <string.h>
#include <tstr.h>

typedef struct TEST_SOURCE {
  const char *name, *text;
  JINJA_CMETA_STATUS status;
} TEST_SOURCE;

typedef struct TEST_LOADER {
  const TEST_SOURCE *sources;
  size_t count, loads, releases;
} TEST_LOADER;

static JINJA_CMETA_STATUS test_load(void *opaque, vstr name,
    JINJA_CMETA_SOURCE *source, JINJA_CMETA_ERROR *error) {
  TEST_LOADER *loader = (TEST_LOADER *)opaque;
  (void)error;
  ++loader->loads;
  for (size_t i = 0u; i < loader->count; ++i) {
    const TEST_SOURCE *entry = &loader->sources[i];
    if (!vstr_eq(name, vstr_from_cstr(entry->name))) continue;
    if (entry->status != JINJA_CMETA_OK) return entry->status;
    tstr text = tstr_new_len(entry->text, strlen(entry->text));
    if (text == NULL) return JINJA_CMETA_ERR_OUT_OF_MEMORY;
    *source = (JINJA_CMETA_SOURCE){vstr_from_buf(text, tstr_len(text)), text};
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_NOT_FOUND;
}

static void test_release(void *opaque, JINJA_CMETA_SOURCE *source) {
  TEST_LOADER *loader = (TEST_LOADER *)opaque;
  ++loader->releases;
  memset(source->lease, '?', source->text.len);
  tstr_free((tstr)source->lease);
}

static int test_autoescape_html(void *opaque, vstr name) {
  (void)opaque;
  return vstr_eq(name, vstr_from_cstr("page.html")) ? 1 : 0;
}

static int test_autoescape_html_suffix(void *opaque, vstr name) {
  static const char suffix[] = ".html";
  (void)opaque;
  if (!vstr_is_valid(name) || name.len < sizeof(suffix) - 1u) return 0;
  return memcmp(name.data + name.len - (sizeof(suffix) - 1u),
                suffix, sizeof(suffix) - 1u) == 0;
}

typedef struct TEST_BYTE_SINK {
  char bytes[256];
  size_t size;
} TEST_BYTE_SINK;

static int test_byte_sink_write(const char *text, size_t size, void *opaque) {
  TEST_BYTE_SINK *sink = (TEST_BYTE_SINK *)opaque;
  if (sink == NULL || (size != 0u && text == NULL) ||
      sink->size > sizeof(sink->bytes) ||
      size > sizeof(sink->bytes) - sink->size)
    return -1;
  if (size != 0u) memcpy(sink->bytes + sink->size, text, size);
  sink->size += size;
  return 0;
}

static JINJA_CMETA_STATUS test_registered_function(void *opaque,
    const JINJA_CMETA_CALL_CONTEXT *context, JINJA_CMETA_CALL_RESULT *result) {
  (void)opaque;
  if (context == NULL || result == NULL || context->argument_count != 1u ||
      context->positional_count != 1u ||
      context->arguments[0].value.kind != JINJA_CMETA_CALL_VALUE_STRING ||
      !vstr_eq(context->arguments[0].value.string, vstr_from_cstr("x")))
    return JINJA_CMETA_ERR_RENDER;
  result->value = (JINJA_CMETA_CALL_VALUE){
      .kind = JINJA_CMETA_CALL_VALUE_STRING, .string = vstr_from_cstr("host")};
  return JINJA_CMETA_OK;
}

typedef struct TEST_TRANSLATION {
  size_t calls;
  unsigned failures;
} TEST_TRANSLATION;

static JINJA_CMETA_STATUS test_translate_singular(void *opaque, vstr context,
    vstr singular, vstr plural, const JINJA_CMETA_CALL_ARGUMENT *arguments,
    size_t argument_count, JINJA_CMETA_CALL_RESULT *result) {
  TEST_TRANSLATION *translation = (TEST_TRANSLATION *)opaque;
  if (translation == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (!vstr_eq(context, vstr_from_cstr("menu"))) translation->failures |= 1u;
  if (!vstr_eq(singular, vstr_from_cstr("Hello %(name)s!"))) translation->failures |= 2u;
  if (plural.len != 0u) translation->failures |= 4u;
  if (argument_count != 1u) translation->failures |= 8u;
  if (argument_count != 0u) {
    if (!vstr_eq(arguments[0].name, vstr_from_cstr("name"))) translation->failures |= 16u;
    if (arguments[0].value.kind != JINJA_CMETA_CALL_VALUE_STRING) translation->failures |= 32u;
    if (!vstr_eq(arguments[0].value.string, vstr_from_cstr("Ada"))) translation->failures |= 64u;
  }
  ++translation->calls;
  result->value = (JINJA_CMETA_CALL_VALUE){
      .kind = JINJA_CMETA_CALL_VALUE_STRING, .string = vstr_from_cstr("Bonjour Ada!")};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS test_translate_plural(void *opaque, vstr context,
    vstr singular, vstr plural, const JINJA_CMETA_CALL_ARGUMENT *arguments,
    size_t argument_count, JINJA_CMETA_CALL_RESULT *result) {
  TEST_TRANSLATION *translation = (TEST_TRANSLATION *)opaque;
  if (translation == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (context.len != 0u) translation->failures |= 1u;
  if (!vstr_eq(singular, vstr_from_cstr("%(count)s item"))) translation->failures |= 2u;
  if (!vstr_eq(plural, vstr_from_cstr("%(count)s items"))) translation->failures |= 4u;
  if (argument_count != 1u) translation->failures |= 8u;
  if (argument_count != 0u) {
    if (!vstr_eq(arguments[0].name, vstr_from_cstr("count"))) translation->failures |= 16u;
    if (arguments[0].value.kind != JINJA_CMETA_CALL_VALUE_INTEGER) translation->failures |= 32u;
    if (arguments[0].value.integer != 2) translation->failures |= 64u;
  }
  ++translation->calls;
  result->value = (JINJA_CMETA_CALL_VALUE){
      .kind = JINJA_CMETA_CALL_VALUE_STRING, .string = vstr_from_cstr("2 articles")};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS test_translate_preserved_whitespace(void *opaque, vstr context,
    vstr singular, vstr plural, const JINJA_CMETA_CALL_ARGUMENT *arguments,
    size_t argument_count, JINJA_CMETA_CALL_RESULT *result) {
  TEST_TRANSLATION *translation = (TEST_TRANSLATION *)opaque;
  if (translation == NULL || result == NULL) return JINJA_CMETA_ERR_RENDER;
  if (context.len != 0u) translation->failures |= 1u;
  if (!vstr_eq(singular, vstr_from_cstr("\n  Hello\n  world\n"))) translation->failures |= 2u;
  if (plural.len != 0u) translation->failures |= 4u;
  if (argument_count != 0u) translation->failures |= 8u;
  ++translation->calls;
  result->value = (JINJA_CMETA_CALL_VALUE){
      .kind = JINJA_CMETA_CALL_VALUE_STRING, .string = vstr_from_cstr("Bonjour monde")};
  return JINJA_CMETA_OK;
}

spec("Jinja named template environment") {
  static JINJA_CMETA_ENV *env;
  static JINJA_CMETA_TEMPLATE *templ;
  static JINJA_CMETA_ERROR error;
  static TEST_LOADER loader;
  static char *output;
  before_each() { env = NULL; templ = NULL; output = NULL; loader = (TEST_LOADER){0}; }
  after_each() { free(output); jinja_cmeta_release(templ); jinja_cmeta_env_destroy(env); }

  it("applies named root autoescape across all render entry points") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.autoescape_selector = test_autoescape_html;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);

    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("page.html"),
        vstr_from_cstr("{{ '<x & y>' }}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    static const char expected[] = "&lt;x &amp; y&gt;";
    const JINJA_CMETA_RENDERER renderer = {test_byte_sink_write};

    TEST_BYTE_SINK sink = {{0}, 0u};
    check_equal(jinja_cmeta_render(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &renderer, &sink, &error), JINJA_CMETA_OK);
    check_equal(sink.size, sizeof(expected) - 1u);
    check_equal(sink.bytes, expected, sizeof(expected) - 1u);

    JINJA_CMETA_RUNTIME_CONFIG *runtime = jinja_cmeta_runtime_config_create(&error);
    check_not_null(runtime);
    sink = (TEST_BYTE_SINK){{0}, 0u};
    check_equal(jinja_cmeta_render_ex(templ, jinja_cmeta_vstr_data(),
        &root, NULL, runtime, &renderer, &sink, &error), JINJA_CMETA_OK);
    check_equal(sink.size, sizeof(expected) - 1u);
    check_equal(sink.bytes, expected, sizeof(expected) - 1u);

    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);
    output = NULL;

    check_equal(jinja_cmeta_render_string_ex(templ, jinja_cmeta_vstr_data(),
        &root, NULL, runtime, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);
    output = NULL;
    jinja_cmeta_runtime_config_destroy(runtime);

    jinja_cmeta_release(templ);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("plain.txt"),
        vstr_from_cstr("{{ '<x & y>' }}"), &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<x & y>");
  }

  it("allows explicit autoescape blocks to override and restore named root policy") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.autoescape_selector = test_autoescape_html;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("page.html"),
        vstr_from_cstr("{{ '<x>' }}|{% autoescape false %}{{ '<y>' }}"
                       "{% endautoescape %}|{{ '<z>' }}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "&lt;x&gt;|<y>|&lt;z&gt;");
  }

  it("applies named root strict undefined policy without changing default behavior") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("plain.txt"),
        vstr_from_cstr("A{{ missing }}B"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "AB");

    free(output);
    output = NULL;
    jinja_cmeta_release(templ);
    templ = NULL;
    jinja_cmeta_env_destroy(env);
    env = NULL;

    options = (JINJA_CMETA_ENV_OPTIONS)JINJA_CMETA_ENV_OPTIONS_INIT;
    options.undefined_policy = JINJA_CMETA_UNDEFINED_STRICT;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("plain.txt"),
        vstr_from_cstr("A{{ missing }}B"), &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("keeps direct unnamed compilation autoescape disabled") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ '<x & y>' }}"), NULL, &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<x & y>");
  }

  it("retains the defining instance of macros passed across includes") {
    const TEST_SOURCE sources[] = {{"child",
      "{% macro echo(x) %}child-{{ x }}{% endmacro %}"
      "{{ parent('P') }}|{% set ns.saved = echo %}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"), vstr_from_cstr(
      "{% set ns = namespace() %}{% macro echo(x) %}root-{{ x }}{% endmacro %}"
      "{% set parent = echo %}{% include 'child' %}"
      "{{ ns.saved('X') }}|{{ ns.saved.name }}|{{ ns.saved.arguments }}|{{ echo('Q') }}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "root-P|child-X|echo|('x',)|root-Q");
    check_equal(loader.loads, 1u);
    check_equal(loader.releases, 1u);
  }

  it("isolates include assignments and honors without context") {
    const TEST_SOURCE sources[] = {{"child",
      "{{ value|default('missing') }}{% set value = 'child' %}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"), vstr_from_cstr(
      "{% set value = 'root' %}{% include 'child' %}|"
      "{% include 'child' without context %}|{{ value }}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "root|missing|root");
  }

  it("runs filtered loop lookahead in its defining template") {
    const TEST_SOURCE sources[] = {{"child", "{{ x }}/{{ loop.length }};", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"), vstr_from_cstr(
      "{% for x in [1,2,3] if x > 1 %}{% include 'child' %}{% endfor %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2/2;3/2;");
  }

  it("invokes a recursive loop from another template") {
    const TEST_SOURCE sources[] = {{"child",
      "{{ loop.depth }}{% if row %}{{ loop(row) }}{% endif %}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"), vstr_from_cstr(
      "{% for row in [[[[]]]] recursive %}{% include 'child' %}{% endfor %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "123");
  }

  it("keeps self bound to its defining template across an include") {
    const TEST_SOURCE sources[] = {{"child", "{{ original.body() }}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"), vstr_from_cstr(
      "{% block body %}root{% endblock %}|{% set original = self %}{% include 'child' %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "root|root");
  }

  it("tries include candidates in order and ignores only absent names") {
    const TEST_SOURCE sources[] = {{"child", "found", JINJA_CMETA_OK},
      {"denied", NULL, JINJA_CMETA_ERR_LOADER}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"), vstr_from_cstr(
      "{% include ['missing', 'child'] %}{% include 'missing' ignore missing %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "found");
    free(output);
    output = NULL;
    jinja_cmeta_release(templ);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% include ['denied', 'child'] ignore missing %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_LOADER);
    check_equal(error.template_name, "denied");
    check_null(output);
  }

  it("keeps the failing child diagnostic while unwinding includes") {
    const TEST_SOURCE sources[] = {{"child", "prefix {{ 1/0 }}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% include 'child' %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(error.template_name, "child");
    check_equal(loader.releases, 1u);
    check_null(output);
  }

  it("shares cumulative load and source budgets across sibling includes") {
    const TEST_SOURCE sources[] = {{"child", "abc", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    const struct { size_t templates, bytes, loads; } cases[] = {{1u, 6u, 1u}, {2u, 5u, 2u}};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
      options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
      options.max_loaded_templates = cases[i].templates;
      options.max_loaded_source_bytes = cases[i].bytes;
      env = jinja_cmeta_env_create(&options, &error);
      check_not_null(env);
      templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
        vstr_from_cstr("{% include 'child' %}{% include 'child' %}"), &error);
      info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
      vstr root = vstr_from_cstr("");
      check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
      check_equal(loader.loads, cases[i].loads);
      check_equal(loader.releases, cases[i].loads);
      check_null(output);
      jinja_cmeta_release(templ);
      templ = NULL;
      jinja_cmeta_env_destroy(env);
      env = NULL;
      loader.loads = loader.releases = 0u;
    }
  }

  it("preserves an empty child name in runtime errors") {
    const TEST_SOURCE sources[] = {{"", "{{ 1/0 }}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% include '' %}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(error.template_name_length, 0u);
    check_equal(error.template_name, "");
  }

  it("does not treat undefined include names as missing templates") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% include absent ignore missing %}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(loader.loads, 0u);
  }

  it("requires a loader when executing an import") {
    env = jinja_cmeta_env_create(NULL, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% import 'module' as macros %}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_LOADER);
    check_null(output);
  }

  it("shares value visits across include calls") {
    const TEST_SOURCE sources[] = {{"child", "{{ [1] == [1] }}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% include 'child' %}{% include 'child' %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    JINJA_CMETA_RENDER_OPTIONS render = JINJA_CMETA_RENDER_OPTIONS_INIT;
    render.max_value_visits = 2u;
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, &render, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("bounds include cycles with the shared render depth") {
    const TEST_SOURCE sources[] = {{"child", "{% include 'child' %}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
      vstr_from_cstr("{% include 'child' %}"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    JINJA_CMETA_RENDER_OPTIONS render = JINJA_CMETA_RENDER_OPTIONS_INIT;
    render.max_render_depth = 3u;
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, &render, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(loader.loads, 3u);
    check_equal(loader.releases, 3u);
    check_null(output);
  }

  it("renders named source after its loader lease has been released") {
    const TEST_SOURCE sources[] = {{"页面", "Hello {{ 'Ada' }}", JINJA_CMETA_OK}};
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_load(env, vstr_from_cstr("页面"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    check_equal(loader.loads, 1u);
    check_equal(loader.releases, 1u);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Hello Ada");
  }

  it("owns delimiter configuration and compiles named source without a loader") {
    char open[] = "[[", close[] = "]]", name[] = "named";
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.variable_start_string = vstr_from_cstr(open);
    options.compile.variable_end_string = vstr_from_cstr(close);
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    open[0] = close[0] = '?';
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr(name), vstr_from_cstr("[[ 1/0 ]]"), &error);
    info("compile status=%d offset=%zu message=%s", error.status, error.offset, error.message);
    check_not_null(templ);
    memset(name, '?', sizeof(name) - 1u);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(error.template_name, "named");
    check_equal(error.template_name_length, 5u);
    check_equal(error.template_name_truncated, 0);
    check_null(jinja_cmeta_env_load(env, vstr_from_cstr("absent"), &error));
    check_equal(error.status, JINJA_CMETA_ERR_LOADER);
  }

  it("distinguishes lookup failures from failed source compilation") {
    const TEST_SOURCE sources[] = {
      {"denied", NULL, JINJA_CMETA_ERR_LOADER}, {"syntax", "{{", JINJA_CMETA_OK},
      {"large", "12345", JINJA_CMETA_OK}
    };
    const struct { const char *name; JINJA_CMETA_STATUS status; size_t releases; } cases[] = {
      {"missing", JINJA_CMETA_ERR_NOT_FOUND, 0u}, {"denied", JINJA_CMETA_ERR_LOADER, 0u},
      {"syntax", JINJA_CMETA_ERR_SYNTAX, 1u}, {"large", JINJA_CMETA_ERR_CAPACITY, 2u}
    };
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    options.max_loaded_source_bytes = 4u;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      info("name=%s", cases[i].name);
      templ = jinja_cmeta_env_load(env, vstr_from_cstr(cases[i].name), &error);
      check_null(templ);
      check_equal(error.status, cases[i].status);
      check_equal(error.template_name, cases[i].name);
      check_equal(loader.releases, cases[i].releases);
    }
  }
}

static int test_count_output(const char *text, size_t size, void *opaque) {
  (void)text;
  size_t *bytes = (size_t *)opaque;
  *bytes += size;
  return 0;
}

static JINJA_CMETA_STATUS test_render_named(JINJA_CMETA_ENV *env, const char *source,
    char **output, JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_env_compile(env,
      vstr_from_cstr("entry"), vstr_from_cstr(source), error);
  if (templ == NULL) return error->status;
  vstr root = vstr_from_cstr("");
  const JINJA_CMETA_STATUS status = jinja_cmeta_render_string(templ,
      jinja_cmeta_vstr_data(), &root, NULL, output, error);
  jinja_cmeta_release(templ);
  info("status=%d template=%s offset=%zu message=%s",
      status, error->template_name, error->offset, error->message);
  return status;
}

spec("Jinja imports and inheritance") {
  static const TEST_SOURCE sources[] = {
    {"module", "{% set public = 'P' %}{% set _private = 'secret' %}{% macro echo(x) %}mod-{{ x }}{% endmacro %}", JINJA_CMETA_OK},
    {"facade", "{% from 'module' import echo %}{% set _private = 'secret' %}{% set public = 'F' %}", JINJA_CMETA_OK},
    {"context", "{% macro read() %}{{ value|default('missing') }}{% endmacro %}", JINJA_CMETA_OK},
    {"body", "<b>", JINJA_CMETA_OK},
    {"loop_context", "{% macro read() %}{{ item }}/{{ loop.index }}{% endmacro %}", JINJA_CMETA_OK},
    {"base", "[{% block body %}B{% endblock %}]", JINJA_CMETA_OK},
    {"middle", "{% extends 'base' %}{% block body %}M({{ super() }}){% endblock %}", JINJA_CMETA_OK},
    {"value_base", "{% block body %}{{ value }}{% endblock %}", JINJA_CMETA_OK},
    {"scoped_base", "{% for item in [4,5] %}{% block body scoped %}base{% endblock %}{% endfor %}", JINJA_CMETA_OK},
    {"self_base", "{% block body %}B{% endblock %}|{{ self.body() }}", JINJA_CMETA_OK},
    {"required_base", "{% block body required %} {% endblock %}", JINJA_CMETA_OK},
    {"required_middle", "{% extends 'required_base' %}", JINJA_CMETA_OK},
    {"derived", "{% extends 'base' %}{% block body %}D{% endblock %}", JINJA_CMETA_OK},
    {"self_module", "{% block body %}S{% endblock %}{% macro again() %}{{ self.body() }}{% endmacro %}", JINJA_CMETA_OK},
    {"import_cycle", "{% import 'import_cycle' as m %}", JINJA_CMETA_OK},
    {"extends_cycle", "{% extends 'extends_cycle' %}", JINJA_CMETA_OK},
    {"failure", "{{ 1/0 }}", JINJA_CMETA_OK},
    {"autoescape_base.html",
     "[{% block body %}{{ '<base & x>' }}{% endblock %}]",
     JINJA_CMETA_OK}
  };
  static JINJA_CMETA_ENV *env;
  static JINJA_CMETA_ERROR error;
  static TEST_LOADER loader;
  static char *output;
  before_each() {
    output = NULL;
    loader = (TEST_LOADER){.sources = sources, .count = sizeof(sources) / sizeof(sources[0])};
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
  }
  after_each() { free(output); jinja_cmeta_env_destroy(env); }

  it("uses explicit cell limits independent of the entry template") {
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("{% import 'module' as m %}{{ m.echo('X') }}"), &error);
    check_not_null(compiled);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_CELLS, 1u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        NULL, config, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_CELLS, 128u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        NULL, config, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "mod-X");
    jinja_cmeta_release(compiled);
    jinja_cmeta_runtime_config_destroy(config);
  }

  it("rejects invalid configuration operations without changing limits") {
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    size_t limit = 0u;
    check_equal(jinja_cmeta_runtime_config_get_limit(config, JINJA_CMETA_RESOURCE_CELLS,
        &limit, &error), JINJA_CMETA_OK);
    check_equal(limit, JINJA_CMETA_DEFAULT_MAX_CELLS);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, (JINJA_CMETA_RESOURCE)0,
        1u, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(jinja_cmeta_runtime_config_get_limit(config, (JINJA_CMETA_RESOURCE)0,
        &limit, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(limit, JINJA_CMETA_DEFAULT_MAX_CELLS);
    check_equal(jinja_cmeta_runtime_config_get_limit(config, JINJA_CMETA_RESOURCE_CELLS,
        &limit, &error), JINJA_CMETA_OK);
    check_equal(limit, JINJA_CMETA_DEFAULT_MAX_CELLS);
    check_equal(jinja_cmeta_runtime_config_set_limit(NULL, JINJA_CMETA_RESOURCE_CELLS,
        1u, NULL), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(jinja_cmeta_runtime_config_get_limit(config, JINJA_CMETA_RESOURCE_CELLS,
        NULL, NULL), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    jinja_cmeta_runtime_config_destroy(config);
    jinja_cmeta_runtime_config_destroy(NULL);
  }

  it("checks zero and overflowing quotas before publishing streamed output") {
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("ok"), &error);
    check_not_null(compiled);
    vstr root = vstr_from_cstr("");
    size_t bytes = 0u;
    const JINJA_CMETA_RENDERER renderer = {test_count_output};
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_ACTIVATIONS,
        0u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_ex(compiled, jinja_cmeta_vstr_data(), &root, NULL,
        config, &renderer, &bytes, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(bytes, 0u);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_ACTIVATIONS,
        1u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_CELLS,
        SIZE_MAX, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_ex(compiled, jinja_cmeta_vstr_data(), &root, NULL,
        config, &renderer, &bytes, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(bytes, 0u);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_CELLS,
        0u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_ex(compiled, jinja_cmeta_vstr_data(), &root, NULL,
        config, &renderer, &bytes, &error), JINJA_CMETA_OK);
    check_equal(bytes, 2u);
    jinja_cmeta_release(compiled);
    jinja_cmeta_runtime_config_destroy(config);
  }

  it("bounds activation admission across imported macros") {
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_ACTIVATIONS, 1u, &error), JINJA_CMETA_OK);
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("{% import 'module' as m %}{{ m.echo('X') }}"), &error);
    check_not_null(compiled);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        NULL, config, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    jinja_cmeta_release(compiled);
    jinja_cmeta_runtime_config_destroy(config);
  }

  it("enforces an independent value quota across imported modules") {
    enum { SNAPSHOT_SLOTS = 2 };
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
        0u, &error), JINJA_CMETA_OK);
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("{% import 'module' as m %}{{ m.echo('X') }}"), &error);
    check_not_null(compiled);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        NULL, config, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "mod-X");
    free(output);
    output = NULL;
    jinja_cmeta_release(compiled);

    /* Each defining module retains one list slot; crossing a macro boundary
     * must not reset the render-wide quota. Scalar-only calls above need none. */
    const TEST_SOURCE snapshot_sources[] = {
      {"module", "{% from 'leaf' import echo %}{% macro run(x) %}"
                 "{{ [x]|first }}{{ echo(x) }}{% endmacro %}", JINJA_CMETA_OK},
      {"leaf", "{% macro echo(x) %}{{ [x]|first }}{% endmacro %}", JINJA_CMETA_OK}
    };
    loader.sources = snapshot_sources;
    loader.count = sizeof(snapshot_sources) / sizeof(snapshot_sources[0]);
    compiled = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("{% import 'module' as m %}{{ m.run('X') }}"), &error);
    check_not_null(compiled);
    for (size_t limit = 0u; limit <= SNAPSHOT_SLOTS; ++limit) {
      info("retained value quota=%zu", limit);
      check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
          limit, &error), JINJA_CMETA_OK);
      const JINJA_CMETA_STATUS expected = limit == SNAPSHOT_SLOTS
          ? JINJA_CMETA_OK : JINJA_CMETA_ERR_CAPACITY;
      check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
          NULL, config, &output, &error), expected);
      if (expected == JINJA_CMETA_OK) check_equal(output, "XX");
      else check_null(output);
    }
    jinja_cmeta_release(compiled);
    jinja_cmeta_runtime_config_destroy(config);
  }

  it("retains earlier snapshots while later value allocations grow") {
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
        1024u, &error), JINJA_CMETA_OK);
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("{% set saved = [1,2] %}{% for i in range(300) %}"
                      "{% set discard = [i] %}{% endfor %}{{ saved }}"), &error);
    check_not_null(compiled);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        NULL, config, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[1, 2]");
    jinja_cmeta_release(compiled);
    jinja_cmeta_runtime_config_destroy(config);
  }

  it("reserves loop argument cells for scoped block contexts") {
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_load(env, vstr_from_cstr("scoped_base"), &error);
    check_not_null(compiled);
    int loop_argument = 0;
    for (size_t i = 0u; i < compiled->cell_binding_count; ++i) {
      const JINJA_CMETA_CELL_BINDING *binding = &compiled->cell_bindings[i];
      if (vstr_eq(compiled->cells[binding->cell].name, vstr_from_cstr("loop")) &&
          binding->load == JINJA_CMETA_CELL_ARGUMENT) loop_argument = 1;
    }
    jinja_cmeta_release(compiled);
    check_equal(loop_argument, 1);
  }

  it("imports public assignments and macros with aliases") {
    check_equal(test_render_named(env, "{% import 'module' as m %}{% from 'module' import echo as say, public %}{{ m.echo('A') }}|{{ say('B') }}|{{ public }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "mod-A|mod-B|P");
    check_equal(loader.releases, loader.loads);
  }

  it("keeps private names and imported aliases out of exports") {
    check_equal(test_render_named(env, "{% import 'facade' as m %}{{ m._private|default('private') }}|{{ m.echo|default('hidden') }}|{{ m.public }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "private|hidden|F");
    check_equal(loader.releases, loader.loads);
  }

  it("turns absent exports into undefined values") {
    check_equal(test_render_named(env, "{% from 'module' import absent as x %}{{ x|default('missing') }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing");
    check_equal(loader.releases, loader.loads);
  }

  it("isolates default imports and snapshots explicit context") {
    check_equal(test_render_named(env, "{% set value = 'root' %}{% import 'context' as a %}{% import 'context' as b with context %}{% set value = 'later' %}{{ a.read() }}|{{ b.read() }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|root");
    check_equal(loader.releases, loader.loads);
  }

  it("reuses context-free modules within a render") {
    check_equal(test_render_named(env, "{% import 'module' as a %}{% import 'module' as b %}{{ a is sameas b }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    check_equal(loader.loads, 1u);
    check_equal(loader.releases, loader.loads);
  }

  it("does not cache context-dependent modules") {
    check_equal(test_render_named(env, "{% import 'module' as a with context %}{% import 'module' as b with context %}{{ a is sameas b }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False");
    check_equal(loader.loads, 2u);
    check_equal(loader.releases, loader.loads);
  }

  it("renders module bodies through string conversion and concatenation") {
    check_equal(test_render_named(env, "{% import 'body' as m %}{{ m }}|{{ m|string }}|{{ 'x' ~ m }}|{{ [m]|join(',') }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<b>|<b>|x<b>|<b>");
    check_equal(loader.releases, loader.loads);
  }

  it("distinguishes module repr from self without changing module string conversion") {
    check_equal(test_render_named(env,
        "{% import 'body' as m %}{{ [m] }}|{{ self }}|{{ m|string }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<TemplateModule 'body'>]|<TemplateReference None>|<b>");
  }

  it("retains module identity and exports through containers") {
    check_equal(test_render_named(env,
        "{% import 'module' as m %}{% set items = [m] %}"
        "{{ items[0] is sameas m }}|{{ items[0].echo('X') }}|{{ m is sameas self }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|mod-X|False");
  }

  it("passes loop context into imported macros") {
    check_equal(test_render_named(env, "{% for item in [4,5] %}{% import 'loop_context' as m with context %}{{ m.read() }};{% endfor %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "4/1;5/2;");
    check_equal(loader.releases, loader.loads);
  }

  it("preserves named autoescape inside overriding inheritance blocks") {
    jinja_cmeta_env_destroy(env);
    env = NULL;
    loader.loads = 0u;
    loader.releases = 0u;
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    options.autoescape_selector = test_autoescape_html_suffix;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);

    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_env_compile(
        env, vstr_from_cstr("page.html"),
        vstr_from_cstr(
            "{% extends 'autoescape_base.html' %}"
            "{% block body %}{{ '<child & x>' }}{% endblock %}"),
        &error);
    check_not_null(compiled);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(
        compiled, jinja_cmeta_vstr_data(), &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "[&lt;child &amp; x&gt;]");
    check_equal(loader.releases, loader.loads);
    jinja_cmeta_release(compiled);
  }

  it("resolves three generations of blocks and super") {
    check_equal(test_render_named(env, "{% extends 'middle' %}{% block body %}C({{ super() }})/{{ super.super() }}{% endblock %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[C(M(B))/B]");
    check_equal(loader.releases, loader.loads);
  }

  it("initializes assignments after extends before rendering the parent") {
    check_equal(test_render_named(env, "pre{% extends 'value_base' %}ignored{% set value = 'late' %}{% block body %}{{ value }}{% endblock %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "prelate");
    check_equal(loader.releases, loader.loads);
  }

  it("inherits scoped loop bindings without marking the override scoped") {
    check_equal(test_render_named(env, "{% extends 'scoped_base' %}{% block body %}{{ item }}/{{ loop.index }};{% endblock %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "4/1;5/2;");
    check_equal(loader.releases, loader.loads);
  }

  it("finds overriding blocks inside false conditionals") {
    check_equal(test_render_named(env, "{% extends 'base' %}{% if false %}{% block body %}C{% endblock %}{% endif %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[C]");
    check_equal(loader.releases, loader.loads);
  }

  it("routes self calls through the most derived block") {
    check_equal(test_render_named(env, "{% extends 'self_base' %}{% block body %}C{% endblock %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "C|C");
    check_equal(loader.releases, loader.loads);
  }

  it("allows required blocks to be fulfilled across an intermediate template") {
    check_equal(test_render_named(env, "{% extends 'required_middle' %}{% block body %}C{% endblock %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "C");
    check_equal(loader.releases, loader.loads);
  }

  it("supports conditional parent selection") {
    check_equal(test_render_named(env, "{% if false %}{% extends 'missing' %}{% else %}{% extends 'base' %}{% endif %}{% block body %}C{% endblock %}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[C]");
    check_equal(loader.releases, loader.loads);
  }

  it("includes and imports templates that inherit") {
    check_equal(test_render_named(env, "{% include 'derived' %}|{% import 'derived' as m %}{{ m }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[D]|[D]");
    check_equal(loader.releases, loader.loads);
  }

  it("keeps module self distinct from its exported namespace") {
    check_equal(test_render_named(env, "{% import 'self_module' as m %}{{ m.again() }}",
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "S");
    check_equal(loader.releases, loader.loads);
  }

  it("rejects cyclic imports") {
    check_equal(test_render_named(env, "{% import 'import_cycle' as m %}",
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.template_name, "import_cycle");
  }

  it("rejects cyclic inheritance") {
    check_equal(test_render_named(env, "{% extends 'extends_cycle' %}",
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.template_name, "extends_cycle");
  }

  it("rejects multiple executed parent selections") {
    check_equal(test_render_named(env, "{% extends 'base' %}{% extends 'middle' %}",
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.template_name, "entry");
  }

  it("rejects an unfulfilled required block") {
    check_equal(test_render_named(env, "{% extends 'required_middle' %}",
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.template_name, "required_base");
  }

  it("retains the imported template error identity") {
    check_equal(test_render_named(env, "{% import 'failure' as m %}",
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.template_name, "failure");
  }

  it("reports missing imported templates") {
    check_equal(test_render_named(env, "{% import 'missing' as m %}",
        &output, &error), JINJA_CMETA_ERR_NOT_FOUND);
    check_null(output);
  }
}

spec("Jinja registered functions") {
  static JINJA_CMETA_REGISTRY *registry;
  static JINJA_CMETA_ENV *env;
  static JINJA_CMETA_TEMPLATE *templ;
  static JINJA_CMETA_ERROR error;
  static TEST_LOADER loader;
  static char *output;
  before_each() {
    registry = NULL;
    env = NULL;
    templ = NULL;
    output = NULL;
    loader = (TEST_LOADER){0};
  }
  after_each() {
    free(output);
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
    jinja_cmeta_registry_destroy(registry);
  }

  it("invokes one environment function through included templates") {
    const TEST_SOURCE sources[] = {{"child", "{{ shout('x') }}", JINJA_CMETA_OK}};
    const JINJA_CMETA_CALLABLE callable = {
        vstr_from_cstr("shout"), 1u, 1u, 0u, test_registered_function, NULL};
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    loader.sources = sources;
    loader.count = sizeof(sources) / sizeof(sources[0]);
    registry = jinja_cmeta_registry_create(&error);
    check_not_null(registry);
    check_equal(jinja_cmeta_registry_register(registry, &callable, &error), JINJA_CMETA_OK);
    options.loader = (JINJA_CMETA_LOADER){test_load, test_release, &loader};
    env = jinja_cmeta_env_create_with_registry(&options, registry, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("parent"),
        vstr_from_cstr("{% include 'child' %}|{{ shout('x') }}"), &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(),
        &(vstr){"", 0u}, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "host|host");
  }
}

spec("Jinja i18n extension") {
  static JINJA_CMETA_ENV *env;
  static JINJA_CMETA_TEMPLATE *templ;
  static JINJA_CMETA_ERROR error;
  static TEST_TRANSLATION translation;
  static char *output;
  before_each() {
    env = NULL;
    templ = NULL;
    output = NULL;
    translation = (TEST_TRANSLATION){0};
  }
  after_each() {
    free(output);
    jinja_cmeta_release(templ);
    jinja_cmeta_env_destroy(env);
  }

  it("rejects i18n without a translation callback") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.extensions = JINJA_CMETA_EXTENSION_TAG_I18N;
    env = jinja_cmeta_env_create(&options, &error);
    check_null(env);
    check_equal(error.status, JINJA_CMETA_ERR_INVALID_ARGUMENT);
  }

  it("passes context and named singular bindings to the translator") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.extensions = JINJA_CMETA_EXTENSION_TAG_I18N;
    options.translation = test_translate_singular;
    options.translation_userdata = &translation;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"),
        vstr_from_cstr("{% trans 'menu' name='Ada' %}Hello {{ name }}!{% endtrans %}"),
        &error);
    info("i18n singular compile status=%d offset=%zu message=%s", error.status,
        error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    JINJA_CMETA_STATUS status = jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &output, &error);
    info("i18n singular render status=%d message=%s", error.status, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "Bonjour Ada!");
    check_equal(translation.calls, 1u);
    check_equal(translation.failures, 0u);
  }

  it("trims whitespace while preserving named translation placeholders") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.extensions = JINJA_CMETA_EXTENSION_TAG_I18N;
    options.translation = test_translate_singular;
    options.translation_userdata = &translation;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"), vstr_from_cstr(
        "{% trans 'menu' trimmed name='Ada' %}\n  Hello {{ name }}!\n{% endtrans %}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Bonjour Ada!");
    check_equal(translation.calls, 1u);
    check_equal(translation.failures, 0u);
  }

  it("preserves translation whitespace when notrimmed is selected") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.extensions = JINJA_CMETA_EXTENSION_TAG_I18N;
    options.translation = test_translate_preserved_whitespace;
    options.translation_userdata = &translation;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"), vstr_from_cstr(
        "{% trans notrimmed %}\n  Hello\n  world\n{% endtrans %}"), &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Bonjour monde");
    check_equal(translation.calls, 1u);
    check_equal(translation.failures, 0u);
  }

  it("resolves a translation binding from its enclosing loop scope") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.extensions = JINJA_CMETA_EXTENSION_TAG_I18N;
    options.translation = test_translate_singular;
    options.translation_userdata = &translation;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"), vstr_from_cstr(
        "{% for user in ['Ada'] %}{% trans 'menu' name=user %}Hello {{ name }}!{% endtrans %}{% endfor %}"),
        &error);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Bonjour Ada!");
    check_equal(translation.calls, 1u);
    check_equal(translation.failures, 0u);
  }

  it("passes plural messages and the selected count to the translator") {
    JINJA_CMETA_ENV_OPTIONS options = JINJA_CMETA_ENV_OPTIONS_INIT;
    options.compile.extensions = JINJA_CMETA_EXTENSION_TAG_I18N;
    options.translation = test_translate_plural;
    options.translation_userdata = &translation;
    env = jinja_cmeta_env_create(&options, &error);
    check_not_null(env);
    templ = jinja_cmeta_env_compile(env, vstr_from_cstr("entry"), vstr_from_cstr(
        "{% trans count=2 %}{{ count }} item{% pluralize count %}{{ count }} items{% endtrans %}"),
        &error);
    info("i18n plural compile status=%d offset=%zu message=%s", error.status,
        error.offset, error.message);
    check_not_null(templ);
    vstr root = vstr_from_cstr("");
    JINJA_CMETA_STATUS status = jinja_cmeta_render_string(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &output, &error);
    info("i18n plural render status=%d message=%s", error.status, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "2 articles");
    check_equal(translation.calls, 1u);
    check_equal(translation.failures, 0u);
  }
}
