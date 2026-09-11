#include <data_bind.h>
#include <tbe_typed.h>
#include <datetime_parser.h>
#include <query_vm.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  const char path[] = "databind-native-consumer.tbe";
  const char schema[] = "message Event { datetime at; uint32 id; string name; }\n";
  const char json[] = "{\"name\":\"native\",\"id\":7,\"at\":\"Sat, 04 Mar 2006 13:27:54 GMT\"}";
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *bind = NULL;
  DataBindValue *value = NULL;
  datetime_t date;
  FILE *file = fopen(path, "wb");
  int failed = 1;
  if (!file) return 1;
  size_t written = fwrite(schema, 1, sizeof(schema) - 1, file);
  int close_result = fclose(file);
  if (written != sizeof(schema) - 1 || close_result != 0) goto cleanup;
  if (limits.max_steps != QVM_DEFAULT_MAX_STEPS || diagnostic.status != QVM_STATUS_OK)
    goto cleanup;
  if (data_bind_create(path, &bind, &error) != DATA_BIND_OK) goto cleanup;
  if (data_bind_parse_json(bind, "Event", json, sizeof(json) - 1, &value, &error) != DATA_BIND_OK)
    goto cleanup;
  if (!data_bind_value_as_datetime(data_bind_value_get(value, "at"), &date) || date.year != 2006)
    goto cleanup;
  if (data_bind_value_as_int(data_bind_value_get(value, "id")) != 7) goto cleanup;
  const char *name = data_bind_value_as_string(data_bind_value_get(value, "name"));
  if (!name || strcmp(name, "native") != 0) goto cleanup;
  data_bind_value_free(value);
  value = NULL;
  const char xml[] = "<Event><at>Sat, 04 Mar 2006 13:27:54 GMT</at><id>7</id><name/></Event>";
  if (data_bind_parse_xml(bind, "Event", xml, sizeof(xml) - 1, &value, &error) != DATA_BIND_OK)
    goto cleanup;
  name = data_bind_value_as_string(data_bind_value_get(value, "name"));
  if (!name || name[0] != '\0') goto cleanup;
  failed = 0;
cleanup:
  if (failed) fprintf(stderr, "Installed native DataBind consumer failed: %s\n", error.message);
  data_bind_value_free(value);
  data_bind_free(bind);
  remove(path);
  return failed;
}
