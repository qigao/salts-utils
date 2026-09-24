#include "data_bind.h"
#include "tinytest.h"

spec("DataBind Component reflection") {
  it("enumerates canonical Component capability references") {
    static const char schema[] =
        "schema Image [version(1)];"
        "component ImageProcessor {"
        " service Codec;"
        " service Metadata;"
        "}"
        "message Request { uint32 id; }"
        "message Response { uint32 value; }"
        "service Codec { Decode: Request -> Response; }"
        "service Metadata { Inspect: Request -> Response; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindComponent component = DATA_BIND_COMPONENT_INIT;
    DataBindComponentCapability capability =
        DATA_BIND_COMPONENT_CAPABILITY_INIT;

    check_equal(data_bind_create_from_text(
                    schema, sizeof(schema) - 1u, &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);

    check_equal(data_bind_component_count(codec), (size_t)1u);
    check(data_bind_component_at(codec, 0u, &component) == 1);
    check_equal(component.name, "ImageProcessor");
    check_equal(component.qualified_name, "Image.ImageProcessor");
    check_equal(component.capability_count, (size_t)2u);

    component = (DataBindComponent)DATA_BIND_COMPONENT_INIT;
    check(data_bind_component_find(
              codec, "ImageProcessor", &component) == 1);
    check_equal(component.qualified_name, "Image.ImageProcessor");

    check_equal(data_bind_component_capability_count(
                    codec, "ImageProcessor"),
                (size_t)2u);
    check(data_bind_component_capability_at(
              codec, "ImageProcessor", 0u, &capability) == 1);
    check_equal(capability.kind,
                DATA_BIND_COMPONENT_CAPABILITY_SERVICE);
    check_equal(capability.name, "Codec");
    check_equal(capability.qualified_name, "Image.Codec");

    capability =
        (DataBindComponentCapability)DATA_BIND_COMPONENT_CAPABILITY_INIT;
    check(data_bind_component_capability_find(
              codec, "ImageProcessor",
              DATA_BIND_COMPONENT_CAPABILITY_SERVICE,
              "Metadata", &capability) == 1);
    check_equal(capability.qualified_name, "Image.Metadata");
    check_equal(data_bind_component_capability_kind_name(capability.kind),
                "service");

    capability =
        (DataBindComponentCapability)DATA_BIND_COMPONENT_CAPABILITY_INIT;
    check(data_bind_component_capability_find(
              codec, "ImageProcessor",
              DATA_BIND_COMPONENT_CAPABILITY_SERVICE,
              "Missing", &capability) == 0);
    check_null(capability.name);

    data_bind_free(codec);
  }

  it("preserves schemas without Components") {
    static const char schema[] =
        "message Request { uint32 id; }"
        "message Response { uint32 value; }"
        "service Codec { Decode: Request -> Response; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_create_from_text(
                    schema, sizeof(schema) - 1u, &codec, &error),
                DATA_BIND_OK);
    check_equal(data_bind_component_count(codec), (size_t)0u);
    data_bind_free(codec);
  }
}
