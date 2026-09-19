#include "data_bind.h"

#ifdef SALTS_QUERY_VM_H
#error "data_bind.h must not expose QueryVM implementation headers"
#endif

#ifdef DATETIME_PARSER_H
#error "data_bind.h must not expose DateTime parser implementation headers"
#endif

int main(void) {
  DataBindDateTime datetime = {0};
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;

  datetime.year = 2026;
  return datetime.year == 2026 &&
                 limits.max_steps == DATA_BIND_QUERY_DEFAULT_MAX_STEPS &&
                 diagnostic.status == DATA_BIND_QUERY_OK
             ? 0
             : 1;
}
