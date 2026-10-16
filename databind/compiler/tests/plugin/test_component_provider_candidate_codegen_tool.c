#include "compiler_core.h"
#include "component_provider_candidate.h"
#include "service_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A Component can select a Service only through canonical IdlContract IR.
 * This is not an IDL parser or a new capability source. */
static int selected_service(void *context, const char *service_name) {
  const IdlComponent *component = (const IdlComponent *)context;
  size_t i;
  if (component == NULL || service_name == NULL) return 0;
  for (i = 0u; i < component->capability_count; ++i)
    if (component->capabilities[i].kind == IDL_CAPABILITY_SERVICE &&
        component->capabilities[i].name != NULL &&
        strcmp(component->capabilities[i].name, service_name) == 0)
      return 1;
  return 0;
}

int main(int argc, char **argv) {
  Node *root = NULL;
  IdlContract *contract = NULL;
  char *schema_data = NULL;
  const IdlComponent *component = NULL;
  databind_compiler_service_native_ir ir = {0};
  FILE *header = NULL;
  FILE *source = NULL;
  int rc = 1;

  if (argc != 6) {
    fprintf(stderr,
        "usage: %s <schema> <canonical component> <original service-header basename> <candidate-header> <candidate-source>\n",
        argc > 0 ? argv[0] : "component-provider-codegen");
    return 2;
  }
  if (databind_compiler_parse_contract_file(
          argv[1], &root, &contract, &schema_data) != 0)
    goto cleanup;
  component = idl_contract_find_component(contract, argv[2]);
  if (component == NULL ||
      databind_compiler_service_native_build_selected(
          contract, selected_service, (void *)component, &ir) != 0)
    goto cleanup;

  /* Deliberately forbid implicit multiservice, typed-error or inferred ABI
   * fallback. Reject the entire artifact before opening output files. */
  if (!databind_component_provider_candidate_admitted(component, &ir))
    goto cleanup;
  {
    databind_compiler_service_native_ir malformed = ir;
    databind_compiler_service_native_operation wrong = ir.operations[0];
    /* Upper bound, duplicate native slot, foreign Service and typed errors
     * all reject before the first output file is opened. */
    malformed.operation_count = 5u;
    if (databind_component_provider_candidate_admitted(
            component, &malformed))
      goto cleanup;
    {
      databind_compiler_service_native_operation duplicated[2] = {
        ir.operations[0], ir.operations[0]
      };
      malformed.operation_count = 2u;
      malformed.operations = duplicated;
      if (databind_component_provider_candidate_admitted(
              component, &malformed))
        goto cleanup;
    }
    malformed.operation_count = 1u;
    malformed.operations = &wrong;
    wrong.error_count = 1u;
    if (databind_component_provider_candidate_admitted(
            component, &malformed))
      goto cleanup;
    wrong.error_count = 0u;
    wrong.service_name = "NotTheChosenService";
    if (databind_component_provider_candidate_admitted(
            component, &malformed))
      goto cleanup;
  }

  header = fopen(argv[4], "wb");
  if (header == NULL) goto cleanup;
  source = fopen(argv[5], "wb");
  if (source == NULL) goto cleanup;

  {
    const char *name = strrchr(argv[4], '/');
    const char *windows_name = strrchr(argv[4], '\\');
    if (windows_name != NULL && (name == NULL || windows_name > name))
      name = windows_name;
    name = name != NULL ? name + 1 : argv[4];
    if (databind_component_provider_candidate_render(
            header, source, component, &ir, argv[3], name,
            1u, 0u, 0u) != 0)
      goto cleanup;
  }
  /* Emission has no fallthrough to an ABI-only plugin export. */
  if (fflush(header) != 0 || fflush(source) != 0)
    goto cleanup;
  if (fclose(header) != 0) { header = NULL; goto cleanup; }
  header = NULL;
  if (fclose(source) != 0) { source = NULL; goto cleanup; }
  source = NULL;
  rc = 0;

cleanup:
  if (header != NULL) (void)fclose(header);
  if (source != NULL) (void)fclose(source);
  if (rc != 0) {
    (void)remove(argv[4]);
    (void)remove(argv[5]);
    fprintf(stderr, "ComponentProvider candidate: unsupported or failed, no artifact published\n");
  }
  databind_compiler_service_native_destroy(&ir);
  idl_contract_destroy(contract);
  node_free(root);
  free(schema_data);
  return rc;
}
