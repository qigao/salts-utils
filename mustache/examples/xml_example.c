/**
 * @file xml_example.c
 * @brief Example of using XML data provider with Mustache
 */
#include "mustache_xml.h"
#include <xml_parser/xml_parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *xml_data = 
        "<dashboard>"
        "  <project name=\"TurboUtils\">"
        "    <version>2.0</version>"
        "    <status>Active</status>"
        "  </project>"
        "  <stats>"
        "    <item>Tests: 150</item>"
        "    <item>Coverage: 85%</item>"
        "    <item>Uptime: 99.9%</item>"
        "  </stats>"
        "</dashboard>";

    const char *template_text = 
        "Project: {{project.name}}\n"
        "Version: {{project.version}}\n"
        "Status: {{project.status}}\n"
        "\n"
        "Stats:\n"
        "{{#stats.item}}"
        " - {{.}}\n"
        "{{/stats.item}}";

    /* Parse XML */
    turbo_xml_document document = {0};
    if (turbo_xml_parse(&document, xml_data, strlen(xml_data), NULL, NULL) != TURBO_XML_OK) {
        fprintf(stderr, "Failed to parse XML\n");
        return 1;
    }

    /* Compile Template */
    MUSTACHE_TEMPLATE *templ = mustache_compile(template_text, strlen(template_text), NULL, NULL, 0);
    if (!templ) {
        fprintf(stderr, "Failed to compile template\n");
        turbo_xml_document_destroy(&document);
        return 1;
    }

    /* Setup renderer */
    MUSTACHE_STRING_RENDERER renderer = {0};
    if (mustache_string_renderer_init(&renderer) != 0) {
        fprintf(stderr, "Failed to initialize renderer\n");
        mustache_release(templ);
        turbo_xml_document_destroy(&document);
        return 1;
    }

    /* Render */
    if (mustache_render_xml(templ, (void *)turbo_xml_document_root(&document).impl,
                            &renderer.base, &renderer, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to render template\n");
        mustache_string_renderer_free(&renderer);
        mustache_release(templ);
        turbo_xml_document_destroy(&document);
        return 1;
    }

    char *result = mustache_string_renderer_get(&renderer);
    if (!result) {
        fprintf(stderr, "Failed to copy rendered output\n");
        mustache_string_renderer_free(&renderer);
        mustache_release(templ);
        turbo_xml_document_destroy(&document);
        return 1;
    }
    printf("Rendered result:\n---\n%s\n---\n", result);
    free(result);

    /* Cleanup */
    mustache_string_renderer_free(&renderer);
    mustache_release(templ);
    turbo_xml_document_destroy(&document);

    return 0;
}
