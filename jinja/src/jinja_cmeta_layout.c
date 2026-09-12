#include "jinja_cmeta_internal.h"
#include "jinja_cmeta_artifact.h"
#include "parser/jinja_template_parser.h"

#include <stdlib.h>
#include <string.h>

enum { JINJA_LAYOUT_FOR_PARTS = 3 };

typedef struct JINJA_LAYOUT {
  vstr source;
  const JINJA_TEMPLATE_TREE *tree;
  JINJA_CMETA_TEMPLATE *templ;
  JINJA_EXPRESSION_SCOPE *scopes;
  size_t *node_scopes;
  size_t scope_capacity;
  size_t failure;
} JINJA_LAYOUT;

static JINJA_CMETA_STATUS jinja_layout_status(JINJA_EXPRESSION_PARSE_STATUS status) {
  switch (status) {
  case JINJA_EXPRESSION_PARSE_OK: return JINJA_CMETA_OK;
  case JINJA_EXPRESSION_PARSE_CAPACITY: return JINJA_CMETA_ERR_CAPACITY;
  case JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY: return JINJA_CMETA_ERR_OUT_OF_MEMORY;
  case JINJA_EXPRESSION_PARSE_UNSUPPORTED: return JINJA_CMETA_ERR_UNSUPPORTED;
  default: return JINJA_CMETA_ERR_SYNTAX;
  }
}

static int jinja_layout_same_name(vstr left, vstr right) {
  return left.len == right.len && memcmp(left.data, right.data, left.len) == 0;
}

/* Temporary workspace shares admission with the artifact but is not retained. */
static void *jinja_layout_allocate(JINJA_LAYOUT *layout, size_t bytes, JINJA_CMETA_STATUS *status) {
  void *storage = NULL;
  const stl_allocator *allocator = &layout->templ->allocator;
  *status = jinja_cmeta_memory_status(allocator->allocate(allocator->context, bytes, &storage));
  return storage;
}

static JINJA_CMETA_STATUS jinja_layout_loop_parameter(JINJA_LAYOUT *layout, size_t opener,
                                                     JINJA_EXPRESSION_SCOPE *scope, int *implicit_loop) {
  JINJA_EXPRESSION_SPAN reference = {0};
  JINJA_CMETA_STATUS status = jinja_layout_status(jinja_template_find_undeclared_allocated(layout->source,
      layout->tree, opener, vstr_from_cstr("loop"), &reference, &layout->failure, &layout->templ->allocator));
  if (status != JINJA_CMETA_OK) return status;
  if (reference.length == 0u) {
    /* Context-bearing references and scoped blocks can read loop elsewhere.
     * Keep a real cell even when this template has no explicit loop read. */
    for (size_t i = opener + 1u; i < layout->tree->nodes[opener].match; ++i)
      if (layout->tree->nodes[i].kind == JINJA_TEMPLATE_INCLUDE ||
          layout->tree->nodes[i].kind == JINJA_TEMPLATE_IMPORT ||
          layout->tree->nodes[i].kind == JINJA_TEMPLATE_FROM ||
          (layout->tree->nodes[i].kind == JINJA_TEMPLATE_BLOCK && layout->tree->nodes[i].scoped)) {
        *implicit_loop = 1;
        break;
      }
    return JINJA_CMETA_OK;
  }
  size_t slot = 0u;
  for (; slot < scope->count; ++slot) {
    JINJA_EXPRESSION_SPAN name = scope->symbols[slot].name;
    if (jinja_layout_same_name(vstr_from_buf(scope->source.data + name.offset, name.length),
                              vstr_from_cstr("loop"))) break;
  }
  if (slot == scope->count) {
    if (scope->count == JINJA_EXPRESSION_MAX_REFERENCES) return JINJA_CMETA_ERR_CAPACITY;
    ++scope->count;
  }
  scope->symbols[slot] = (JINJA_EXPRESSION_SCOPE_SYMBOL){.name = reference,
      .load = JINJA_EXPRESSION_SCOPE_ARGUMENT, .stored = 1, .parent_symbol = SIZE_MAX};
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_layout_add_scope(JINJA_LAYOUT *layout, size_t opener,
    size_t parent, JINJA_CMETA_SCOPE_PART part, size_t owner, size_t *result) {
  const size_t index = layout->templ->lexical_scope_count;
  if (index >= layout->scope_capacity) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_EXPRESSION_SCOPE *scope = &layout->scopes[index];
  const JINJA_EXPRESSION_SCOPE *ancestor = parent == SIZE_MAX ? NULL : &layout->scopes[parent];
  const int loop = opener != SIZE_MAX && layout->tree->nodes[opener].kind == JINJA_TEMPLATE_FOR;
  JINJA_CMETA_STATUS status = jinja_layout_status(loop
      ? jinja_template_analyze_for_frame_allocated(layout->source, layout->tree, opener,
          part == JINJA_CMETA_SCOPE_TEST ? JINJA_TEMPLATE_FOR_TEST :
          part == JINJA_CMETA_SCOPE_ELSE ? JINJA_TEMPLATE_FOR_ELSE : JINJA_TEMPLATE_FOR_BODY,
          ancestor, scope, &layout->failure, &layout->templ->allocator)
      : jinja_template_analyze_frame_allocated(layout->source, layout->tree, opener,
          ancestor, scope, &layout->failure, &layout->templ->allocator));
  if (status != JINJA_CMETA_OK) return status;
  int implicit_loop = 0;
  if (loop && part == JINJA_CMETA_SCOPE_BODY) {
    status = jinja_layout_loop_parameter(layout, opener, scope, &implicit_loop);
    if (status != JINJA_CMETA_OK) return status;
  }
  layout->templ->lexical_scopes[index] = (JINJA_CMETA_LEXICAL_SCOPE){
      .parent = parent, .owner = owner, .part = part,
      .level = parent == SIZE_MAX ? 0u : layout->templ->lexical_scopes[parent].level + 1u,
      .source_offset = opener == SIZE_MAX ? 0u : layout->tree->nodes[opener].source.offset,
      .binding_count = scope->count + (size_t)implicit_loop};
  ++layout->templ->lexical_scope_count;
  *result = index;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_layout_for(JINJA_LAYOUT *layout, size_t opener, size_t parent) {
  const JINJA_TEMPLATE_NODE *node = &layout->tree->nodes[opener];
  JINJA_CMETA_STATUS status;
  JINJA_TEMPLATE_FOR_HEADER *header = (JINJA_TEMPLATE_FOR_HEADER *)jinja_layout_allocate(
      layout, sizeof(*header), &status);
  if (status != JINJA_CMETA_OK) return status;
  size_t relative = 0u;
  status = jinja_layout_status(jinja_expression_parse_for_header_allocated(
      vstr_from_buf(layout->source.data + node->header.offset, node->header.length), header, &relative,
      &layout->templ->allocator));
  const int recursive = status == JINJA_CMETA_OK && header->recursive;
  const int filtered = status == JINJA_CMETA_OK && header->has_test;
  layout->templ->allocator.deallocate(layout->templ->allocator.context, header, sizeof(*header));
  if (status != JINJA_CMETA_OK) { layout->failure = node->header.offset + relative; return status; }
  size_t body, unused;
  const size_t owner = recursive ? layout->templ->lexical_scope_count : layout->templ->lexical_scopes[parent].owner;
  status = jinja_layout_add_scope(layout, opener, parent, JINJA_CMETA_SCOPE_BODY, owner, &body);
  if (status != JINJA_CMETA_OK) return status;
  layout->node_scopes[opener] = body;
  if (filtered) {
    status = jinja_layout_add_scope(layout, opener, parent, JINJA_CMETA_SCOPE_TEST,
        layout->templ->lexical_scope_count, &unused);
    if (status != JINJA_CMETA_OK) return status;
  }
  if (node->branch != SIZE_MAX)
    status = jinja_layout_add_scope(layout, opener, parent, JINJA_CMETA_SCOPE_ELSE,
        owner, &layout->node_scopes[node->branch]);
  return status;
}

static JINJA_CMETA_STATUS jinja_layout_frames(JINJA_LAYOUT *layout) {
  size_t root;
  JINJA_CMETA_STATUS status = jinja_layout_add_scope(layout, SIZE_MAX, SIZE_MAX,
      JINJA_CMETA_SCOPE_BODY, 0u, &root);
  if (status != JINJA_CMETA_OK) return status;
  for (size_t i = 0u; i < layout->tree->count; ++i) {
    const JINJA_TEMPLATE_NODE *node = &layout->tree->nodes[i];
    if (layout->node_scopes[i] != SIZE_MAX) continue;
    size_t parent = node->parent == SIZE_MAX ? root : layout->node_scopes[node->parent];
    if (parent == SIZE_MAX) return JINJA_CMETA_ERR_METADATA;
    layout->failure = node->source.offset;
    switch (node->kind) {
    case JINJA_TEMPLATE_BLOCK:
      status = jinja_layout_add_scope(layout, i, SIZE_MAX, JINJA_CMETA_SCOPE_BODY,
          layout->templ->lexical_scope_count, &layout->node_scopes[i]);
      break;
    case JINJA_TEMPLATE_FOR:
      status = jinja_layout_for(layout, i, parent);
      break;
    case JINJA_TEMPLATE_MACRO:
    case JINJA_TEMPLATE_CALL:
    case JINJA_TEMPLATE_WITH:
    case JINJA_TEMPLATE_CAPTURE:
    case JINJA_TEMPLATE_FILTER:
    case JINJA_TEMPLATE_AUTOESCAPE:
      status = jinja_layout_add_scope(layout, i, parent, JINJA_CMETA_SCOPE_BODY,
          node->kind == JINJA_TEMPLATE_MACRO || node->kind == JINJA_TEMPLATE_CALL
              ? layout->templ->lexical_scope_count : layout->templ->lexical_scopes[parent].owner,
          &layout->node_scopes[i]);
      break;
    default:
      layout->node_scopes[i] = parent;
      break;
    }
    if (status != JINJA_CMETA_OK) return status;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_layout_cells(JINJA_LAYOUT *layout) {
  JINJA_CMETA_TEMPLATE *templ = layout->templ;
  size_t total = 0u, bytes = 0u;
  for (size_t i = 0u; i < templ->lexical_scope_count; ++i) {
    if (templ->lexical_scopes[i].binding_count > JINJA_CMETA_MAX_LEXICAL_CELLS - total) {
      layout->failure = templ->lexical_scopes[i].source_offset;
      return JINJA_CMETA_ERR_CAPACITY;
    }
    total += templ->lexical_scopes[i].binding_count;
  }
  if (total == 0u) return JINJA_CMETA_OK;
  templ->cells = (JINJA_CMETA_CELL *)jinja_cmeta_artifact_zero(templ, total, sizeof(*templ->cells));
  templ->cell_bindings = (JINJA_CMETA_CELL_BINDING *)jinja_cmeta_artifact_zero(templ, total, sizeof(*templ->cell_bindings));
  if (templ->cells == NULL || templ->cell_bindings == NULL) return templ->allocation_status;
  for (size_t i = 0u; i < templ->lexical_scope_count; ++i) {
    JINJA_CMETA_LEXICAL_SCOPE *frame = &templ->lexical_scopes[i];
    frame->first_binding = templ->cell_binding_count;
    for (size_t j = 0u; j < frame->binding_count; ++j) {
      const JINJA_EXPRESSION_SCOPE_SYMBOL *symbol = j < layout->scopes[i].count ? &layout->scopes[i].symbols[j] : NULL;
      vstr name = symbol != NULL ? vstr_from_buf(layout->source.data + symbol->name.offset, symbol->name.length)
                                : vstr_from_cstr("loop");
      size_t cell = 0u;
      for (; cell < templ->cell_count; ++cell)
        if (templ->cells[cell].owner == frame->owner && templ->cells[cell].level == frame->level &&
            jinja_layout_same_name(templ->cells[cell].name, name)) break;
      if (cell == templ->cell_count) {
        if (name.len > JINJA_CMETA_MAX_PROGRAM_BYTES - bytes) {
          layout->failure = symbol != NULL ? symbol->name.offset : frame->source_offset;
          return JINJA_CMETA_ERR_CAPACITY;
        }
        bytes += name.len;
        size_t slot = templ->lexical_scopes[frame->owner].cell_count++;
        templ->cells[templ->cell_count++] = (JINJA_CMETA_CELL){
            .owner = frame->owner, .level = frame->level, .name = name, .slot = slot};
      }
      JINJA_CMETA_CELL_BINDING binding = {.cell = cell, .source_cell = SIZE_MAX};
      switch (symbol != NULL ? symbol->load : JINJA_EXPRESSION_SCOPE_ARGUMENT) {
      case JINJA_EXPRESSION_SCOPE_ARGUMENT: binding.load = JINJA_CMETA_CELL_ARGUMENT; break;
      case JINJA_EXPRESSION_SCOPE_RESOLVE: binding.load = JINJA_CMETA_CELL_RESOLVE; break;
      case JINJA_EXPRESSION_SCOPE_UNDEFINED: binding.load = JINJA_CMETA_CELL_UNDEFINED; break;
      case JINJA_EXPRESSION_SCOPE_ALIAS: {
        binding.load = JINJA_CMETA_CELL_ALIAS;
        size_t ancestor = i;
        for (size_t k = 0u; k < symbol->parent_depth; ++k) ancestor = templ->lexical_scopes[ancestor].parent;
        binding.source_cell = templ->cell_bindings[
            templ->lexical_scopes[ancestor].first_binding + symbol->parent_symbol].cell;
        break;
      }
      }
      templ->cell_bindings[templ->cell_binding_count++] = binding;
    }
  }
  templ->cell_strings = (char *)jinja_cmeta_artifact_allocate(templ, bytes, sizeof(char));
  if (templ->cell_strings == NULL) return templ->allocation_status;
  size_t cursor = 0u;
  for (size_t i = 0u; i < templ->cell_count; ++i) {
    memcpy(templ->cell_strings + cursor, templ->cells[i].name.data, templ->cells[i].name.len);
    templ->cells[i].name.data = templ->cell_strings + cursor;
    cursor += templ->cells[i].name.len;
  }
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_build_layout(vstr source, const JINJA_TEMPLATE_TREE *tree,
    JINJA_CMETA_TEMPLATE *templ, JINJA_CMETA_ERROR *error) {
  if (!vstr_is_valid(source) || tree == NULL || templ == NULL || tree->count > JINJA_TEMPLATE_MAX_NODES ||
      templ->allocator.allocate == NULL || templ->allocator.deallocate == NULL)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  size_t capacity = 1u;
  /* Literal and branch nodes inherit frames; only these statements create them.
   * FOR reserves at most body/test/else, without parsing its header twice. */
  for (size_t i = 0u; i < tree->count; ++i) {
    size_t addition = 0u;
    switch (tree->nodes[i].kind) {
    case JINJA_TEMPLATE_FOR: addition = JINJA_LAYOUT_FOR_PARTS; break;
    case JINJA_TEMPLATE_BLOCK: case JINJA_TEMPLATE_MACRO: case JINJA_TEMPLATE_CALL: case JINJA_TEMPLATE_WITH:
    case JINJA_TEMPLATE_CAPTURE: case JINJA_TEMPLATE_FILTER: case JINJA_TEMPLATE_AUTOESCAPE:
      addition = 1u; break;
    default: break;
    }
    if (addition > SIZE_MAX - capacity) return JINJA_CMETA_ERR_CAPACITY;
    capacity += addition;
  }
  if (capacity > SIZE_MAX / sizeof(JINJA_EXPRESSION_SCOPE) ||
      capacity > SIZE_MAX / sizeof(JINJA_CMETA_LEXICAL_SCOPE) ||
      tree->count > SIZE_MAX / sizeof(size_t)) return JINJA_CMETA_ERR_CAPACITY;
  JINJA_LAYOUT layout = {.source = source, .tree = tree, .templ = templ};
  layout.scope_capacity = capacity;
  JINJA_CMETA_STATUS status = JINJA_CMETA_OK;
  if (tree->count != 0u) {
    layout.node_scopes = (size_t *)jinja_layout_allocate(&layout,
        tree->count * sizeof(*layout.node_scopes), &status);
    if (status != JINJA_CMETA_OK) goto done;
  }
  layout.scopes = (JINJA_EXPRESSION_SCOPE *)jinja_layout_allocate(&layout,
      capacity * sizeof(*layout.scopes), &status);
  if (status != JINJA_CMETA_OK) goto done;
  memset(layout.scopes, 0, capacity * sizeof(*layout.scopes));
  templ->lexical_scopes = (JINJA_CMETA_LEXICAL_SCOPE *)jinja_cmeta_artifact_zero(templ, capacity, sizeof(*templ->lexical_scopes));
  status = templ->allocation_status;
  if (status != JINJA_CMETA_OK) goto done;
  for (size_t i = 0u; i < tree->count; ++i) layout.node_scopes[i] = SIZE_MAX;
  status = jinja_layout_frames(&layout);
  if (status != JINJA_CMETA_OK) goto done;
  status = jinja_layout_cells(&layout);
  if (status != JINJA_CMETA_OK) goto done;
  for (size_t i = 0u; i < templ->function_count; ++i) {
    size_t offset = templ->instructions[templ->functions[i].body_begin - 1u].source_offset;
    size_t frame = 1u;
    for (; frame < templ->lexical_scope_count; ++frame)
      if (templ->lexical_scopes[frame].source_offset == offset &&
          templ->lexical_scopes[frame].part == JINJA_CMETA_SCOPE_BODY) break;
    if (frame == templ->lexical_scope_count) { status = JINJA_CMETA_ERR_METADATA; goto done; }
    templ->functions[i].scope = frame;
  }
done:
  if (layout.node_scopes != NULL)
    templ->allocator.deallocate(templ->allocator.context, layout.node_scopes,
        tree->count * sizeof(*layout.node_scopes));
  if (layout.scopes != NULL)
    templ->allocator.deallocate(templ->allocator.context, layout.scopes,
        capacity * sizeof(*layout.scopes));
  if (status != JINJA_CMETA_OK)
    jinja_cmeta_error_set(error, status, layout.failure, "unable to build lexical cell layout");
  return status;
}
