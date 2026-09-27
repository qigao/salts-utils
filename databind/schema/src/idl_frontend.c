#include "idl.h"
#include "schema_enum.h"
#include "schema_service.h"
#include "schema_channel.h"
#include "schema_component.h"
#include "schema_lexer.h"
#include "schema_types.h"
#include "schema_grammar_gen.h"
#include "tbe_error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#define SCHEMA_MAX_TEXT_BYTES (10u * 1024u * 1024u)

/*
 * Forward declarations for the lemon-generated parser.
 * The generated code provides these functions:
 *   void *SchemaParseAlloc(void *(*)(size_t));
 *   void  SchemaParseFree(void *, void (*)(void *));
 *   void  SchemaParse(void *, int, schema_token_t, schema_parse_ctx_t *);
 */
void *SchemaParseAlloc(void *(*)(size_t));
void  SchemaParseFree(void *, void (*)(void *));
void  SchemaParse(void *, int, schema_token_t, schema_parse_ctx_t *);

static int is_named_child(const Node *node, const char *name) {
    return node && node->name && strcmp(node->name, name) == 0;
}

static void map_remove_named_children(Node *map, const char *name) {
    size_t out = 0;

    if (!map || map->type != NODE_MAP) {
        return;
    }

    for (size_t i = 0; i < map->data.map.count; ++i) {
        Node *child = map->data.map.items[i];
        if (is_named_child(child, name)) {
            node_free(child);
            continue;
        }
        map->data.map.items[out++] = child;
    }

    map->data.map.count = out;
}

static Node *map_take_named_child(Node *map, const char *name) {
    if (!map || map->type != NODE_MAP) {
        return NULL;
    }

    for (size_t i = 0; i < map->data.map.count; ++i) {
        Node *child = map->data.map.items[i];
        if (!is_named_child(child, name)) {
            continue;
        }

        if (i + 1 < map->data.map.count) {
            memmove(&map->data.map.items[i], &map->data.map.items[i + 1],
                    (map->data.map.count - i - 1) * sizeof(Node *));
        }
        map->data.map.count--;
        return child;
    }

    return NULL;
}

static Node *parse_schema_raw(const char *text, size_t len, tbe_error_t *err) {
    Node *temp_root;
    Node *schema_node;
    Node *messages_list;
    Node *composites_list;
    Node *groups_list;
    Node *enums_list;
    Node *services_list;
    Node *channels_list;
    Node *components_list;
    Node *unions_list;

    temp_root = create_node_map(NULL);
    schema_node = NULL;
    messages_list = create_node_list("messages");
    composites_list = create_node_list("composites");
    groups_list = create_node_list("groups");
    enums_list = create_node_list("enums");
    unions_list = create_node_list("unions");
    services_list = create_node_list("services");
    channels_list = create_node_list("channels");
    components_list = create_node_list("components");
    if (!temp_root || !messages_list || !composites_list || !groups_list ||
        !enums_list || !unions_list || !services_list || !channels_list ||
        !components_list) {
        node_free(components_list);
        node_free(channels_list);
        node_free(services_list);
        node_free(unions_list);
        node_free(enums_list);
        node_free(groups_list);
        node_free(composites_list);
        node_free(messages_list);
        node_free(temp_root);
        if (err) {
            tbe_error_set(err, TBE_ERR_OUT_OF_MEMORY, -1, -1, "Failed to allocate node structures");
        }
        return NULL;
    }

    {
        Node *root_lists[] = {
            messages_list, composites_list, groups_list, enums_list,
            unions_list, services_list, channels_list, components_list
        };
        size_t list_count = sizeof(root_lists) / sizeof(root_lists[0]);
        size_t i;

        for (i = 0u; i < list_count; ++i) {
            if (map_add(temp_root, root_lists[i]) != 0) {
                size_t j;
                for (j = i; j < list_count; ++j) node_free(root_lists[j]);
                node_free(temp_root);
                if (err) {
                    tbe_error_set(
                        err, TBE_ERR_OUT_OF_MEMORY, -1, -1,
                        "Failed to add child nodes");
                }
                return NULL;
            }
        }
    }

    schema_parse_ctx_t ctx = {0};
    ctx.root         = temp_root;
    ctx.schema_node  = schema_node;
    ctx.messages_list = messages_list;
    ctx.composites_list = composites_list;
    ctx.groups_list = groups_list;
    ctx.enums_list   = enums_list;
    ctx.unions_list  = unions_list;
    ctx.services_list = services_list;
    ctx.channels_list = channels_list;
    ctx.components_list = components_list;
    ctx.cur_service = NULL;
    ctx.cur_operations = NULL;
    ctx.cur_component = NULL;
    ctx.cur_component_capabilities = NULL;
    ctx.cur_record   = NULL;
    ctx.cur_fields   = NULL;
    ctx.cur_enum     = NULL;
    ctx.cur_enum_items = NULL;
    ctx.cur_record_kind = SCHEMA_RECORD_COMPOSITE;
    ctx.cur_field_section = SCHEMA_FIELD_SECTION_FIXED;
    ctx.error        = 0;

    schema_lexer_t lexer;
    schema_lexer_init(&lexer, text, len);

    void *parser = SchemaParseAlloc(malloc);
    if (!parser) {
        node_free(temp_root);
        if (err) {
            tbe_error_set(err, TBE_ERR_OUT_OF_MEMORY, -1, -1, "Failed to allocate parser");
        }
        return NULL;
    }

    schema_token_t tok;
    int rc;
    while ((rc = schema_lexer_next(&lexer, &tok)) > 0) {
        SchemaParse(parser, tok.type, tok, &ctx);
        if (ctx.error) break;
    }

    if (rc < 0) {
        if (err) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Lexer error at line %d", lexer.line);
            tbe_error_set(err, TBE_ERR_LEXER_ERROR, lexer.line, -1, msg);
        }
        ctx.error = 1;
    }

    if (!ctx.error) {
        schema_token_t eof_tok = {0};
        SchemaParse(parser, 0, eof_tok, &ctx);
    }

    SchemaParseFree(parser, free);
    if (ctx.error) {
        node_free(temp_root);
        if (err) {
            if (ctx.error_msg[0] != '\0') {
                tbe_error_set(err, TBE_ERR_SYNTAX_ERROR, ctx.error_line, ctx.error_column,
                              ctx.error_msg);
            } else if (err->code == TBE_OK) {
                tbe_error_set(err, TBE_ERR_SYNTAX_ERROR, -1, -1, "Parse error");
            }
        }
        return NULL;
    }

    return temp_root;
}

static int merge_schema_into_root(Node *root, Node *parsed) {
    Node *generated_children[9];
    const char *generated_names[] = {
        "schema", "messages", "composites", "groups",
        "enums", "unions", "services", "channels", "components"
    };

    for (size_t i = 0; i < sizeof(generated_children) / sizeof(generated_children[0]); ++i) {
        generated_children[i] = map_take_named_child(parsed, generated_names[i]);
        if (i > 0 && !generated_children[i]) {
            for (size_t j = 0; j < i; ++j) {
                node_free(generated_children[j]);
            }
            return -1;
        }
    }

    for (size_t i = 0; i < sizeof(generated_children) / sizeof(generated_children[0]); ++i) {
        map_remove_named_children(root, generated_names[i]);
        if (generated_children[i]) {
            if ((strcmp(generated_names[i], "services") == 0 ||
                 strcmp(generated_names[i], "channels") == 0 ||
                 strcmp(generated_names[i], "components") == 0) &&
                generated_children[i]->type == NODE_LIST &&
                generated_children[i]->data.list.count == 0u) {
                node_free(generated_children[i]);
                generated_children[i] = NULL;
                continue;
            }
            if (map_add(root, generated_children[i]) != 0) {
                node_free(generated_children[i]);
                for (size_t j = i + 1; j < sizeof(generated_children) / sizeof(generated_children[0]); ++j) {
                    node_free(generated_children[j]);
                }
                return -1;
            }
        }
    }
    return 0;
}

int idl_parse(const char *text, size_t len, Node *root, tbe_error_t *err) {
    if (err) {
        tbe_error_init(err);
    }

    if (!text || !root || root->type != NODE_MAP) {
        if (err) {
            tbe_error_set(err, TBE_ERR_INVALID_ARGUMENT, -1, -1, "Invalid arguments to idl_parse");
        }
        return -1;
    }
    
    // Check for unreasonably large input (prevent DoS)
    if (len > SCHEMA_MAX_TEXT_BYTES) {
        if (err) {
            tbe_error_set(err, TBE_ERR_INVALID_ARGUMENT, -1, -1, "IDL text too large");
        }
        return -1;
    }

    Node *parsed = parse_schema_raw(text, len, err);
    if (!parsed) {
        return -1;
    }

    if (schema_validate_enums(parsed, err) != 0) {
        node_free(parsed);
        return -1;
    }

    if (!schema_validate_services(parsed, err)) {
        node_free(parsed);
        return -1;
    }

    if (!schema_validate_channels(parsed, err)) {
        node_free(parsed);
        return -1;
    }

    if (!schema_validate_components(parsed, err)) {
        node_free(parsed);
        return -1;
    }

    int result = merge_schema_into_root(root, parsed);
    node_free(parsed);

    if (result != 0 && err && err->code == TBE_OK) {
        tbe_error_set(err, TBE_ERR_OUT_OF_MEMORY, -1, -1, "Failed to merge schema into root");
    }

    return result;
}
