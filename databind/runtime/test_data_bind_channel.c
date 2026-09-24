#include "data_bind.h"
#include "tinytest.h"

#include <stddef.h>

spec("DataBind Channel reflection") {
  it("reflects Channels and mixed Component capabilities") {
    static const char schema[] =
        "schema Device [version(1)];"
        "component DeviceRuntime {"
        " service Control;"
        " channel Telemetry;"
        "}"
        "message TelemetryEvent { uint32 sequence; }"
        "message PingRequest { uint32 value; }"
        "message PingResponse { uint32 value; }"
        "service Control { Ping: PingRequest -> PingResponse; }"
        "channel Telemetry: TelemetryEvent;";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindChannel channel = DATA_BIND_CHANNEL_INIT;
    DataBindComponent component = DATA_BIND_COMPONENT_INIT;
    DataBindComponentCapability capability =
        DATA_BIND_COMPONENT_CAPABILITY_INIT;

    check_equal(data_bind_create_from_text(
                    schema, sizeof(schema) - 1u, &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);

    check_equal(data_bind_channel_count(codec), (size_t)1u);
    check(data_bind_channel_at(codec, 0u, &channel) == 1);
    check_equal(channel.name, "Telemetry");
    check_equal(channel.qualified_name, "Device.Telemetry");
    check_equal(channel.message_type, "TelemetryEvent");

    channel = (DataBindChannel)DATA_BIND_CHANNEL_INIT;
    check(data_bind_channel_find(codec, "Telemetry", &channel) == 1);
    check_equal(channel.qualified_name, "Device.Telemetry");

    check(data_bind_component_find(
              codec, "DeviceRuntime", &component) == 1);
    check_equal(component.capability_count, (size_t)2u);

    check(data_bind_component_capability_find(
              codec, "DeviceRuntime",
              DATA_BIND_COMPONENT_CAPABILITY_CHANNEL,
              "Telemetry", &capability) == 1);
    check_equal(capability.kind,
                DATA_BIND_COMPONENT_CAPABILITY_CHANNEL);
    check_equal(capability.name, "Telemetry");
    check_equal(capability.qualified_name, "Device.Telemetry");
    check_equal(data_bind_component_capability_kind_name(capability.kind),
                "channel");

    capability =
        (DataBindComponentCapability)DATA_BIND_COMPONENT_CAPABILITY_INIT;
    check(data_bind_component_capability_find(
              codec, "DeviceRuntime",
              DATA_BIND_COMPONENT_CAPABILITY_SERVICE,
              "Control", &capability) == 1);
    check_equal(capability.kind,
                DATA_BIND_COMPONENT_CAPABILITY_SERVICE);

    data_bind_free(codec);
  }

  it("honors size-prefix Channel reflection") {
    static const char schema[] =
        "message Event { uint32 value; }"
        "channel Events: Event;";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindChannel channel = DATA_BIND_CHANNEL_INIT;

    check_equal(data_bind_create_from_text(
                    schema, sizeof(schema) - 1u, &codec, &error),
                DATA_BIND_OK);

    channel.size = offsetof(DataBindChannel, qualified_name);
    channel.qualified_name = "untouched";
    channel.message_type = "untouched";
    check(data_bind_channel_at(codec, 0u, &channel) == 1);
    check_equal(channel.name, "Events");
    check_equal(channel.qualified_name, "untouched");
    check_equal(channel.message_type, "untouched");

    data_bind_free(codec);
  }

  it("clears reflected Channel output on failed lookup") {
    static const char schema[] =
        "message Event { uint32 value; }"
        "channel Events: Event;";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindChannel channel = DATA_BIND_CHANNEL_INIT;

    check_equal(data_bind_create_from_text(
                    schema, sizeof(schema) - 1u, &codec, &error),
                DATA_BIND_OK);
    check(data_bind_channel_find(codec, "Missing", &channel) == 0);
    check_null(channel.name);
    check_null(channel.qualified_name);
    check_null(channel.message_type);
    data_bind_free(codec);
  }

  it("preserves schemas without Channels") {
    static const char schema[] =
        "message Request { uint32 value; }"
        "message Response { uint32 value; }"
        "service S { Op: Request -> Response; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_create_from_text(
                    schema, sizeof(schema) - 1u, &codec, &error),
                DATA_BIND_OK);
    check_equal(data_bind_channel_count(codec), (size_t)0u);
    data_bind_free(codec);
  }
}
