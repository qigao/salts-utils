#include "component_idl_codegen.h"
#include <stdio.h>

int main(void) {
    const IdlComponentCapability capability = {
        IDL_CAPABILITY_CHANNEL, "Events", "Example.Events"
    };
    const IdlComponent component = {
        "Bad", "Example.Bad", 1u, &capability, 0u, NULL
    };
    IdlContract contract = {0};
    FILE *file;
    long size;
    contract.size = sizeof(contract);
    contract.abi_version = IDL_CONTRACT_ABI_VERSION;
    contract.name = "Example";
    contract.version = "1";
    contract.component_count = 1u;
    contract.components = &component;

    file = tmpfile();
    if (file == NULL) return 1;
    if (salts_component_idl_candidate_admit(
            &contract, "Example.Bad", "messages_native.h") == 0 ||
        salts_component_idl_candidate_render(
            file, &contract, "Example.Bad", "messages_native.h") == 0) {
        fclose(file);
        return 2;
    }
    size = ftell(file);
    if (size != 0L) { fclose(file); return 3; }
    if (salts_component_idl_candidate_admit(
            &contract, "Example.Bad", "../escape.h") == 0) {
        fclose(file);
        return 4;
    }
    return fclose(file) == 0 ? 0 : 5;
}
