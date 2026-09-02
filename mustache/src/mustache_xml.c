/**
 * @file mustache_xml.c
 * @brief XML data provider implementation backed by Rocida::XmlParser
 */

#include "mustache_xml.h"

#include <xml_parser/xml_parser.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SURROGATE_LIST_TYPE 100
#define MUSTACHE_XML_PROVIDER_ERROR_CAPACITY SIZE_MAX

typedef struct {
    int _type;
    size_t count;
    void **items;
} SURROGATE_LIST;

static int xml_dump(void *node, int (*out_fn)(const char *, size_t, void *),
                    void *renderer_data, void *provider_data);
static void *xml_get_root(void *provider_data);
static void *xml_get_child_by_name(void *node, const char *name, size_t size,
                                   void *provider_data);
static void *xml_get_child_by_index(void *node, unsigned index, void *provider_data);
static MUSTACHE_TEMPLATE *xml_get_partial(const char *name, size_t size,
                                          void *provider_data);
static int xml_provider_failed(const MUSTACHE_XML_PROVIDER *provider);

static turbo_xml_node xml_node_from_raw(const void *impl) {
    const turbo_xml_node node = {impl};
    return node;
}

static int xml_view_eq(turbo_xml_string_view candidate, const char *expected,
                       size_t expected_size) {
    return candidate.size == expected_size &&
           (expected_size == 0u ||
            (candidate.data != NULL && memcmp(candidate.data, expected, expected_size) == 0));
}

int mustache_xml_provider_init(
    MUSTACHE_XML_PROVIDER *provider, void *xml_node,
    MUSTACHE_TEMPLATE *(*template_loader)(const char *, size_t, void *),
    void *user_data) {
    if (provider == NULL || xml_node == NULL) return -1;

    provider->base.dump = xml_dump;
    provider->base.get_root = xml_get_root;
    provider->base.get_child_by_name = xml_get_child_by_name;
    provider->base.get_child_by_index = xml_get_child_by_index;
    provider->base.get_partial = xml_get_partial;
    provider->base.is_lambda = NULL;
    provider->base.call_lambda = NULL;
    provider->root_node = xml_node;
    provider->template_loader = template_loader;
    provider->user_data = user_data;
    provider->allocated_lists = NULL;
    provider->list_count = 0u;
    provider->list_capacity = 0u;
    return 0;
}

void mustache_xml_provider_free(MUSTACHE_XML_PROVIDER *provider) {
    size_t index;
    if (provider == NULL) return;
    for (index = 0u; index < provider->list_count; ++index) {
        SURROGATE_LIST *list = (SURROGATE_LIST *)provider->allocated_lists[index];
        free(list->items);
        free(list);
    }
    free(provider->allocated_lists);
    provider->allocated_lists = NULL;
    provider->list_count = 0u;
    provider->list_capacity = 0u;
}

int mustache_xml_provider_status(const MUSTACHE_XML_PROVIDER *provider) {
    return provider == NULL || xml_provider_failed(provider) ? -1 : 0;
}

static int xml_provider_failed(const MUSTACHE_XML_PROVIDER *provider) {
    return provider != NULL &&
           provider->list_capacity == MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
}

static void *add_surrogate(MUSTACHE_XML_PROVIDER *provider, size_t count) {
    size_t new_capacity;
    void **new_lists;
    SURROGATE_LIST *list;

    if (provider == NULL || count == 0u || xml_provider_failed(provider)) return NULL;
    if (provider->list_count >= provider->list_capacity) {
        if (provider->list_capacity == 0u) {
            new_capacity = 8u;
        } else {
            if (provider->list_capacity > SIZE_MAX / 2u) goto allocation_failed;
            new_capacity = provider->list_capacity * 2u;
        }
        if (new_capacity > SIZE_MAX / sizeof(void *)) goto allocation_failed;
        new_lists = (void **)realloc(provider->allocated_lists,
                                     new_capacity * sizeof(void *));
        if (new_lists == NULL) goto allocation_failed;
        provider->allocated_lists = new_lists;
        provider->list_capacity = new_capacity;
    }
    if (count > SIZE_MAX / sizeof(void *)) goto allocation_failed;
    list = (SURROGATE_LIST *)malloc(sizeof(*list));
    if (list == NULL) goto allocation_failed;
    list->items = (void **)malloc(count * sizeof(void *));
    if (list->items == NULL) {
        free(list);
        goto allocation_failed;
    }
    list->_type = SURROGATE_LIST_TYPE;
    list->count = count;
    provider->allocated_lists[provider->list_count++] = list;
    return list;

allocation_failed:
    provider->list_capacity = MUSTACHE_XML_PROVIDER_ERROR_CAPACITY;
    return NULL;
}

static int is_surrogate(void *node) {
    return node != NULL && ((const SURROGATE_LIST *)node)->_type == SURROGATE_LIST_TYPE;
}

static int xml_dump(void *raw_node,
                    int (*out_fn)(const char *, size_t, void *),
                    void *renderer_data, void *provider_data) {
    turbo_xml_node node;
    turbo_xml_node_kind type;
    size_t child_count;
    size_t index;
    int result;
    (void)provider_data;

    if (raw_node == NULL) return 0;
    if (is_surrogate(raw_node)) return out_fn("[list]", 6u, renderer_data);

    node = xml_node_from_raw(raw_node);
    type = turbo_xml_node_type(node);
    if (type == TURBO_XML_ATTRIBUTE || type == TURBO_XML_TEXT) {
        const turbo_xml_string_view value = turbo_xml_node_text_view(node);
        return value.data != NULL ? out_fn(value.data, value.size, renderer_data) : 0;
    }
    if (type != TURBO_XML_ELEMENT) return 0;

    child_count = turbo_xml_node_child_count(node);
    for (index = 0u; index < child_count; ++index) {
        if (turbo_xml_node_type(turbo_xml_node_child_at(node, index)) ==
            TURBO_XML_ELEMENT)
            return out_fn("[object]", 8u, renderer_data);
    }
    for (index = 0u; index < child_count; ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_TEXT) {
            const turbo_xml_string_view value = turbo_xml_node_text_view(child);
            if (value.data != NULL) {
                result = out_fn(value.data, value.size, renderer_data);
                if (result != 0) return result;
            }
        }
    }
    return 0;
}

static void *xml_get_root(void *provider_data) {
    MUSTACHE_XML_PROVIDER *provider = (MUSTACHE_XML_PROVIDER *)provider_data;
    return provider != NULL ? (void *)provider->root_node : NULL;
}

static void *xml_get_child_by_name(void *raw_node, const char *name, size_t size,
                                   void *provider_data) {
    MUSTACHE_XML_PROVIDER *provider = (MUSTACHE_XML_PROVIDER *)provider_data;
    turbo_xml_node node;
    size_t index;
    size_t child_count;
    size_t match_count = 0u;

    if (raw_node == NULL || name == NULL || is_surrogate(raw_node) ||
        xml_provider_failed(provider))
        return NULL;
    node = xml_node_from_raw(raw_node);
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT &&
        turbo_xml_node_type(node) != TURBO_XML_DOCUMENT)
        return NULL;

    for (index = 0u; index < turbo_xml_node_attribute_count(node); ++index) {
        const turbo_xml_attribute attribute = turbo_xml_node_attribute_at(node, index);
        if (xml_view_eq(turbo_xml_attribute_qualified_name(attribute), name, size))
            return (void *)attribute.impl;
    }

    child_count = turbo_xml_node_child_count(node);
    for (index = 0u; index < child_count; ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            xml_view_eq(turbo_xml_node_qualified_name(child), name, size))
            ++match_count;
    }
    if (match_count == 1u) {
        for (index = 0u; index < child_count; ++index) {
            const turbo_xml_node child = turbo_xml_node_child_at(node, index);
            if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
                xml_view_eq(turbo_xml_node_qualified_name(child), name, size))
                return (void *)child.impl;
        }
    } else if (match_count > 1u) {
        SURROGATE_LIST *list = (SURROGATE_LIST *)add_surrogate(provider, match_count);
        size_t match_index = 0u;
        if (list == NULL) return NULL;
        for (index = 0u; index < child_count; ++index) {
            const turbo_xml_node child = turbo_xml_node_child_at(node, index);
            if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
                xml_view_eq(turbo_xml_node_qualified_name(child), name, size))
                list->items[match_index++] = (void *)child.impl;
        }
        return list;
    }
    return NULL;
}

static void *xml_get_child_by_index(void *node, unsigned index,
                                    void *provider_data) {
    (void)provider_data;
    if (node == NULL) return NULL;
    if (is_surrogate(node)) {
        SURROGATE_LIST *list = (SURROGATE_LIST *)node;
        return index < list->count ? list->items[index] : NULL;
    }
    return index == 0u ? node : NULL;
}

static MUSTACHE_TEMPLATE *xml_get_partial(const char *name, size_t size,
                                          void *provider_data) {
    MUSTACHE_XML_PROVIDER *provider = (MUSTACHE_XML_PROVIDER *)provider_data;
    return provider != NULL && provider->template_loader != NULL
               ? provider->template_loader(name, size, provider->user_data)
               : NULL;
}

int mustache_render_xml(
    const MUSTACHE_TEMPLATE *templ, void *xml_node,
    const MUSTACHE_RENDERER *renderer, void *renderer_data,
    MUSTACHE_TEMPLATE *(*template_loader)(const char *, size_t, void *),
    void *user_data) {
    MUSTACHE_XML_PROVIDER provider;
    int result;
    if (mustache_xml_provider_init(&provider, xml_node, template_loader, user_data) != 0)
        return -1;
    result = mustache_process(templ, renderer, renderer_data, &provider.base, &provider);
    if (mustache_xml_provider_status(&provider) != 0) result = -1;
    mustache_xml_provider_free(&provider);
    return result;
}
