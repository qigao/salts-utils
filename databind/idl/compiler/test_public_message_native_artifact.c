#include "tinytest.h"

#include "message_native_artifact_native.h"

#include <stddef.h>
#include <string.h>

spec("DataBind public Message native artifact") {
  it("publishes exact optional and nullable overlays") {
    const DataBindMessageNativeArtifact *artifact = Event_native_artifact();
    DataBindNativeTypeBinding binding = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *data = NULL;

    check_not_null(artifact);
    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->type_name, "Event");
    check_not_null(artifact->native_binding);
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_equal(Event_cmeta_data(&data, &error), DATA_BIND_OK);
    check_true(binding.data == data);
    check_equal(binding.idl_type_name, "Event");

    check_equal(binding.presence_count, (size_t)2u);
    check_not_null(binding.presence);
    check_equal(binding.presence[0].field_name, "sequence");
    check_equal(binding.presence[0].byte_offset, offsetof(Event_t, _presence));
    check_equal(binding.presence[0].bit, 0u);
    check_equal(binding.presence[1].field_name, "tri");
    check_equal(binding.presence[1].byte_offset, offsetof(Event_t, _presence));
    check_equal(binding.presence[1].bit, 1u);

    check_equal(binding.null_count, (size_t)2u);
    check_not_null(binding.nulls);
    check_equal(binding.nulls[0].field_name, "result");
    check_equal(binding.nulls[0].byte_offset, offsetof(Event_t, _nulls));
    check_equal(binding.nulls[0].bit, 0u);
    check_equal(binding.nulls[1].field_name, "tri");
    check_equal(binding.nulls[1].byte_offset, offsetof(Event_t, _nulls));
    check_equal(binding.nulls[1].bit, 1u);
  }
}
