#include "cbind_standalone.h"

int main() {
  CBindStandaloneEnvelope_t envelope{};
  const cmeta_data_desc *descriptor = CBindStandaloneEnvelope_cbind_data();

  if (!cmeta_data_desc_valid(descriptor)) return 1;
  if (envelope.details.sequence != 0 || envelope.label != nullptr || envelope.event_id != 0) return 2;
  return 0;
}
