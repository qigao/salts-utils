#include "data_bind.h"
#include "tinytest.h"

#include <string.h>

spec("DataBind schema reflection contract") {
  it("keeps structural reflection distinct from schema overlay metadata") {
    static const char schema[] =
        "schema Market [id(1), version(2), byte_order(little)];"
        "enum Side <uint8> { Buy = 1; Sell = 2; }"
        "message Order { uint32 id; optional uint32 venue default 7; Side side; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindSchemaEnumItem item = DATA_BIND_SCHEMA_ENUM_ITEM_INIT;
    DataBindSchemaAttribute attr = DATA_BIND_SCHEMA_ATTRIBUTE_INIT;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error), DATA_BIND_OK);
    check_not_null(codec);

    check_equal(data_bind_schema_name(codec), "Market");
    check_equal(data_bind_schema_attribute_count(codec), 3u);
    check(data_bind_schema_attribute_at(codec, 0u, &attr) == 1);
    check_equal(attr.name, "id");
    check_equal(attr.value, "1");

    check(data_bind_schema_find_type(codec, "Order", &type) == 1);
    check_equal(type.name, "Order");
    check_equal(type.kind, DATA_BIND_SCHEMA_MESSAGE);
    check_equal(type.field_count, 3u);

    check(data_bind_schema_field_at(codec, "Order", 0u, &field) == 1);
    check_equal(field.name, "id");
    check_equal(field.type, "uint32");
    check_equal(field.is_optional, 0);
    check_equal(field.has_default, 0);

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "Order", 1u, &field) == 1);
    check_equal(field.name, "venue");
    check_equal(field.type, "uint32");
    check_equal(field.is_optional, 1);
    check_equal(field.has_default, 1);
    check_equal(field.default_value, "7");

    type = (DataBindSchemaType)DATA_BIND_SCHEMA_TYPE_INIT;
    check(data_bind_schema_find_type(codec, "Side", &type) == 1);
    check_equal(type.kind, DATA_BIND_SCHEMA_ENUM);
    check_equal(type.underlying_type, "uint8");
    check_equal(type.item_count, 2u);

    check(data_bind_schema_enum_item_at(codec, "Side", 0u, &item) == 1);
    check_equal(item.name, "Buy");
    check_equal(item.value, "1");

    data_bind_free(codec);
  }
}
