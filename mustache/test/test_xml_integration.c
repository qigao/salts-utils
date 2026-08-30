#include "tinytest.h"

#include "mustache.h"
#include "mustache_xml.h"
#include <xml_parser/xml_parser.h>

#include <stdlib.h>
#include <string.h>

static char *render_xml_text(const char *template_text, const char *xml_text) {
  turbo_xml_document document = {0};
  MUSTACHE_TEMPLATE *templ = NULL;
  MUSTACHE_STRING_RENDERER renderer = {0};
  char *result = NULL;
  int renderer_ready = 0;

  if (turbo_xml_parse(&document, xml_text, strlen(xml_text), NULL, NULL) != TURBO_XML_OK)
    return NULL;
  templ = mustache_compile(template_text, strlen(template_text), NULL, NULL, 0);
  if (!templ) goto cleanup;
  if (mustache_string_renderer_init(&renderer) != 0) goto cleanup;
  renderer_ready = 1;
  if (mustache_render_xml(templ, (void *)turbo_xml_document_root(&document).impl,
                          &renderer.base, &renderer, NULL, NULL) != 0)
    goto cleanup;
  result = mustache_string_renderer_get(&renderer);

cleanup:
  if (renderer_ready) mustache_string_renderer_free(&renderer);
  mustache_release(templ);
  turbo_xml_document_destroy(&document);
  return result;
}

spec("mustache XML integration") {
  describe("name matching") {
    it("should preserve XML element and attribute case") {
      char *result = render_xml_text("{{Foo}}|{{foo}}|{{FOO}}|{{Name}}|{{name}}",
                                     "<root Name=\"Upper\" name=\"lower\">"
                                     "<Foo>A</Foo><foo>B</foo></root>");
      check_not_null(result);
      if (result) check_equal(result, "A|B||Upper|lower");
      free(result);
    }

    it("should not merge equal local names from different namespaces") {
      char *result = render_xml_text("{{a:item}}|{{b:item}}|{{item}}",
                                     "<root xmlns:a=\"urn:a\" xmlns:b=\"urn:b\">"
                                     "<a:item>A</a:item><b:item>B</b:item></root>");
      check_not_null(result);
      if (result) check_equal(result, "A|B|");
      free(result);
    }
  }

  describe("repeated elements") {
    it("should iterate exact-name siblings") {
      char *result = render_xml_text("{{#item}}{{.}},{{/item}}",
                                     "<root><item>A</item><item>B</item></root>");
      check_not_null(result);
      if (result) check_equal(result, "A,B,");
      free(result);
    }
  }

  describe("provider status") {
    it("should reject a missing provider") {
      check_not_equal(mustache_xml_provider_status(NULL), 0);
    }

    it("should report success after initialization") {
      turbo_xml_document document = {0};
      MUSTACHE_XML_PROVIDER provider;
      check_equal(turbo_xml_parse(&document, "<root/>", strlen("<root/>"), NULL, NULL),
                  TURBO_XML_OK);
      if (document.impl) {
        check_equal(mustache_xml_provider_init(
                        &provider, (void *)turbo_xml_document_root(&document).impl,
                        NULL, NULL),
                    0);
        check_equal(mustache_xml_provider_status(&provider), 0);
        mustache_xml_provider_free(&provider);
        turbo_xml_document_destroy(&document);
      }
    }
  }
}
