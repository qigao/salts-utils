/* Regression for a private static XML engine embedded in multiple real DSOs. */
#include <xml_parser/xml_parser.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

enum { RELOAD_CYCLES = 4 };
typedef salts_xml_status (*parse_fn)(salts_xml_document *, const char *, size_t,
                                    const salts_xml_limits *, salts_xml_diagnostic *);
typedef void (*destroy_fn)(salts_xml_document *);
typedef salts_xml_node (*root_fn)(const salts_xml_document *);
typedef salts_xml_string_view (*name_fn)(salts_xml_node);

static int check_xml(void *library) {
    static const char xml[] = "<root><value>hello</value></root>";
    salts_xml_document document = {0};
    salts_xml_diagnostic diagnostic = {0};
    parse_fn parse;
    destroy_fn destroy;
    root_fn root;
    name_fn name;
    void *symbol;
    int failed = 0;
#define LOAD(function, exported_name) do { \
    _Static_assert(sizeof(function) == sizeof(symbol), "POSIX dlsym representation"); \
    dlerror(); symbol = dlsym(library, exported_name); \
    if (dlerror() != NULL || symbol == NULL) { \
        fprintf(stderr, "Missing public XML function: %s\n", exported_name); return 1; \
    } \
    memcpy(&(function), &symbol, sizeof(function)); \
} while (0)
    LOAD(parse, "salts_xml_parse");
    LOAD(destroy, "salts_xml_document_destroy");
    LOAD(root, "salts_xml_document_root");
    LOAD(name, "salts_xml_node_qualified_name");
#undef LOAD
    if (parse(&document, xml, sizeof(xml) - 1, NULL, &diagnostic) != SALTS_XML_OK) {
        fprintf(stderr, "Public XML parse failed: %s\n", diagnostic.message);
        failed = 1;
    } else {
        salts_xml_string_view text = name(root(&document));
        if (text.size != 4 || text.data == NULL || memcmp(text.data, "root", 4) != 0) {
            fputs("Public XML root did not round-trip\n", stderr);
            failed = 1;
        }
    }
    destroy(&document);
    return failed;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    for (int cycle = 0; cycle < RELOAD_CYCLES; ++cycle) {
        void *libraries[2] = {NULL, NULL};
        int failed = 0;
        for (int i = 0; i < 2; ++i) {
            /* GLOBAL matches startup DT_NEEDED interposition, unlike LOCAL. */
            libraries[i] = dlopen(argv[i + 1], RTLD_NOW | RTLD_GLOBAL);
            if (libraries[i] == NULL) {
                fprintf(stderr, "Cannot load %s: %s\n", argv[i + 1], dlerror());
                failed = 1;
                break;
            }
        }
        for (int i = 0; i < 2 && !failed; ++i) {
            dlerror();
            if (dlsym(libraries[i], "cxml_find_attribute_list") != NULL) {
                fputs("Private cxml global leaked into the public symbol namespace\n", stderr);
                failed = 1;
                break;
            }
            (void)dlerror();
            failed = check_xml(libraries[i]);
        }
        for (int i = 1; i >= 0; --i) {
            if (libraries[i] != NULL && dlclose(libraries[i]) != 0) {
                fprintf(stderr, "Cannot unload library: %s\n", dlerror());
                failed = 1;
            }
        }
        if (failed) return 1;
    }
    puts("Private XML engine: both real libraries parsed successfully across four load/unload cycles");
    return 0;
}
