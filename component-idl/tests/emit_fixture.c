#include "component_idl_codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_idl(const char *path, size_t *out_len) {
    FILE *f;
    char *data;
    long length;
    size_t n;
    if (!out_len || !(f = fopen(path, "rb"))) return NULL;
    if (fseek(f, 0L, SEEK_END) || (length = ftell(f)) <= 0 ||
        length > 1048576L || fseek(f, 0L, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    data = (char *)malloc((size_t)length + 1u);
    if (!data) { fclose(f); return NULL; }
    n = fread(data, 1u, (size_t)length, f);
    if (fclose(f) || n != (size_t)length) {
        free(data);
        return NULL;
    }
    data[n] = '\0';
    *out_len = n;
    return data;
}

int main(int argc, char **argv) {
    size_t len = 0u;
    char *source;
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    FILE *out = NULL;
    int status = 1;
    const int provider_plugin =
        argc == 6 && strcmp(argv[5], "--provider-plugin") == 0;
    if (argc != 5 && !provider_plugin) return 2;
    source = read_idl(argv[1], &len);
    if (!source) return 3;
    /* The canonical IDL parser returns boolean success, not IdlStatus. */
    if (!idl_contract_parse(source, len, &contract, &diagnostic)) {
        fprintf(stderr, "component-idl: IDL parse failed: %s\n",
                diagnostic.message);
        goto done;
    }
    if (salts_component_idl_candidate_admit(
            contract, argv[2], argv[3]) != 0) {
        fprintf(stderr, "component-idl: unsupported Component or Service contract: %s\n",
                argv[2]);
        goto done;
    }
    out = fopen(argv[4], "wb");
    if (!out) goto done;
    if ((provider_plugin
             ? salts_component_idl_candidate_render_provider_plugin(
                   out, contract, argv[2], argv[3])
             : salts_component_idl_candidate_render(
                   out, contract, argv[2], argv[3])) == 0 &&
        fflush(out) == 0)
        status = 0;
done:
    if (out && fclose(out)) status = 1;
    if (status) (void)remove(argv[4]);
    idl_contract_destroy(contract);
    free(source);
    return status;
}
