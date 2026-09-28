#include "idl_contract_internal.h"
#include "idl.h"

#include <stdlib.h>
#include <string.h>

static const Node *child(const Node *parent, const char *name) {
  size_t i;
  const Node *const *items;
  size_t count;
  if (parent == NULL || name == NULL) return NULL;
  if (parent->type == NODE_MAP || parent->type == NODE_ROOT) {
    items = (const Node *const *)parent->data.map.items;
    count = parent->data.map.count;
  } else {
    return NULL;
  }
  for (i = 0u; i < count; ++i)
    if (items[i] != NULL && items[i]->name != NULL &&
        strcmp(items[i]->name, name) == 0)
      return items[i];
  return NULL;
}

static const Node *list_child(const Node *parent, const char *name) {
  const Node *node = child(parent, name);
  return node != NULL && node->type == NODE_LIST ? node : NULL;
}

static const char *text_child(const Node *parent, const char *name) {
  const Node *node = child(parent, name);
  return node != NULL && node->type == NODE_STRING
             ? node->data.string_val
             : NULL;
}

static int flag_child(const Node *parent, const char *name) {
  return child(parent, name) != NULL;
}

static char *copy_text(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  copy = (char *)malloc(length + 1u);
  if (copy != NULL) memcpy(copy, text, length + 1u);
  return copy;
}

static void diagnostic_set(
    IdlDiagnostic *diagnostic, IdlStatus status,
    int line, int column, const char *message) {
  size_t size;
  if (diagnostic == NULL ||
      diagnostic->size < sizeof(*diagnostic) ||
      diagnostic->abi_version != IDL_DIAGNOSTIC_ABI_VERSION)
    return;
  size = diagnostic->size;
  *diagnostic = (IdlDiagnostic)IDL_DIAGNOSTIC_INIT;
  diagnostic->size = size;
  diagnostic->status = status;
  diagnostic->line = line;
  diagnostic->column = column;
  if (message != NULL) {
    strncpy(diagnostic->message, message, sizeof(diagnostic->message) - 1u);
    diagnostic->message[sizeof(diagnostic->message) - 1u] = '\0';
  }
}

static IdlStatus diagnostic_status_from_tbe(tbe_error_code_t code) {
  switch (code) {
  case TBE_OK: return IDL_OK;
  case TBE_ERR_INVALID_ARGUMENT: return IDL_INVALID_ARGUMENT;
  case TBE_ERR_OUT_OF_MEMORY: return IDL_NO_MEMORY;
  case TBE_ERR_LEXER_ERROR: return IDL_LEXER_ERROR;
  case TBE_ERR_SYNTAX_ERROR: return IDL_SYNTAX_ERROR;
  case TBE_ERR_SEMANTIC_ERROR: return IDL_SEMANTIC_ERROR;
  default: return IDL_SEMANTIC_ERROR;
  }
}

static void annotation_destroy(IdlAnnotation *items, size_t count) {
  size_t i, j;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    free((void *)items[i].value);
    for (j = 0u; j < items[i].argument_count; ++j)
      free((void *)items[i].arguments[j]);
    free((void *)items[i].arguments);
  }
  free(items);
}

static int annotation_build(
    const Node *owner, IdlAnnotation **out_items, size_t *out_count) {
  const Node *attrs = list_child(owner, "attributes");
  IdlAnnotation *items;
  size_t i;
  *out_items = NULL;
  *out_count = 0u;
  if (attrs == NULL || attrs->data.list.count == 0u) return 1;
  items = (IdlAnnotation *)calloc(attrs->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < attrs->data.list.count; ++i) {
    const Node *attr = attrs->data.list.items[i];
    const Node *values = list_child(attr, "values");
    const char *name = text_child(attr, "name");
    const char *value = text_child(attr, "value");
    size_t j;

    items[i].name = copy_text(name != NULL ? name : "");
    items[i].value = copy_text(value != NULL ? value : "");
    items[i].bare = flag_child(attr, "bare");

    if (values != NULL && values->data.list.count != 0u) {
      char **arguments = (char **)calloc(
          values->data.list.count, sizeof(*arguments));
      if (arguments == NULL) {
        annotation_destroy(items, attrs->data.list.count);
        return 0;
      }
      items[i].arguments = (const char *const *)arguments;
      items[i].argument_count = values->data.list.count;
      for (j = 0u; j < values->data.list.count; ++j) {
        const Node *argument = values->data.list.items[j];
        const char *text =
            argument != NULL && argument->type == NODE_STRING
                ? argument->data.string_val
                : NULL;
        if (text == NULL || (arguments[j] = copy_text(text)) == NULL) {
          annotation_destroy(items, attrs->data.list.count);
          return 0;
        }
      }
    }

    if (items[i].name == NULL || items[i].value == NULL) {
      annotation_destroy(items, attrs->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = attrs->data.list.count;
  return 1;
}

static void constraint_destroy(IdlConstraint *items, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].kind);
    free((void *)items[i].value);
    free((void *)items[i].minimum);
    free((void *)items[i].maximum);
    free((void *)items[i].pattern);
  }
  free(items);
}

static int constraint_build(
    const Node *field, IdlConstraint **out_items, size_t *out_count) {
  const Node *constraints = list_child(field, "constraints");
  IdlConstraint *items;
  size_t i;
  *out_items = NULL;
  *out_count = 0u;
  if (constraints == NULL || constraints->data.list.count == 0u) return 1;
  items = (IdlConstraint *)calloc(
      constraints->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < constraints->data.list.count; ++i) {
    const Node *item = constraints->data.list.items[i];
    const char *kind = text_child(item, "kind");
    const char *value = text_child(item, "value");
    const char *minimum = text_child(item, "min");
    const char *maximum = text_child(item, "max");
    const char *pattern = text_child(item, "pattern");
    items[i].kind = copy_text(kind != NULL ? kind : "");
    if (value != NULL) items[i].value = copy_text(value);
    if (minimum != NULL) items[i].minimum = copy_text(minimum);
    if (maximum != NULL) items[i].maximum = copy_text(maximum);
    if (pattern != NULL) items[i].pattern = copy_text(pattern);
    if (items[i].kind == NULL ||
        (value != NULL && items[i].value == NULL) ||
        (minimum != NULL && items[i].minimum == NULL) ||
        (maximum != NULL && items[i].maximum == NULL) ||
        (pattern != NULL && items[i].pattern == NULL)) {
      constraint_destroy(items, constraints->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = constraints->data.list.count;
  return 1;
}

static IdlCollectionKind collection_kind(const Node *field) {
  const char *kind;
  if (flag_child(field, "is_group_field")) return IDL_COLLECTION_GROUP;
  if (!flag_child(field, "is_collection")) return IDL_COLLECTION_NONE;
  kind = text_child(field, "collection_kind");
  if (kind == NULL) return IDL_COLLECTION_NONE;
  if (strcmp(kind, "array") == 0) return IDL_COLLECTION_ARRAY;
  if (strcmp(kind, "list") == 0) return IDL_COLLECTION_LIST;
  if (strcmp(kind, "set") == 0) return IDL_COLLECTION_SET;
  if (strcmp(kind, "map") == 0) return IDL_COLLECTION_MAP;
  return IDL_COLLECTION_NONE;
}

static void field_destroy(IdlField *fields, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i) {
    free((void *)fields[i].name);
    free((void *)fields[i].type_name);
    free((void *)fields[i].inner_type);
    free((void *)fields[i].key_type);
    free((void *)fields[i].value_type);
    free((void *)fields[i].length);
    free((void *)fields[i].default_value);
    constraint_destroy((IdlConstraint *)fields[i].constraints,
                       fields[i].constraint_count);
    annotation_destroy((IdlAnnotation *)fields[i].annotations,
                       fields[i].annotation_count);
  }
  free(fields);
}

static int field_build_list(
    const Node *record, IdlField **out_fields, size_t *out_count) {
  const Node *fields = list_child(record, "fields");
  IdlField *result;
  size_t i;
  *out_fields = NULL;
  *out_count = 0u;
  if (fields == NULL || fields->data.list.count == 0u) return 1;
  result = (IdlField *)calloc(fields->data.list.count, sizeof(*result));
  if (result == NULL) return 0;
  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = text_child(field, "name");
    const char *type = text_child(field, "type");
    const char *inner = text_child(field, "inner_type");
    const char *key = text_child(field, "key_type");
    const char *value = text_child(field, "value_type");
    const char *length = text_child(field, "length_field");
    const char *def = text_child(field, "default_value");
    IdlConstraint *constraints = NULL;
    IdlAnnotation *annotations = NULL;
    size_t constraint_count = 0u;
    size_t annotation_count = 0u;
    result[i].name = copy_text(name != NULL ? name : "");
    result[i].type_name = copy_text(type != NULL ? type : "");
    result[i].collection_kind = collection_kind(field);
    if (inner != NULL) result[i].inner_type = copy_text(inner);
    if (key != NULL) result[i].key_type = copy_text(key);
    if (value != NULL) result[i].value_type = copy_text(value);
    if (length != NULL) result[i].length = copy_text(length);
    if (def != NULL) result[i].default_value = copy_text(def);
    result[i].optional = flag_child(field, "is_optional");
    result[i].nullable = flag_child(field, "is_nullable");
    if (!constraint_build(field, &constraints, &constraint_count) ||
        !annotation_build(field, &annotations, &annotation_count)) {
      constraint_destroy(constraints, constraint_count);
      annotation_destroy(annotations, annotation_count);
      field_destroy(result, fields->data.list.count);
      return 0;
    }
    result[i].constraints = constraints;
    result[i].constraint_count = constraint_count;
    result[i].annotations = annotations;
    result[i].annotation_count = annotation_count;
    if (result[i].name == NULL || result[i].type_name == NULL ||
        (inner != NULL && result[i].inner_type == NULL) ||
        (key != NULL && result[i].key_type == NULL) ||
        (value != NULL && result[i].value_type == NULL) ||
        (length != NULL && result[i].length == NULL) ||
        (def != NULL && result[i].default_value == NULL)) {
      field_destroy(result, fields->data.list.count);
      return 0;
    }
  }
  *out_fields = result;
  *out_count = fields->data.list.count;
  return 1;
}

static const char *decl_name(const Node *node) {
  const char *name = text_child(node, "name");
  if (name != NULL) return name;
  name = text_child(node, "message_name");
  if (name != NULL) return name;
  name = text_child(node, "enum_name");
  if (name != NULL) return name;
  return text_child(node, "union_name");
}

static void enum_item_destroy(IdlEnumItem *items, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    free((void *)items[i].value);
  }
  free(items);
}

static int enum_item_build(
    const Node *owner, IdlEnumItem **out_items, size_t *out_count) {
  const Node *items_node = list_child(owner, "items");
  IdlEnumItem *items;
  size_t i;
  *out_items = NULL;
  *out_count = 0u;
  if (items_node == NULL || items_node->data.list.count == 0u) return 1;
  items = (IdlEnumItem *)calloc(items_node->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < items_node->data.list.count; ++i) {
    const char *name = text_child(items_node->data.list.items[i], "name");
    const char *value = text_child(items_node->data.list.items[i], "value");
    items[i].name = copy_text(name != NULL ? name : "");
    items[i].value = copy_text(value != NULL ? value : "");
    if (items[i].name == NULL || items[i].value == NULL) {
      enum_item_destroy(items, items_node->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = items_node->data.list.count;
  return 1;
}

static void data_destroy(IdlDataDecl *items, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    free((void *)items[i].underlying_type);
    field_destroy((IdlField *)items[i].fields, items[i].field_count);
    enum_item_destroy((IdlEnumItem *)items[i].enum_items,
                      items[i].enum_item_count);
    annotation_destroy((IdlAnnotation *)items[i].annotations,
                       items[i].annotation_count);
  }
  free(items);
}

static size_t list_count(const Node *root, const char *name) {
  const Node *list = list_child(root, name);
  return list != NULL ? list->data.list.count : 0u;
}

static int data_build(
    const Node *root, IdlDataDecl **out_items, size_t *out_count) {
  static const struct {
    const char *list;
    IdlDataKind kind;
  } groups[] = {
      {"messages", IDL_DATA_MESSAGE},
      {"composites", IDL_DATA_COMPOSITE},
      {"groups", IDL_DATA_GROUP},
      {"enums", IDL_DATA_ENUM},
      {"unions", IDL_DATA_UNION},
  };
  size_t total = 0u;
  size_t i, j, out = 0u;
  IdlDataDecl *items;
  for (i = 0u; i < sizeof(groups) / sizeof(groups[0]); ++i)
    total += list_count(root, groups[i].list);
  *out_items = NULL;
  *out_count = 0u;
  if (total == 0u) return 1;
  items = (IdlDataDecl *)calloc(total, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < sizeof(groups) / sizeof(groups[0]); ++i) {
    const Node *list = list_child(root, groups[i].list);
    if (list == NULL) continue;
    for (j = 0u; j < list->data.list.count; ++j, ++out) {
      const Node *node = list->data.list.items[j];
      const char *name = decl_name(node);
      const char *underlying = text_child(node, "underlying_type");
      IdlField *fields = NULL;
      IdlEnumItem *enum_items = NULL;
      IdlAnnotation *annotations = NULL;
      size_t field_count = 0u;
      size_t enum_item_count = 0u;
      size_t annotation_count = 0u;
      items[out].kind = groups[i].kind;
      items[out].name = copy_text(name != NULL ? name : "");
      if (underlying != NULL)
        items[out].underlying_type = copy_text(underlying);
      items[out].flags = flag_child(node, "is_flags");
      if (!field_build_list(node, &fields, &field_count) ||
          !enum_item_build(node, &enum_items, &enum_item_count) ||
          !annotation_build(node, &annotations, &annotation_count)) {
        field_destroy(fields, field_count);
        enum_item_destroy(enum_items, enum_item_count);
        annotation_destroy(annotations, annotation_count);
        data_destroy(items, total);
        return 0;
      }
      items[out].fields = fields;
      items[out].field_count = field_count;
      items[out].enum_items = enum_items;
      items[out].enum_item_count = enum_item_count;
      items[out].annotations = annotations;
      items[out].annotation_count = annotation_count;
      if (items[out].name == NULL ||
          (underlying != NULL && items[out].underlying_type == NULL)) {
        data_destroy(items, total);
        return 0;
      }
    }
  }
  *out_items = items;
  *out_count = total;
  return 1;
}

static void operation_destroy(IdlOperation *items, size_t count) {
  size_t i, j;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    free((void *)items[i].request_type);
    free((void *)items[i].response_type);
    for (j = 0u; j < items[i].error_count; ++j)
      free((void *)items[i].error_types[j]);
    free((void *)items[i].error_types);
    annotation_destroy((IdlAnnotation *)items[i].annotations,
                       items[i].annotation_count);
  }
  free(items);
}

static int operation_build(
    const Node *service, IdlOperation **out_items, size_t *out_count) {
  const Node *ops = list_child(service, "operations");
  IdlOperation *items;
  size_t i, j;
  *out_items = NULL;
  *out_count = 0u;
  if (ops == NULL || ops->data.list.count == 0u) return 1;
  items = (IdlOperation *)calloc(ops->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < ops->data.list.count; ++i) {
    const Node *op = ops->data.list.items[i];
    const Node *errors = list_child(op, "errors");
    const char *name = text_child(op, "name");
    const char *request = text_child(op, "request_type");
    const char *response = text_child(op, "response_type");
    IdlAnnotation *annotations = NULL;
    size_t annotation_count = 0u;
    items[i].name = copy_text(name != NULL ? name : "");
    items[i].request_type = copy_text(request != NULL ? request : "");
    items[i].response_type = copy_text(response != NULL ? response : "");
    if (!annotation_build(op, &annotations, &annotation_count)) {
      operation_destroy(items, ops->data.list.count);
      return 0;
    }
    items[i].annotations = annotations;
    items[i].annotation_count = annotation_count;
    if (errors != NULL && errors->data.list.count != 0u) {
      char **types = (char **)calloc(errors->data.list.count, sizeof(*types));
      if (types == NULL) {
        operation_destroy(items, ops->data.list.count);
        return 0;
      }
      for (j = 0u; j < errors->data.list.count; ++j) {
        const Node *err = errors->data.list.items[j];
        types[j] = copy_text(
            err != NULL && err->type == NODE_STRING
                ? err->data.string_val
                : "");
        if (types[j] == NULL) {
          size_t k;
          for (k = 0u; k <= j; ++k) free(types[k]);
          free(types);
          operation_destroy(items, ops->data.list.count);
          return 0;
        }
      }
      items[i].error_types = (const char *const *)types;
      items[i].error_count = errors->data.list.count;
    }
    if (items[i].name == NULL ||
        items[i].request_type == NULL ||
        items[i].response_type == NULL) {
      operation_destroy(items, ops->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = ops->data.list.count;
  return 1;
}

static void service_destroy(IdlService *items, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    operation_destroy((IdlOperation *)items[i].operations,
                      items[i].operation_count);
    annotation_destroy((IdlAnnotation *)items[i].annotations,
                       items[i].annotation_count);
  }
  free(items);
}

static int service_build(
    const Node *root, IdlService **out_items, size_t *out_count) {
  const Node *services = list_child(root, "services");
  IdlService *items;
  size_t i;
  *out_items = NULL;
  *out_count = 0u;
  if (services == NULL || services->data.list.count == 0u) return 1;
  items = (IdlService *)calloc(services->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const char *name = text_child(service, "name");
    IdlOperation *ops = NULL;
    IdlAnnotation *annotations = NULL;
    size_t op_count = 0u;
    size_t annotation_count = 0u;
    items[i].name = copy_text(name != NULL ? name : "");
    if (!operation_build(service, &ops, &op_count) ||
        !annotation_build(service, &annotations, &annotation_count)) {
      operation_destroy(ops, op_count);
      annotation_destroy(annotations, annotation_count);
      service_destroy(items, services->data.list.count);
      return 0;
    }
    items[i].operations = ops;
    items[i].operation_count = op_count;
    items[i].annotations = annotations;
    items[i].annotation_count = annotation_count;
    if (items[i].name == NULL) {
      service_destroy(items, services->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = services->data.list.count;
  return 1;
}

static void channel_destroy(IdlChannel *items, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    free((void *)items[i].qualified_name);
    free((void *)items[i].message_type);
    annotation_destroy((IdlAnnotation *)items[i].annotations,
                       items[i].annotation_count);
  }
  free(items);
}

static int channel_build(
    const Node *root, IdlChannel **out_items, size_t *out_count) {
  const Node *channels = list_child(root, "channels");
  IdlChannel *items;
  size_t i;
  *out_items = NULL;
  *out_count = 0u;
  if (channels == NULL || channels->data.list.count == 0u) return 1;
  items = (IdlChannel *)calloc(channels->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < channels->data.list.count; ++i) {
    const Node *channel = channels->data.list.items[i];
    const char *name = text_child(channel, "name");
    const char *qualified = text_child(channel, "qualified_name");
    const char *message = text_child(channel, "message_type");
    IdlAnnotation *annotations = NULL;
    size_t annotation_count = 0u;
    items[i].name = copy_text(name != NULL ? name : "");
    items[i].qualified_name =
        copy_text(qualified != NULL ? qualified : (name != NULL ? name : ""));
    items[i].message_type = copy_text(message != NULL ? message : "");
    if (!annotation_build(channel, &annotations, &annotation_count)) {
      annotation_destroy(annotations, annotation_count);
      channel_destroy(items, channels->data.list.count);
      return 0;
    }
    items[i].annotations = annotations;
    items[i].annotation_count = annotation_count;
    if (items[i].name == NULL ||
        items[i].qualified_name == NULL ||
        items[i].message_type == NULL) {
      channel_destroy(items, channels->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = channels->data.list.count;
  return 1;
}

static void component_destroy(IdlComponent *items, size_t count) {
  size_t i, j;
  for (i = 0u; i < count; ++i) {
    free((void *)items[i].name);
    free((void *)items[i].qualified_name);
    for (j = 0u; j < items[i].capability_count; ++j) {
      IdlComponentCapability *cap =
          (IdlComponentCapability *)&items[i].capabilities[j];
      free((void *)cap->name);
      free((void *)cap->qualified_name);
    }
    free((void *)items[i].capabilities);
    annotation_destroy((IdlAnnotation *)items[i].annotations,
                       items[i].annotation_count);
  }
  free(items);
}

static int component_build(
    const Node *root, IdlComponent **out_items, size_t *out_count) {
  const Node *components = list_child(root, "components");
  IdlComponent *items;
  size_t i, j;
  *out_items = NULL;
  *out_count = 0u;
  if (components == NULL || components->data.list.count == 0u) return 1;
  items = (IdlComponent *)calloc(
      components->data.list.count, sizeof(*items));
  if (items == NULL) return 0;
  for (i = 0u; i < components->data.list.count; ++i) {
    const Node *component = components->data.list.items[i];
    const Node *caps = list_child(component, "capabilities");
    const char *name = text_child(component, "name");
    const char *qualified = text_child(component, "qualified_name");
    IdlAnnotation *annotations = NULL;
    size_t annotation_count = 0u;
    items[i].name = copy_text(name != NULL ? name : "");
    items[i].qualified_name =
        copy_text(qualified != NULL ? qualified : (name != NULL ? name : ""));
    if (!annotation_build(component, &annotations, &annotation_count)) {
      annotation_destroy(annotations, annotation_count);
      component_destroy(items, components->data.list.count);
      return 0;
    }
    items[i].annotations = annotations;
    items[i].annotation_count = annotation_count;
    if (caps != NULL && caps->data.list.count != 0u) {
      IdlComponentCapability *out_caps =
          (IdlComponentCapability *)calloc(
              caps->data.list.count, sizeof(*out_caps));
      if (out_caps == NULL) {
        component_destroy(items, components->data.list.count);
        return 0;
      }
      for (j = 0u; j < caps->data.list.count; ++j) {
        const Node *cap = caps->data.list.items[j];
        const char *kind = text_child(cap, "kind");
        const char *cap_name = text_child(cap, "name");
        const char *cap_qualified = text_child(cap, "qualified_name");
        out_caps[j].kind =
            kind != NULL && strcmp(kind, "channel") == 0
                ? IDL_CAPABILITY_CHANNEL
                : IDL_CAPABILITY_SERVICE;
        out_caps[j].name = copy_text(cap_name != NULL ? cap_name : "");
        out_caps[j].qualified_name = copy_text(
            cap_qualified != NULL ? cap_qualified
                                  : (cap_name != NULL ? cap_name : ""));
        if (out_caps[j].name == NULL ||
            out_caps[j].qualified_name == NULL) {
          size_t k;
          for (k = 0u; k <= j; ++k) {
            free((void *)out_caps[k].name);
            free((void *)out_caps[k].qualified_name);
          }
          free(out_caps);
          component_destroy(items, components->data.list.count);
          return 0;
        }
      }
      items[i].capabilities = out_caps;
      items[i].capability_count = caps->data.list.count;
    }
    if (items[i].name == NULL || items[i].qualified_name == NULL) {
      component_destroy(items, components->data.list.count);
      return 0;
    }
  }
  *out_items = items;
  *out_count = components->data.list.count;
  return 1;
}

static const char *schema_attribute(
    const Node *schema, const char *name) {
  const Node *attrs = list_child(schema, "attributes");
  size_t i;
  if (attrs == NULL || name == NULL) return NULL;
  for (i = 0u; i < attrs->data.list.count; ++i) {
    const Node *attr = attrs->data.list.items[i];
    const char *candidate = text_child(attr, "name");
    if (candidate != NULL && strcmp(candidate, name) == 0)
      return text_child(attr, "value");
  }
  return NULL;
}

int idl_contract_build_from_tree(
    const Node *root, IdlContract **out_contract, IdlDiagnostic *diagnostic) {
  const Node *schema;
  const char *name;
  const char *version;
  IdlContract *contract;
  IdlAnnotation *contract_annotations = NULL;
  size_t contract_annotation_count = 0u;
  IdlDataDecl *data = NULL;
  IdlService *services = NULL;
  IdlChannel *channels = NULL;
  IdlComponent *components = NULL;
  size_t data_count = 0u, service_count = 0u;
  size_t channel_count = 0u, component_count = 0u;

  if (out_contract != NULL) *out_contract = NULL;
  if (root == NULL || out_contract == NULL) {
    diagnostic_set(diagnostic, IDL_INVALID_ARGUMENT, -1, -1,
                   "Invalid IDL contract build arguments");
    return 0;
  }

  schema = child(root, "schema");
  name = text_child(schema, "schema_name");
  version = schema_attribute(schema, "version");

  contract = (IdlContract *)calloc(1, sizeof(*contract));
  if (contract == NULL) goto oom;
  contract->size = sizeof(*contract);
  contract->abi_version = IDL_CONTRACT_ABI_VERSION;
  contract->name = copy_text(name != NULL ? name : "");
  if (version != NULL) contract->version = copy_text(version);
  if (contract->name == NULL ||
      (version != NULL && contract->version == NULL) ||
      !annotation_build(schema, &contract_annotations,
                        &contract_annotation_count))
    goto oom;

  if (!data_build(root, &data, &data_count) ||
      !service_build(root, &services, &service_count) ||
      !channel_build(root, &channels, &channel_count) ||
      !component_build(root, &components, &component_count))
    goto oom;

  contract->annotations = contract_annotations;
  contract->annotation_count = contract_annotation_count;
  contract->data = data;
  contract->data_count = data_count;
  contract->services = services;
  contract->service_count = service_count;
  contract->channels = channels;
  contract->channel_count = channel_count;
  contract->components = components;
  contract->component_count = component_count;
  diagnostic_set(diagnostic, IDL_OK, -1, -1, NULL);
  *out_contract = contract;
  return 1;

oom:
  annotation_destroy(contract_annotations, contract_annotation_count);
  data_destroy(data, data_count);
  service_destroy(services, service_count);
  channel_destroy(channels, channel_count);
  component_destroy(components, component_count);
  if (contract != NULL) {
    free((void *)contract->name);
    free((void *)contract->version);
    free(contract);
  }
  diagnostic_set(diagnostic, IDL_NO_MEMORY, -1, -1,
                 "Out of memory building typed IDL contract");
  return 0;
}

int idl_contract_parse(
    const char *text, size_t length,
    IdlContract **out_contract, IdlDiagnostic *diagnostic) {
  Node *root;
  tbe_error_t error = {0};
  int ok;
  if (out_contract != NULL) *out_contract = NULL;
  if (text == NULL || out_contract == NULL) {
    diagnostic_set(diagnostic, IDL_INVALID_ARGUMENT, -1, -1,
                   "Invalid IDL parse arguments");
    return 0;
  }
  root = create_node_map(NULL);
  if (root == NULL) {
    diagnostic_set(diagnostic, IDL_NO_MEMORY, -1, -1,
                   "Out of memory creating IDL frontend tree");
    return 0;
  }
  if (idl_parse(text, length, root, &error) != 0) {
    diagnostic_set(diagnostic, diagnostic_status_from_tbe(error.code),
                   error.line, error.column, error.message);
    node_free(root);
    return 0;
  }
  ok = idl_contract_build_from_tree(root, out_contract, diagnostic);
  node_free(root);
  return ok;
}

void idl_contract_destroy(IdlContract *contract) {
  if (contract == NULL) return;
  free((void *)contract->name);
  free((void *)contract->version);
  annotation_destroy((IdlAnnotation *)contract->annotations,
                     contract->annotation_count);
  data_destroy((IdlDataDecl *)contract->data, contract->data_count);
  service_destroy((IdlService *)contract->services, contract->service_count);
  channel_destroy((IdlChannel *)contract->channels, contract->channel_count);
  component_destroy((IdlComponent *)contract->components,
                    contract->component_count);
  free(contract);
}

const IdlDataDecl *idl_contract_find_data(
    const IdlContract *contract, const char *name) {
  size_t i;
  if (contract == NULL || name == NULL) return NULL;
  for (i = 0u; i < contract->data_count; ++i)
    if (contract->data[i].name != NULL &&
        strcmp(contract->data[i].name, name) == 0)
      return &contract->data[i];
  return NULL;
}

const IdlService *idl_contract_find_service(
    const IdlContract *contract, const char *name) {
  size_t i;
  if (contract == NULL || name == NULL) return NULL;
  for (i = 0u; i < contract->service_count; ++i)
    if (contract->services[i].name != NULL &&
        strcmp(contract->services[i].name, name) == 0)
      return &contract->services[i];
  return NULL;
}

const IdlChannel *idl_contract_find_channel(
    const IdlContract *contract, const char *name) {
  size_t i;
  if (contract == NULL || name == NULL) return NULL;
  for (i = 0u; i < contract->channel_count; ++i) {
    const IdlChannel *channel = &contract->channels[i];
    if ((channel->qualified_name != NULL &&
         strcmp(channel->qualified_name, name) == 0) ||
        (channel->name != NULL &&
         strcmp(channel->name, name) == 0))
      return channel;
  }
  return NULL;
}

const IdlComponent *idl_contract_find_component(
    const IdlContract *contract, const char *name) {
  size_t i;
  if (contract == NULL || name == NULL) return NULL;
  for (i = 0u; i < contract->component_count; ++i) {
    const IdlComponent *component = &contract->components[i];
    if ((component->qualified_name != NULL &&
         strcmp(component->qualified_name, name) == 0) ||
        (component->name != NULL &&
         strcmp(component->name, name) == 0))
      return component;
  }
  return NULL;
}

size_t idl_annotation_count(
    const IdlAnnotation *annotations, size_t annotation_count,
    const char *name) {
  size_t i;
  size_t count = 0u;
  if (name == NULL) return 0u;
  for (i = 0u; i < annotation_count; ++i)
    if (annotations != NULL && annotations[i].name != NULL &&
        strcmp(annotations[i].name, name) == 0)
      ++count;
  return count;
}

const IdlAnnotation *idl_annotation_find(
    const IdlAnnotation *annotations, size_t annotation_count,
    const char *name, size_t occurrence) {
  size_t i;
  if (name == NULL) return NULL;
  for (i = 0u; i < annotation_count; ++i) {
    if (annotations != NULL && annotations[i].name != NULL &&
        strcmp(annotations[i].name, name) == 0) {
      if (occurrence == 0u) return &annotations[i];
      --occurrence;
    }
  }
  return NULL;
}

const char *idl_annotation_argument(
    const IdlAnnotation *annotation, size_t index) {
  if (annotation == NULL || index >= annotation->argument_count ||
      annotation->arguments == NULL)
    return NULL;
  return annotation->arguments[index];
}
