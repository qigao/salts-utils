#include "jinja_cmeta.h"
#include "tinytest.h"

#include <cmeta/struct.h>
#include <vstr.h>

#include <stddef.h>

enum {
  JINJA_BENCH_COMPILE_SAMPLES = 5000,
  JINJA_BENCH_RENDER_SAMPLES = 20000,
  JINJA_BENCH_STRING_SAMPLES = 250,
  JINJA_BENCH_STRING_OPS = 20,
  JINJA_BENCH_EXPECTED_OUTPUT_BYTES = 18
};

Struct(JinjaBenchRoot, (vstr, name), (bool, active));

static const cmeta_type_identity jinja_bench_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.JinjaRoot");
static const cmeta_type_desc jinja_bench_root_type = {"JinjaBenchRoot",
                                                      sizeof(JinjaBenchRoot),
                                                      _Alignof(JinjaBenchRoot),
                                                      CMETA_T_OBJECT,
                                                      NULL,
                                                      NULL,
                                                      &jinja_bench_root_identity};
static volatile size_t jinja_bench_sink;

typedef struct JinjaBenchOutput {
  size_t bytes;
} JinjaBenchOutput;

static int jinja_bench_count_output(const char *output, size_t size, void *data) {
  JinjaBenchOutput *state = (JinjaBenchOutput *)data;
  (void)output;
  state->bytes += size;
  return 0;
}

static const JINJA_CMETA_RENDERER jinja_bench_renderer = {jinja_bench_count_output};

spec("Jinja CMeta benchmark") {
  bench("string construction workloads") {
    static const struct {
      const char *name;
      const char *source;
      size_t output_bytes;
    } cases[] = {
        {"render literal string", "{{'none'}}", 4u},
        {"render native scalar", "{{none}}", 4u},
        {"render scalar string filter", "{{none|string}}", 4u},
        {"render container repr", "{{[1,2,3]}}", 9u},
        {"render container concatenation", "{{'A'~[1,2,3]}}", 10u},
        {"render large repeat", "{{'a'*16384}}", 16384u},
        {"render large concatenation", "{{'a'*16384~[1,2,3]}}", 16393u},
        {"render repeat 16 B", "{{'a'*16}}", 16u},
        {"render repeat 64 B", "{{'a'*64}}", 64u},
        {"render repeat 256 B", "{{'a'*256}}", 256u},
        {"render repeat 1 KiB", "{{'a'*1024}}", 1024u},
        {"render repeat 4 KiB", "{{'a'*4096}}", 4096u},
        {"render repeat 16 KiB", "{{'a'*16384}}", 16384u},
        {"render repeat 64 KiB", "{{'a'*65536}}", 65536u}};
    const vstr root = vstr_from_cstr("");
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_compile(vstr_from_cstr(cases[i].source), NULL, &error);
      JinjaBenchOutput output = {0u};
      JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
      check_not_null(compiled);
      check_equal(jinja_cmeta_render(compiled, jinja_cmeta_vstr_data(), &root, NULL,
          &jinja_bench_renderer, &output, &error), JINJA_CMETA_OK);
      check_equal(output.bytes, cases[i].output_bytes);
      benchmark_io(cases[i].name, JINJA_BENCH_STRING_SAMPLES, JINJA_BENCH_STRING_OPS,
                   cases[i].output_bytes * JINJA_BENCH_STRING_OPS) {
        output.bytes = 0u;
        for (size_t op = 0u; op < JINJA_BENCH_STRING_OPS; ++op) {
          JINJA_CMETA_STATUS current = jinja_cmeta_render(
              compiled, jinja_cmeta_vstr_data(), &root, NULL, &jinja_bench_renderer, &output, NULL);
          if (current != JINJA_CMETA_OK) status = current;
        }
        jinja_bench_sink += output.bytes;
      }
      check_equal(status, JINJA_CMETA_OK);
      jinja_cmeta_release(compiled);
    }
  }

  bench("legacy subset baseline") {
    static const char source[] = "Hello {{ name }}! {% if active %}ready{% else %}idle{% endif %}";
    cmeta_data_field_desc fields[2];
    cmeta_data_struct_shape shape;
    cmeta_data_desc root_desc;
    JinjaBenchRoot root = {vstr_from_cstr("World"), true};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *compiled;
    JinjaBenchOutput output = {0u};

    fields[0] = (cmeta_data_field_desc){"benchmark.JinjaRoot.name", "name",
                                        offsetof(JinjaBenchRoot, name), jinja_cmeta_vstr_data()};
    fields[1] = (cmeta_data_field_desc){"benchmark.JinjaRoot.active", "active",
                                        offsetof(JinjaBenchRoot, active), &cmeta_data_bool};
    shape = (cmeta_data_struct_shape){StructMeta(JinjaBenchRoot), fields, 2u};
    root_desc = (cmeta_data_desc){sizeof(cmeta_data_desc),
                                  CMETA_DATA_DESC_ABI_VERSION,
                                  "benchmark.JinjaRoot.data",
                                  "JinjaBenchRoot",
                                  CMETA_DATA_STRUCT,
                                  &jinja_bench_root_type,
                                  &shape,
                                  NULL,
                                  NULL,
                                  NULL};

    compiled = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(compiled);
    check_equal(error.status, JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render(compiled, &root_desc, &root, NULL, &jinja_bench_renderer,
                                   &output, &error),
                JINJA_CMETA_OK);
    check_equal(output.bytes, (size_t)JINJA_BENCH_EXPECTED_OUTPUT_BYTES);

    benchmark_bytes("compile legacy control flow", JINJA_BENCH_COMPILE_SAMPLES,
                    sizeof(source) - 1u) {
      JINJA_CMETA_TEMPLATE *candidate = jinja_cmeta_compile(vstr_from_cstr(source), NULL, NULL);
      if (candidate != NULL) {
        ++jinja_bench_sink;
        jinja_cmeta_release(candidate);
      }
    }

    benchmark_io("render legacy control flow", JINJA_BENCH_RENDER_SAMPLES, 1u,
                 JINJA_BENCH_EXPECTED_OUTPUT_BYTES) {
      output.bytes = 0u;
      if (jinja_cmeta_render(compiled, &root_desc, &root, NULL, &jinja_bench_renderer, &output,
                             NULL) == JINJA_CMETA_OK)
        jinja_bench_sink += output.bytes;
    }

    jinja_cmeta_release(compiled);
  }
}
