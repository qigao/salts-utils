#include <cmeta_cmeta_data.h>

const cmeta_type_desc *native_identity_alpha(void);
const cmeta_type_desc *native_identity_alpha_copy(void);
const cmeta_type_desc *native_identity_beta(void);
const cmeta_type_desc *native_identity_alpha_v2(void);

int main(void) {
  const cmeta_type_desc *a = native_identity_alpha();
  const cmeta_type_desc *copy = native_identity_alpha_copy();
  const cmeta_type_desc *b = native_identity_beta();
  const cmeta_type_desc *v2 = native_identity_alpha_v2();
  if (!cmeta_type_desc_valid(a) ||
      !cmeta_type_desc_valid(copy) ||
      !cmeta_type_desc_valid(b) ||
      !cmeta_type_desc_valid(v2))
    return 1;
  /* Descriptor addresses are distinct, but the same named Contract version
   * must retain equal CMeta identity across compilation units. */
  if (a == copy || !cmeta_type_equal(a, copy))
    return 2;
  /* Identical C struct layouts and names in distinct Contract namespaces
   * are different semantic types. So are distinct contract versions. */
  if (cmeta_type_equal(a, b) || cmeta_type_equal(a, v2) ||
      cmeta_type_equal(b, v2))
    return 3;
  return 0;
}
