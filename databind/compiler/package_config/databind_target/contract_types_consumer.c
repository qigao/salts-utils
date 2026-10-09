#include "contract_types_native.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* This binary links ONLY against installed Salts::CSTL via the public
 * salts_idl_target(TYPES) target. No generated Binary wire source is present. */
int main(void) {
    NativeBatch original, copy;
    NativeEntry source;
    char label_bytes[] = "alpha";
    const databind_native_text key = {label_bytes, 5u};
    const databind_native_text lookup = {(char *)"alpha", 5u};
    const NativeEntry *cloned_item;
    const NativeEntry *mapped_item;

    NativeBatch_init(&original);
    NativeBatch_init(&copy);
    NativeEntry_init(&source);
    source.label.data = (char *)malloc(6u);
    if (source.label.data == NULL) return 1;
    memcpy(source.label.data, "alpha", 6u);
    source.label.size = 5u;
    source.id = 42u;

    original.by_name.key_type = &databind_native_text_cmeta_type;
    original.by_name.value_type = &NativeEntry_native_element_cmeta_type;
    if (vec_raw_init(&original.items, &NativeEntry_native_element_cmeta_type, 8u) != STL_OK ||
        map_init(&original.by_name, 8u) != STL_OK ||
        vec_push(&original.items, &source) != STL_OK ||
        map_put(&original.by_name, &key, &source) != STL_OK)
        return 2;
    NativeEntry_clear(&source);

    if (NativeBatch_clone(&copy, &original) != 0 ||
        vec_size(&copy.items) != 1u ||
        map_size(&copy.by_name) != 1u ||
        copy.items.data == original.items.data ||
        copy.by_name.impl == original.by_name.impl)
        return 3;
    cloned_item = (const NativeEntry *)vec_at_const(&copy.items, 0u);
    mapped_item = (const NativeEntry *)map_get_const(&copy.by_name, &lookup);
    if (cloned_item == NULL || mapped_item == NULL ||
        cloned_item->id != 42u || mapped_item->id != 42u ||
        cloned_item->label.data == NULL || mapped_item->label.data == NULL ||
        strcmp(cloned_item->label.data, "alpha") != 0 ||
        strcmp(mapped_item->label.data, "alpha") != 0 ||
        cloned_item->label.data ==
            ((const NativeEntry *)vec_at_const(&original.items, 0u))->label.data)
        return 4;
    label_bytes[0] = 'Z';
    if (!map_get_const(&copy.by_name, &lookup))
        return 5;

    NativeBatch_clear(&original);
    NativeBatch_clear(&copy);
    NativeBatch_clear(&copy);
    return original.items.initialized || copy.by_name.impl ? 6 : 0;
}
