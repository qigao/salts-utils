#include "data_bind_native_descriptor.h"
#include <tinytest.h>
#include <salts_cmeta_fixed_width.h>

#include <string.h>

spec("DataBind native descriptor") {
  it("accepts canonical CMeta metadata without format state") {
    const DataBindNativeDescriptor descriptor =
        DATA_BIND_NATIVE_DESCRIPTOR_INIT("Sample.Count", &salts_uint32_cmeta_data);
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(data_bind_native_descriptor_validate(&descriptor, &error),
                DATA_BIND_OK);
  }

  it("fails closed for missing native identity") {
    const DataBindNativeDescriptor descriptor =
        DATA_BIND_NATIVE_DESCRIPTOR_INIT("Sample.Count", NULL);
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(data_bind_native_descriptor_validate(&descriptor, &error),
                DATA_BIND_ERR_SCHEMA);
  }

  it("accepts format-neutral presence/null overlays") {
    const DataBindNativeStateSlot presence[] = {
        {sizeof(DataBindNativeStateSlot), "value", 8u, 0u}};
    const DataBindNativeStateSlot nulls[] = {
        {sizeof(DataBindNativeStateSlot), "value", 9u, 0u}};
    DataBindNativeDescriptor descriptor =
        DATA_BIND_NATIVE_DESCRIPTOR_INIT("Sample", &salts_uint32_cmeta_data);
    DataBindError error = DATA_BIND_ERROR_INIT;
    descriptor.presence = presence;
    descriptor.presence_count = 1u;
    descriptor.nulls = nulls;
    descriptor.null_count = 1u;
    check_equal(data_bind_native_descriptor_validate(&descriptor, &error),
                DATA_BIND_OK);
  }

  it("rejects incomplete state overlays") {
    DataBindNativeDescriptor descriptor =
        DATA_BIND_NATIVE_DESCRIPTOR_INIT("Sample", &salts_uint32_cmeta_data);
    DataBindError error = DATA_BIND_ERROR_INIT;
    descriptor.presence_count = 1u;
    check_equal(data_bind_native_descriptor_validate(&descriptor, &error),
                DATA_BIND_ERR_SCHEMA);
  }

  it("fails closed for ABI mismatch") {
    DataBindNativeDescriptor descriptor =
        DATA_BIND_NATIVE_DESCRIPTOR_INIT("Sample.Count", &salts_uint32_cmeta_data);
    DataBindError error = DATA_BIND_ERROR_INIT;
    descriptor.abi_version = DATA_BIND_NATIVE_DESCRIPTOR_ABI_VERSION + 1u;
    check_equal(data_bind_native_descriptor_validate(&descriptor, &error),
                DATA_BIND_ERR_SCHEMA);
  }
}
