#include "component_provider_candidate.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Bounded, literal-safe compiler identities. The IR already owns the
 * semantic naming; do not reconstruct identities from display text. */
static int candidate_identifier(const char *s) {
  size_t i, n;
  if (s == NULL || !((s[0] >= 'A' && s[0] <= 'Z') ||
                     (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_'))
    return 0;
  n = strlen(s);
  if (n == 0u || n > 180u) return 0;
  for (i = 1u; i < n; ++i)
    if (!isalnum((unsigned char)s[i]) && s[i] != '_') return 0;
  return 1;
}

static int candidate_stable_id(const char *s) {
  size_t i, n;
  if (s == NULL) return 0;
  n = strlen(s);
  if (n == 0u || n > 180u) return 0;
  for (i = 0u; i < n; ++i) {
    const unsigned char c = (unsigned char)s[i];
    if (!isalnum(c) && c != '.' && c != '_' && c != '-') return 0;
  }
  return 1;
}

static int candidate_basename(const char *s) {
  size_t i;
  if (s == NULL || !s[0]) return 0;
  for (i = 0u; s[i]; ++i) {
    const unsigned char c = (unsigned char)s[i];
    if (i >= 180u || (!isalnum(c) && c != '_' && c != '.' && c != '-'))
      return 0;
  }
  return 1;
}

int databind_component_provider_candidate_admitted(
    const IdlComponent *component,
    const databind_compiler_service_native_ir *ir) {
  const databind_compiler_service_native_operation *op;
  const IdlComponentCapability *cap;
  if (component == NULL || ir == NULL || ir->operation_count != 1u ||
      ir->operations == NULL || component->capability_count != 1u ||
      component->capabilities == NULL ||
      !candidate_stable_id(component->qualified_name))
    return 0;
  cap = &component->capabilities[0];
  op = &ir->operations[0];

  /* First bounded slice: exactly one Service, one native non-throwing
   * operation. Error envelopes and multi-Service dispatch are not inferred. */
  if (cap->kind != IDL_CAPABILITY_SERVICE ||
      !candidate_identifier(cap->name) ||
      !candidate_identifier(op->symbol) ||
      !candidate_identifier(op->request_type) ||
      !candidate_identifier(op->response_type) ||
      !candidate_stable_id(op->request_type_identity) ||
      !candidate_stable_id(op->response_type_identity) ||
      op->error_count != 0u ||
      op->request_type_identity == NULL ||
      op->response_type_identity == NULL ||
      op->service_name == NULL ||
      strcmp(op->service_name, cap->name) != 0)
    return 0;
  return 1;
}

int databind_component_provider_candidate_render(
    FILE *header,
    FILE *source,
    const IdlComponent *component,
    const databind_compiler_service_native_ir *ir,
    const char *native_service_header,
    const char *component_header,
    uint32_t major,
    uint32_t minor,
    uint32_t patch) {
  const databind_compiler_service_native_operation *op;
  const char *sym;
  const char *request;
  const char *response;

  if (header == NULL || source == NULL ||
      header == source ||
      !databind_component_provider_candidate_admitted(component, ir) ||
      !candidate_basename(native_service_header) ||
      !candidate_basename(component_header))
    return -1;

  op = &ir->operations[0];
  sym = op->symbol;
  request = op->request_type;
  response = op->response_type;

  if (fprintf(header,
      "#ifndef %s__COMPONENT_PROVIDER_CANDIDATE_H\n"
      "#define %s__COMPONENT_PROVIDER_CANDIDATE_H\n"
      "#include \"%s\"\n"
      "#include <salts/component_plugin_abi.h>\n"
      "#include <cmeta/object_interface.h>\n\n",
      sym, sym, native_service_header) < 0) return -1;

  /* A Contract-only ServiceNativeIR already has exact NativeSourceIR record
   * identities. Reuse its canonical CMeta storage descriptors; re-declaring
   * atom/const IDs from Binary-oriented names creates a second type authority
   * and would break FunctionAbi comparison with the original Plugin artifact.
   * Pointer representation matches native_contract_emit_reflection exactly. */
  if (fprintf(header,
      "static const cmeta_type_desc %s__component_req_ptr_type = {\n"
      "  .name = \"const %s *\", .size = sizeof(const %s *),\n"
      "  .align = CMETA_ALIGNOF(const %s *), .kind = CMETA_T_POINTER,\n"
      "  .pointee = &%s_native_cmeta_type\n"
      "};\n"
      "static const cmeta_type_desc %s__component_resp_ptr_type = {\n"
      "  .name = \"%s *\", .size = sizeof(%s *),\n"
      "  .align = CMETA_ALIGNOF(%s *), .kind = CMETA_T_POINTER,\n"
      "  .pointee = &%s_native_cmeta_type\n"
      "};\n",
      sym, request, request, request, request,
      sym, response, response, response, response) < 0) return -1;

  if (fprintf(header,
      "#define %s__COMPONENT_METHODS(X, I) \\\n"
      "    X(I, FR2, int, invoke, unknown, &cmeta_type_int, \\\n"
      "      CMETA_ABI_SCALAR, CMETA_RESULT_VALUE, \\\n"
      "      (const %s *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED, \\\n"
      "       &%s__component_req_ptr_type, CMETA_ABI_OBJECT_POINTER), \\\n"
      "      (%s *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, \\\n"
      "       &%s__component_resp_ptr_type, CMETA_ABI_OBJECT_POINTER))\n"
      "CMETA_INTERFACE(%s__component_service, %s__COMPONENT_METHODS);\n"
      "CMETA_OBJECT_INTERFACE_ADAPTER(%s__component_service);\n"
      "#endif /* %s__COMPONENT_PROVIDER_CANDIDATE_H */\n",
      sym, request, sym, response, sym, sym, sym, sym, sym) < 0) return -1;

  if (fprintf(source,
      "#include \"%s\"\n"
      "#include <salts/plugin_decl.h>\n"
      "#include <string.h>\n\n",
      component_header) < 0) return -1;

  if (fprintf(source,
      "/* Compiler verifies exact native ABI; reflection never dispatches C\n"
      " * function pointers dynamically or retains the Plugin module. */\n"
      "static int %s__component_invoke(\n"
      "    void *self, const %s *request, %s *response) {\n"
      "  (void)self;\n"
      "  return %s(request, response);\n"
      "}\n"
      "CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(\n"
      "    &%s__component_invoke, %s__component_service_invoke_fn),\n"
      "    \"generated Component native Service ABI mismatch\");\n"
      "CMETA_IMPLEMENTS(%s__component_service,\n"
      "    %s__component_impl, 0u,\n"
      "    .invoke = %s__component_invoke);\n\n",
      sym, request, response, sym, sym, sym, sym, sym, sym) < 0) return -1;

  if (fprintf(source,
      "static cmeta_status %s__component_project(\n"
      "    void *context, const cmeta_object_ref *object,\n"
      "    const cmeta_interface_desc *expected,\n"
      "    cmeta_interface_projection *out) {\n"
      "  (void)context;\n"
      "  if (object == NULL || out == NULL ||\n"
      "      !cmeta_interface_desc_equal(expected,\n"
      "          %s__component_service_interface()))\n"
      "    return CMETA_TRAIT_MISSING;\n"
      "  out->size = sizeof(*out);\n"
      "  out->interface = %s__component_service_interface();\n"
      "  out->self = object->object;\n"
      "  out->dispatch = &%s__component_impl_vtable;\n"
      "  return CMETA_OK;\n"
      "}\n"
      "static const cmeta_object_interface_provider %s__interfaces = {\n"
      "  sizeof(cmeta_object_interface_provider), NULL,\n"
      "  %s__component_project};\n\n",
      sym, sym, sym, sym, sym, sym) < 0) return -1;

  if (fprintf(source,
      "static const cmeta_component_capability %s__capabilities[] = {\n"
      "  {CMETA_COMPONENT_PROVIDES, &%s__component_service_interface_meta}\n"
      "};\n"
      "static const cmeta_component_desc %s__component = {\n"
      "  sizeof(cmeta_component_desc), CMETA_COMPONENT_DECLARATION_VERSION,\n"
      "  \"%s\", NULL, %s__capabilities, 1u};\n"
      "static int %s__state;\n"
      "static cmeta_status SALTS_COMPONENT_CALL %s__create(\n"
      "    void *context, const cmeta_data_desc *config_data,\n"
      "    const void *config_value,\n"
      "    const salts_component_dependency *dependencies,\n"
      "    size_t dependency_count, cmeta_object_ref *out) {\n"
      "  (void)dependencies;\n"
      "  if (context == NULL || config_data != NULL ||\n"
      "      config_value != NULL || dependency_count != 0u)\n"
      "    return CMETA_INVALID_ARGUMENT;\n"
      "  return cmeta_object_borrow(out, context, &cmeta_data_int, NULL);\n"
      "}\n",
      sym, sym, sym, component->qualified_name, sym,
      sym, sym) < 0) return -1;

  if (fprintf(source,
      "static const salts_component_provider_binding %s__binding = {\n"
      "  sizeof(salts_component_provider_binding),\n"
      "  SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,\n"
      "  &%s__component, &%s__state, &%s__interfaces,\n"
      "  %s__create, NULL, NULL};\n"
      "static const salts_component_provider_binding *%s__get_binding(\n"
      "    void *self) { return (const salts_component_provider_binding *)self; }\n"
      "CMETA_IMPLEMENTS(salts_component_provider,\n"
      "    %s__provider_impl, 0u,\n"
      "    .get_binding = %s__get_binding);\n"
      "static salts_component_provider %s__provider = {\n"
      "  (void *)&%s__binding, &%s__provider_impl_vtable};\n"
      "#define %s__COMPONENT_EXPORTS(X) \\\n"
      "    X(interface, (salts_component_provider, &%s__provider), \\\n"
      "      \"component-provider\", SALTS_COMPONENT_PROVIDER_CONTRACT_ID, \\\n"
      "      SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, 0)\n"
      "CMETA_PLUGIN_DECLARE(%s__component_plugin, \"%s\", (%uu,%uu,%uu),\n"
      "    %s__COMPONENT_EXPORTS, CMETA_PLUGIN_PASSIVE());\n",
      sym, sym, sym, sym, sym,
      sym, sym, sym, sym, sym, sym, sym, sym, sym,
      component->qualified_name, major, minor, patch, sym) < 0) return -1;

  return ferror(header) == 0 && ferror(source) == 0 ? 0 : -1;
}
