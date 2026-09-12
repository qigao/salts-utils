#ifndef TEST_JINJA_NATIVE_FUNCTIONS_SUPPORT_H
#define TEST_JINJA_NATIVE_FUNCTIONS_SUPPORT_H

/* Shared test-only helpers; each native-function case owns its TinyTest main. */
#include "jinja_cmeta_internal.h"
#include "jinja_cmeta_artifact.h"
#include "jinja_cmeta_cells.h"
#include "jinja_cmeta_runtime.h"
#include "parser/jinja_template_parser.h"
#include "tinytest.h"

#include <string.h>
#include <stdlib.h>
#include <tstr.h>

static size_t test_native_cell(const JINJA_CMETA_TEMPLATE *templ, size_t owner, const char *name) {
  for (size_t i = 0u; i < templ->cell_count; ++i)
    if (templ->cells[i].owner == owner && templ->cells[i].name.len == strlen(name) &&
        memcmp(templ->cells[i].name.data, name, strlen(name)) == 0) return i;
  return SIZE_MAX;
}

static int test_native_write(const char *bytes, size_t count, void *opaque) {
  tstr *output = (tstr *)opaque;
  tstr next = tstr_cat_len(*output, bytes, count);
  if (next == NULL) return -1;
  *output = next;
  return 0;
}

static JINJA_CMETA_STATUS test_native_render(const char *source,
    const JINJA_CMETA_RENDER_OPTIONS *options, tstr *output, JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, error);
  if (templ == NULL) return error->status;
  info("native compiled expressions=%zu functions=%zu", templ->expression_count, templ->function_count);
  const JINJA_CMETA_RENDERER renderer = {test_native_write};
  vstr root = vstr_from_cstr("");
  JINJA_CMETA_STATUS status = jinja_cmeta_render(templ, jinja_cmeta_vstr_data(),
      &root, options, &renderer, output, error);
  info("native render status=%d offset=%zu output=%s", (int)status, error->offset, *output);
  jinja_cmeta_release(templ);
  return status;
}

static int test_native_reject_repr(const char *bytes, size_t count, void *opaque) {
  size_t *calls = (size_t *)opaque;
  (void)bytes;
  (void)count;
  ++*calls;
  return *calls == 1u ? 0 : -1;
}

#endif
