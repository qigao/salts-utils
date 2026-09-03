#include "salts_cron.h"

#include <stdio.h>

int main(void) {
  salts_cron_expr_t expr;
  time_t next_fire = 0;
  char buf[32];
  int rc;

  rc = salts_cron_parse("*/30 * * * *", &expr);
  if (rc != SALTS_CRON_OK) {
    fprintf(stderr, "salts_cron_parse failed: %d (%s)\n", rc,
            salts_cron_strerror(rc));
    return 1;
  }

  rc = salts_cron_next(&expr, time(NULL), &next_fire);
  if (rc != SALTS_CRON_OK) {
    fprintf(stderr, "salts_cron_next failed: %d (%s)\n", rc,
            salts_cron_strerror(rc));
    return 2;
  }

  if (salts_cron_format_time(next_fire, buf, sizeof(buf), NULL) < 0) {
    return 3;
  }

  printf("next fire: %s\n", buf);
  return 0;
}
