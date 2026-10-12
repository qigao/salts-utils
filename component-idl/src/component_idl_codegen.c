#include "component_idl_codegen.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    SIDLC_ID_MAX = 64, SIDLC_QUALIFIED_MAX = 255,
    SIDLC_HEADER_MAX = 127, SIDLC_CAP_MAX = 16,
    SIDLC_METHOD_MAX = 16, SIDLC_FIELD_MAX = 64
};

static int identifier(const char *s) {
    size_t i;
    if (s == NULL || !s[0]) return 0;
    for (i = 0u; s[i]; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (i >= SIDLC_ID_MAX) return 0;
        if (i == 0u) {
            if (!((c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') || c == '_')) return 0;
        } else if (!((c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

/* A string admitted here may safely appear in a generated C string literal. */
static int qualified_id(const char *s) {
    size_t i;
    int first = 1;
    if (s == NULL || !s[0]) return 0;
    for (i = 0u; s[i]; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (i >= SIDLC_QUALIFIED_MAX) return 0;
        if (c == '.') {
            if (first) return 0;
            first = 1;
        } else if (first) {
            if (!((c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') || c == '_')) return 0;
            first = 0;
        } else if (!((c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return !first;
}

static int header_name(const char *s) {
    size_t i;
    if (s == NULL || !s[0] || s[0] == '.') return 0;
    for (i = 0u; s[i]; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (i >= SIDLC_HEADER_MAX ||
            !((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' ||
              c == '.' || c == '-'))
            return 0;
    }
    return 1;
}

static int record_admitted(const IdlContract *contract, const char *name) {
    const IdlDataDecl *record;
    size_t i;
    if (!identifier(name)) return 0;
    record = idl_contract_find_data(contract, name);
    if (record == NULL || record->kind != IDL_DATA_MESSAGE ||
        record->field_count == 0u || record->field_count > SIDLC_FIELD_MAX ||
        record->fields == NULL)
        return 0;
    for (i = 0u; i < record->field_count; ++i) {
        const IdlField *field = &record->fields[i];
        if (!identifier(field->name) || field->type_name == NULL ||
            strcmp(field->type_name, "uint32") != 0 ||
            field->collection_kind != IDL_COLLECTION_NONE ||
            field->optional || field->nullable)
            return 0;
    }
    return 1;
}

static int service_admitted(const IdlContract *contract,
                            const IdlComponentCapability *cap) {
    const IdlService *service;
    size_t i;
    if (cap == NULL || cap->kind != IDL_CAPABILITY_SERVICE ||
        !identifier(cap->name) || !qualified_id(cap->qualified_name))
        return 0;
    service = idl_contract_find_service(contract, cap->name);
    if (service == NULL || !identifier(service->name) ||
        service->operation_count == 0u ||
        service->operation_count > SIDLC_METHOD_MAX ||
        service->operations == NULL)
        return 0;
    for (i = 0u; i < service->operation_count; ++i) {
        const IdlOperation *op = &service->operations[i];
        if (!identifier(op->name) || op->error_count != 0u ||
            !record_admitted(contract, op->request_type) ||
            !record_admitted(contract, op->response_type))
            return 0;
    }
    return 1;
}

static const IdlComponent *admitted_component(
    const IdlContract *contract, const char *id, const char *types_header) {
    const IdlComponent *component;
    size_t i;
    if (contract == NULL || contract->size < sizeof(*contract) ||
        contract->abi_version != IDL_CONTRACT_ABI_VERSION ||
        !identifier(contract->name) || !qualified_id(id) ||
        !header_name(types_header))
        return NULL;
    component = idl_contract_find_component(contract, id);
    if (component == NULL || !identifier(component->name) ||
        !qualified_id(component->qualified_name) ||
        strcmp(component->qualified_name, id) != 0 ||
        component->capability_count == 0u ||
        component->capability_count > SIDLC_CAP_MAX ||
        component->capabilities == NULL)
        return NULL;
    for (i = 0u; i < component->capability_count; ++i) {
        size_t j;
        if (!service_admitted(contract, &component->capabilities[i]))
            return NULL;
        for (j = 0u; j < i; ++j)
            if (strcmp(component->capabilities[i].name,
                       component->capabilities[j].name) == 0)
                return NULL;
    }
    return component;
}

/* Length-prefixed native symbols avoid collisions between concatenated names. */
static int native_name(char *out, size_t n,
                       const char *schema, const char *component,
                       const char *service) {
    int written;
    if (service == NULL)
        written = snprintf(out, n, "sidlc_%zu_%s_%zu_%s",
                           strlen(schema), schema,
                           strlen(component), component);
    else
        written = snprintf(out, n, "sidlc_%zu_%s_%zu_%s_%zu_%s",
                           strlen(schema), schema,
                           strlen(component), component,
                           strlen(service), service);
    return written > 0 && (size_t)written < n;
}

int salts_component_idl_candidate_admit(
    const IdlContract *contract, const char *qualified_component_id,
    const char *native_record_header) {
    const IdlComponent *component = admitted_component(
        contract, qualified_component_id, native_record_header);
    char symbol[320];
    size_t i;
    if (component == NULL ||
        !native_name(symbol, sizeof(symbol),
                     contract->name, component->name, NULL))
        return -1;
    for (i = 0u; i < component->capability_count; ++i)
        if (!native_name(symbol, sizeof(symbol),
                         contract->name, component->name,
                         component->capabilities[i].name))
            return -1;
    return 0;
}

int salts_component_idl_candidate_render(
    FILE *out, const IdlContract *contract,
    const char *qualified_component_id, const char *native_record_header) {
    const IdlComponent *component;
    char component_symbol[320];
    size_t i;
    if (out == NULL || salts_component_idl_candidate_admit(
            contract, qualified_component_id, native_record_header) != 0)
        return -1;
    component = idl_contract_find_component(contract, qualified_component_id);
    if (component == NULL ||
        !native_name(component_symbol, sizeof(component_symbol),
                     contract->name, component->name, NULL))
        return -1;
    if (fprintf(out,
          "#ifndef %s_COMPONENT_IDL_H\n#define %s_COMPONENT_IDL_H\n"
          "#include <salts/component_abi.h>\n"
          "#include <cmeta/interface.h>\n"
          "#include <cmeta/object_interface.h>\n"
          "#include \"%s\"\n\n",
          component_symbol, component_symbol, native_record_header) < 0)
        return -1;

    for (i = 0u; i < component->capability_count; ++i) {
        const IdlComponentCapability *cap = &component->capabilities[i];
        const IdlService *service = idl_contract_find_service(contract, cap->name);
        char iface[320];
        size_t m;
        if (service == NULL ||
            !native_name(iface, sizeof(iface), contract->name,
                         component->name, service->name) ||
            fprintf(out, "#define %s_METHODS(X, I) ", iface) < 0 ||
            fputc('\\', out) == EOF || fputc('\n', out) == EOF)
            return -1;
        for (m = 0u; m < service->operation_count; ++m) {
            const IdlOperation *op = &service->operations[m];
            if (fprintf(out,
                  "  X(I, R2, int, %s, const %s *, request, %s *, response)",
                  op->name, op->request_type, op->response_type) < 0)
                return -1;
            if (m + 1u != service->operation_count &&
                (fputc(' ', out) == EOF || fputc('\\', out) == EOF))
                return -1;
            if (fputc('\n', out) == EOF) return -1;
        }
        if (fprintf(out,
              "CMETA_INTERFACE(%s, %s_METHODS);\n"
              "CMETA_OBJECT_INTERFACE_ADAPTER(%s);\n\n",
              iface, iface, iface) < 0)
            return -1;
    }

    if (fprintf(out, "static const cmeta_component_capability "
                     "%s__capabilities[] = {\n", component_symbol) < 0)
        return -1;
    for (i = 0u; i < component->capability_count; ++i) {
        char iface[320];
        if (!native_name(iface, sizeof(iface), contract->name,
                         component->name, component->capabilities[i].name) ||
            fprintf(out,
              "  { CMETA_COMPONENT_PROVIDES, &%s_interface_meta },\n",
              iface) < 0)
            return -1;
    }
    if (fprintf(out,
          "};\n"
          "static const cmeta_component_desc %s__component_meta = {\n"
          "  sizeof(cmeta_component_desc), CMETA_COMPONENT_DECLARATION_VERSION,\n"
          "  \"%s\", NULL, %s__capabilities, %zu\n"
          "};\n\n"
          "/* Provider supplies exact native execution and borrowed projection\n"
          " * authority. No module lease or dynamic invocation is inferred. */\n"
          "static inline salts_component_provider_binding %s_bind(\n"
          "    void *provider_context,\n"
          "    const cmeta_object_interface_provider *interfaces,\n"
          "    salts_component_create_fn create,\n"
          "    salts_component_activate_fn activate,\n"
          "    salts_component_deactivate_fn deactivate) {\n"
          "  salts_component_provider_binding binding = {\n"
          "    sizeof(salts_component_provider_binding),\n"
          "    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,\n"
          "    &%s__component_meta, provider_context, interfaces,\n"
          "    create, activate, deactivate\n"
          "  };\n"
          "  return binding;\n"
          "}\n\n#endif\n",
          component_symbol, component->qualified_name,
          component_symbol, component->capability_count,
          component_symbol, component_symbol) < 0)
        return -1;
    return ferror(out) ? -1 : 0;
}
