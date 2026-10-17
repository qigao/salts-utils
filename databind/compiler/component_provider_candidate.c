#include "component_provider_candidate.h"
#include "cmeta_fs.h"

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
  size_t i, j;
  const IdlComponentCapability *cap;
  if (component == NULL || ir == NULL || ir->operations == NULL ||
      ir->operation_count == 0u || ir->operation_count > 4u ||
      component->capability_count != 1u || component->capabilities == NULL ||
      !candidate_stable_id(component->qualified_name))
    return 0;
  cap = &component->capabilities[0];
  if (cap->kind != IDL_CAPABILITY_SERVICE ||
      !candidate_identifier(cap->name))
    return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (!candidate_identifier(op->symbol) ||
        !candidate_identifier(op->operation_name) ||
        !candidate_identifier(op->request_type) ||
        !candidate_identifier(op->response_type) ||
        !candidate_stable_id(op->request_type_identity) ||
        !candidate_stable_id(op->response_type_identity) ||
        !candidate_stable_id(op->qualified_operation) ||
        op->error_count != 0u || op->service_name == NULL ||
        strcmp(op->service_name, cap->name) != 0)
      return 0;
    for (j = 0u; j < i; ++j) {
      const databind_compiler_service_native_operation *prior = &ir->operations[j];
      if (strcmp(op->operation_name, prior->operation_name) == 0 ||
          strcmp(op->symbol, prior->symbol) == 0 ||
          strcmp(op->qualified_operation, prior->qualified_operation) == 0)
        return 0;
    }
  }
  return 1;
}

static int candidate_emit_common_footer(
    FILE *source, const IdlComponent *component, const char *sym,
    uint32_t major, uint32_t minor, uint32_t patch) {
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
      "CMETA_PLUGIN_DECLARE(%s__component_provider_dso, \"%s\", (%uu,%uu,%uu),\n"
      "    %s__COMPONENT_EXPORTS, CMETA_PLUGIN_PASSIVE());\n",
      sym, sym, sym, sym, sym,
      sym, sym, sym, sym, sym, sym, sym, sym, sym,
      component->qualified_name, major, minor, patch, sym) < 0) return -1;

  return ferror(source) == 0 ? 0 : -1;
}

static int candidate_emit_pointer_descriptors(
    FILE *header, const databind_compiler_service_native_operation *op) {
  const char *sym = op->symbol;
  const char *request = op->request_type;
  const char *response = op->response_type;
  /* Share one original NativeSourceIR value identity across all operations. */
  return fprintf(header,
      "static const cmeta_type_desc %s__component_req_ptr_type = {\n"
      "  \"const %s *\", sizeof(const %s *),\n"
      "  CMETA_ALIGNOF(const %s *), CMETA_T_POINTER,\n"
      "  &%s_native_cmeta_type, NULL, NULL\n"
      "};\n"
      "static const cmeta_type_desc %s__component_resp_ptr_type = {\n"
      "  \"%s *\", sizeof(%s *),\n"
      "  CMETA_ALIGNOF(%s *), CMETA_T_POINTER,\n"
      "  &%s_native_cmeta_type, NULL, NULL\n"
      "};\n",
      sym, request, request, request, request,
      sym, response, response, response, response) >= 0 ? 0 : -1;
}

/* Bounded one-Service/multi-operation projection. Slot names follow the
 * canonical IdlOperation names and can never alias; every dispatch retains
 * its canonical native source operation, not a reflected FFI trampoline. */
static int candidate_render_multi(
    FILE *header, FILE *source, const IdlComponent *component,
    const databind_compiler_service_native_ir *ir,
    const char *native_service_header, const char *component_header,
    uint32_t major, uint32_t minor, uint32_t patch) {
  const char *first = ir->operations[0].symbol;
  size_t i;
  if (fprintf(header,
      "#ifndef %s__COMPONENT_PROVIDER_CANDIDATE_H\n"
      "#define %s__COMPONENT_PROVIDER_CANDIDATE_H\n"
      "#include \"%s\"\n"
      "#include <salts/component_provider_abi.h>\n"
      "#include <cmeta/object_interface.h>\n\n",
      first, first, native_service_header) < 0)
    return -1;

  for (i = 0u; i < ir->operation_count; ++i)
    if (candidate_emit_pointer_descriptors(header, &ir->operations[i]) != 0)
      return -1;
  if (fprintf(header, "#define %s__COMPONENT_METHODS(X, I) \\\n", first) < 0)
    return -1;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(header,
        "    X(I, FR2, int, invoke_%s, unknown, &cmeta_type_int, \\\n"
        "      CMETA_ABI_SCALAR, CMETA_RESULT_VALUE, \\\n"
        "      (const %s *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED, \\\n"
        "       &%s__component_req_ptr_type, CMETA_ABI_OBJECT_POINTER), \\\n"
        "      (%s *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, \\\n"
        "       &%s__component_resp_ptr_type, CMETA_ABI_OBJECT_POINTER))%s\n",
        op->operation_name, op->request_type, op->symbol,
        op->response_type, op->symbol,
        i + 1u < ir->operation_count ? " \\" : "") < 0)
      return -1;
  }
  if (fprintf(header,
      "CMETA_INTERFACE(%s__component_service, %s__COMPONENT_METHODS);\n"
      "CMETA_OBJECT_INTERFACE_ADAPTER(%s__component_service);\n"
      "#endif /* %s__COMPONENT_PROVIDER_CANDIDATE_H */\n",
      first, first, first, first) < 0)
    return -1;

  if (fprintf(source,
      "#include \"%s\"\n"
      "#include <salts/plugin_decl.h>\n"
      "#include <string.h>\n\n",
      component_header) < 0)
    return -1;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(source,
        "static int %s__component_invoke(void *self,\n"
        "    const %s *request, %s *response) {\n"
        "  (void)self;\n"
        "  return %s(request, response);\n"
        "}\n"
        "CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(\n"
        "    &%s__component_invoke,\n"
        "    int (*)(void *, const %s *, %s *)),\n"
        "    \"Generated multi-method native C11 ABI mismatch\");\n",
        op->symbol, op->request_type, op->response_type, op->symbol,
        op->symbol, op->request_type, op->response_type) < 0)
      return -1;
  }
  if (fprintf(source,
      "CMETA_IMPLEMENTS(%s__component_service,\n"
      "    %s__component_impl, 0u,\n",
      first, first) < 0)
    return -1;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(source,
        "    .invoke_%s = %s__component_invoke%s\n",
        op->operation_name, op->symbol,
        i + 1u < ir->operation_count ? "," : "") < 0)
      return -1;
  }
  if (fputs(");\n\n", source) == EOF ||
      candidate_emit_common_footer(
          source, component, first, major, minor, patch) != 0)
    return -1;
  return ferror(header) == 0 && ferror(source) == 0 ? 0 : -1;
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
  if (ir->operation_count > 1u)
    return candidate_render_multi(header, source, component, ir,
        native_service_header, component_header, major, minor, patch);

  op = &ir->operations[0];
  sym = op->symbol;
  request = op->request_type;
  response = op->response_type;

  if (fprintf(header,
      "#ifndef %s__COMPONENT_PROVIDER_CANDIDATE_H\n"
      "#define %s__COMPONENT_PROVIDER_CANDIDATE_H\n"
      "#include \"%s\"\n"
      "#include <salts/component_provider_abi.h>\n"
      "#include <cmeta/object_interface.h>\n\n",
      sym, sym, native_service_header) < 0) return -1;

  /* A Contract-only ServiceNativeIR already has exact NativeSourceIR record
   * identities. Reuse its canonical CMeta storage descriptors; re-declaring
   * atom/const IDs from Binary-oriented names creates a second type authority
   * and would break FunctionAbi comparison with the original Plugin artifact.
   * Pointer representation matches native_contract_emit_reflection exactly. */
  if (fprintf(header,
      "static const cmeta_type_desc %s__component_req_ptr_type = {\n"
      "  \"const %s *\", sizeof(const %s *),\n"
      "  CMETA_ALIGNOF(const %s *), CMETA_T_POINTER,\n"
      "  &%s_native_cmeta_type, NULL, NULL\n"
      "};\n"
      "static const cmeta_type_desc %s__component_resp_ptr_type = {\n"
      "  \"%s *\", sizeof(%s *),\n"
      "  CMETA_ALIGNOF(%s *), CMETA_T_POINTER,\n"
      "  &%s_native_cmeta_type, NULL, NULL\n"
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
      "    &%s__component_invoke, int (*)(void *, const %s *, %s *)),\n"
      "    \"generated Component native Service ABI mismatch\");\n"
      "CMETA_IMPLEMENTS(%s__component_service,\n"
      "    %s__component_impl, 0u,\n"
      "    .invoke = %s__component_invoke);\n\n",
      sym, request, response, sym, sym, request, response, sym, sym, sym) < 0) return -1;

  if (candidate_emit_common_footer(
          source, component, sym, major, minor, patch) != 0)
    return -1;
  return ferror(header) == 0 && ferror(source) == 0 ? 0 : -1;
}


/* The shared compiler coordinator has already reserved both final output
 * paths as unique sibling stage files. No .component artifact is allowed to
 * write to the final directory on its own or bypass transactional abort. */
static int component_provider_selected_service(
    void *context, const char *service_name) {
  const IdlComponent *component = (const IdlComponent *)context;
  size_t i;
  if (component == NULL || service_name == NULL)
    return 0;
  for (i = 0u; i < component->capability_count; ++i)
    if (component->capabilities[i].kind == IDL_CAPABILITY_SERVICE &&
        component->capabilities[i].name != NULL &&
        strcmp(component->capabilities[i].name, service_name) == 0)
      return 1;
  return 0;
}

int databind_compiler_component_provider_render_staged(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const char *header_stage,
    const char *source_stage) {
  const databind_compiler_component_provider_config *config;
  const IdlComponent *component;
  databind_compiler_service_native_ir ir = {0};
  FILE *header = NULL;
  FILE *source = NULL;
  char service_basename[SALTS_FS_MAX_PATH];
  char component_basename[SALTS_FS_MAX_PATH];
  int result = -1;

  if (input == NULL || input->contract == NULL || input->binary_format != NULL ||
      request == NULL || request->config == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT ||
      request->id.kind != DATABIND_COMPILER_ARTIFACT_COMPONENT ||
      request->output == NULL || header_stage == NULL ||
      source_stage == NULL || strcmp(header_stage, source_stage) == 0)
    return -1;
  config = (const databind_compiler_component_provider_config *)request->config;
  if (config->component_id == NULL || config->header_output == NULL ||
      config->native_service_header == NULL ||
      cmeta_fs_path_basename(config->header_output, component_basename,
                             sizeof(component_basename)) != 0 ||
      cmeta_fs_path_basename(config->native_service_header, service_basename,
                             sizeof(service_basename)) != 0)
    return -1;

  component = idl_contract_find_component(input->contract, config->component_id);
  if (component == NULL ||
      databind_compiler_service_native_build_selected(
          input->contract, component_provider_selected_service,
          (void *)component, &ir) != 0)
    goto cleanup;
  /* Reject all unsupported shapes before writing even to the stage files.
   * No output is ever changed for a failure in canonical IR admission. */
  if (!databind_component_provider_candidate_admitted(component, &ir))
    goto cleanup;

  header = fopen(header_stage, "wb");
  if (header == NULL)
    goto cleanup;
  source = fopen(source_stage, "wb");
  if (source == NULL)
    goto cleanup;
  if (databind_component_provider_candidate_render(
          header, source, component, &ir,
          service_basename, component_basename,
          config->version_major, config->version_minor,
          config->version_patch) != 0 ||
      fflush(header) != 0 || fflush(source) != 0)
    goto cleanup;
  result = 0;

cleanup:
  if (header != NULL && fclose(header) != 0)
    result = -1;
  if (source != NULL && fclose(source) != 0)
    result = -1;
  databind_compiler_service_native_destroy(&ir);
  return result;
}

/* Never self-publish the two-file ComponentProvider. The only supported
 * entry is the audited pair-stage contract above, invoked by the same
 * coordinator that owns PLUGIN and the native Record header. */
int databind_compiler_component_provider_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  (void)input;
  (void)request;
  (void)context;
  return -1;
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_COMPONENT_PROVIDER_BACKEND = {
        {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
         DATABIND_COMPILER_ARTIFACT_COMPONENT},
        "component",
        databind_compiler_component_provider_generate,
        NULL,
        DATABIND_COMPILER_OUTPUT_STAGED_MULTI,
};
