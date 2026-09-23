#include "node_tree.h"
#include "schema_cmeta.h"

#include <cstring>

/* Mutation: removing node_tree.h's own C linkage guards mangles Node calls
 * even though schema_cmeta_field_resolve itself still has C linkage. The
 * implementation must be compiled as C, never included or compiled as C++. */
int main() {
    Node *root = create_node_map("root");
    Node *field = create_node_map(nullptr);
    Node *type = create_node_string("type", "int32");
    schema_cmeta_field_type resolved = {};
    int status = 0;

    if (!root || !field || !type) {
        node_free(type);
        node_free(field);
        node_free(root);
        return 1;
    }
    if (map_add(field, type) != 0) {
        node_free(type);
        node_free(field);
        node_free(root);
        return 2;
    }
    if (!schema_cmeta_field_resolve(root, field, &resolved)) {
        status = 3;
    } else if (resolved.kind != CMETA_DATA_SINT || !resolved.data ||
               std::strcmp(resolved.data->stable_id, "salts.int32.data") != 0) {
        status = 4;
    }
    node_free(field);
    node_free(root);
    return status;
}
