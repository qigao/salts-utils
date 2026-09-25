#include <cmeta/cmeta.h>
#include <data_bind.h>
#include <data_bind_projection_plan.h>
#include <salts_uuid.h>
#include <tbe_wire.h>

#include <stdint.h>

int main(void) {
  static const char schema[] =
      "schema ProjectionPlanConsumer [version(1)];"
      "message Request {"
      " optional uint32 optional_id;"
      " nullable uint32 nullable_id;"
      " optional nullable uint32 tri_id;"
      "}"
      "message Response { uint32 value; }"
      "service Store { Read: Request -> Response; }";
  uint8_t storage[4] = {0};
  salts_uuid_t uuid = {{0}};
  DataBind *codec = NULL;
  DataBindFormatPlan *format = NULL;
  DataBindFormatPlanInfo info = DATA_BIND_FORMAT_PLAN_INFO_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  int ok = 0;

  tbe_wire_write_u32(storage, 0, 42u);
  if (data_bind_create_from_text(
          schema, sizeof(schema) - 1u, &codec, &error) != DATA_BIND_OK)
    return 2;
  if (data_bind_format_plan_compile(
          codec, "Request", DATA_BIND_FORMAT_JSON, &format, &error) !=
          DATA_BIND_OK)
    goto cleanup;
  if (!data_bind_format_plan_info(format, &info))
    goto cleanup;

  ok = data_bind_abi_version() == DATA_BIND_ABI_VERSION &&
       tbe_wire_read_u32(storage, 0) == 42u &&
       uuid.bytes[0] == 0u &&
       cmeta_type_desc_valid(&cmeta_type_int) &&
       info.abi_version == DATA_BIND_PROJECTION_PLAN_ABI_VERSION &&
       info.format == DATA_BIND_FORMAT_JSON &&
       info.has_nullable;

cleanup:
  data_bind_format_plan_free(format);
  data_bind_free(codec);
  return ok ? 0 : 1;
}
