#include "tinytest.h"
#include "schema_cmeta.h"

#include <cmeta/data.h>
#include <cmeta_cmeta_data.h>
#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_float_descriptor(const char *name,
                                   const cmeta_data_desc *canonical,
                                   const cmeta_type_desc *storage_type,
                                   uint8_t bits) {
  const cmeta_data_desc *data = schema_cmeta_builtin_data(name);
  cmeta_data_kind kind = CMETA_DATA_BOOL;

  check_true(data != NULL);
  if (data == NULL) return;

  check_true(data == canonical);
  check_true(cmeta_data_desc_valid(data));
  check_equal(data->kind, CMETA_DATA_FLOAT);
  check_true(data->storage_type == storage_type);
  check_true(data->shape != NULL);
  if (data->shape != NULL)
    check_equal(((const cmeta_data_float_shape *)data->shape)->bits, bits);
  check_true(schema_cmeta_data_kind(name, &kind));
  check_equal(kind, data->kind);
}

suite("schema_cmeta") {
  describe("canonical builtin scalar lowering") {
    it("maps bool to the canonical CMeta boolean storage descriptor") {
      const cmeta_data_desc *data = schema_cmeta_builtin_data("bool");
      cmeta_data_kind kind = CMETA_DATA_FLOAT;

      check_true(data != NULL);
      if (data != NULL) {
        check_true(data == &cmeta_data_bool);
        check_true(cmeta_data_desc_valid(data));
        check_equal(data->kind, CMETA_DATA_BOOL);
        check_true(data->storage_type == &cmeta_type_bool);
        check_null(data->shape);
        check_true(schema_cmeta_data_kind("bool", &kind));
        check_equal(kind, data->kind);
      }
    }

    it("maps float and f32 to canonical 32-bit CMeta storage") {
      check_float_descriptor("float", &cmeta_data_float, &cmeta_type_float, 32u);
      check_float_descriptor("f32", &cmeta_data_float, &cmeta_type_float, 32u);
    }

    it("maps double and f64 to canonical 64-bit CMeta storage") {
      check_float_descriptor("double", &cmeta_data_double, &cmeta_type_double, 64u);
      check_float_descriptor("f64", &cmeta_data_double, &cmeta_type_double, 64u);
    }

    it("maps uuid to the process-wide canonical Core descriptor") {
      check_true(schema_cmeta_builtin_data("uuid") == &cmeta_uuid_cmeta_data);
      check_true(cmeta_uuid_cmeta_data_valid(schema_cmeta_builtin_data("uuid")));
    }

    it("rejects unsupported or invalid scalar names explicitly") {
      check_null(schema_cmeta_builtin_data("varint"));
      check_null(schema_cmeta_builtin_data("not-a-type"));
      check_null(schema_cmeta_builtin_data(NULL));
    }

    it("does not choose string or bytes storage from kind classification") {
      cmeta_data_kind kind = CMETA_DATA_BOOL;

      check_true(schema_cmeta_data_kind("string", &kind));
      check_equal(kind, CMETA_DATA_STRING);
      check_null(schema_cmeta_builtin_data("string"));
      check_true(schema_cmeta_data_kind("bytes", &kind));
      check_equal(kind, CMETA_DATA_BYTES);
      check_null(schema_cmeta_builtin_data("bytes"));
    }
  }

  describe("canonical structural descriptor lowering") {
    it("builds a CMeta struct descriptor from structural metadata only") {
      static const cmeta_field_desc layout_fields[] = {
        {"id", "int", 0u, sizeof(int), CMETA_ALIGNOF(int), &cmeta_type_int, NULL}
      };
      static const cmeta_struct_desc layout = {
        "Order", sizeof(int), CMETA_ALIGNOF(int), layout_fields, 1u
      };
      static const cmeta_data_field_desc fields[] = {
        {"schema.Order.id", "id", 0u, &cmeta_data_int}
      };
      cmeta_data_struct_shape shape = {0};
      cmeta_data_desc data = {0};

      check_true(schema_cmeta_struct_data(&data, &shape,
                                         "schema.Order", "Order",
                                         &cmeta_type_int, &layout,
                                         fields, 1u));
      check_equal(data.kind, CMETA_DATA_STRUCT);
      check_true(strcmp(data.stable_id, "schema.Order") == 0);
      check_true(data.storage_type == &cmeta_type_int);
      check_true(data.shape == &shape);
      check_true(shape.layout == &layout);
      check_true(shape.fields == fields);
      check_equal(shape.field_count, 1u);
      check_true(cmeta_data_desc_valid(&data));
    }

    it("builds a CMeta enum descriptor without schema wire metadata") {
      static const cmeta_enum_item_desc items[] = {
        {1, "ORDER_OPEN", "open"}, {2, "ORDER_CLOSED", "closed"}
      };
      static const cmeta_enum_desc meta = {"OrderState", items, 2u};
      cmeta_data_enum_shape shape = {0};
      cmeta_data_desc data = {0};

      check_true(schema_cmeta_enum_data(&data, &shape,
                                       "schema.OrderState", "OrderState",
                                       &cmeta_type_int, &meta));
      check_equal(data.kind, CMETA_DATA_ENUM);
      check_true(strcmp(data.stable_id, "schema.OrderState") == 0);
      check_true(data.storage_type == &cmeta_type_int);
      check_true(data.shape == &shape);
      check_true(shape.meta == &meta);
      check_true(cmeta_data_desc_valid(&data));
    }

    it("rejects incomplete structural inputs without publishing outputs") {
      cmeta_data_desc data = {sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
                              "sentinel", "sentinel", CMETA_DATA_BOOL,
                              &cmeta_type_int, NULL, NULL, NULL, NULL};
      cmeta_data_desc original = data;
      cmeta_data_struct_shape struct_shape = {NULL, NULL, 7u};
      cmeta_data_struct_shape original_struct_shape = struct_shape;
      cmeta_data_enum_shape enum_shape = {NULL};
      cmeta_data_enum_shape original_enum_shape = enum_shape;

      check_false(schema_cmeta_struct_data(&data, &struct_shape,
                                          NULL, "Order", &cmeta_type_int,
                                          NULL, NULL, 0u));
      check_true(memcmp(&data, &original, sizeof(data)) == 0);
      check_true(memcmp(&struct_shape, &original_struct_shape,
                        sizeof(struct_shape)) == 0);

      check_false(schema_cmeta_enum_data(&data, &enum_shape,
                                        "schema.OrderState", "OrderState",
                                        &cmeta_type_int, NULL));
      check_true(memcmp(&data, &original, sizeof(data)) == 0);
      check_true(memcmp(&enum_shape, &original_enum_shape,
                        sizeof(enum_shape)) == 0);
    }
  }

  describe("canonical value generic lowering") {
    it("enforces the canonical tuple upper arity bound") {
      const cmeta_type_identity atom = CMETA_TYPE_ID_ATOM_INIT("schema.Value");
      const cmeta_type_identity *args[17];
      cmeta_type_identity identity = {0};
      cmeta_type_identity original;
      size_t i;
      for (i = 0; i < sizeof(args) / sizeof(args[0]); ++i) args[i] = &atom;

      check_true(schema_cmeta_generic_identity(&identity, &cmeta_tuple_generic_desc,
                                               args, 16u));
      check_true(cmeta_type_identity_valid(&identity));
      check_equal(identity.arity, 16u);
      original = identity;
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_tuple_generic_desc,
                                                args, 17u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
    }

    it("rejects invalid constructors arguments and arity without publishing output") {
      static const cmeta_type_identity atom =
          CMETA_TYPE_ID_ATOM_INIT("schema.Value");
      const cmeta_type_identity *one_arg[] = {&atom};
      cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("sentinel.identity");
      cmeta_type_identity original = identity;
      cmeta_generic_desc invalid_constructor = cmeta_option_generic_desc;
      const cmeta_type_identity invalid_atom = CMETA_TYPE_ID_ATOM_INIT("");
      const cmeta_type_identity *null_arg[] = {NULL};
      const cmeta_type_identity *invalid_arg[] = {&invalid_atom};
      const cmeta_type_identity *two_args[] = {&atom, &atom};
      invalid_constructor.stable_id = "";

      check_false(schema_cmeta_generic_identity(&identity, NULL, one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &invalid_constructor,
                                               one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_option_generic_desc,
                                               null_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_option_generic_desc,
                                               invalid_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_option_generic_desc,
                                               NULL, 0u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_option_generic_desc,
                                               two_args, 2u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);

      check_false(schema_cmeta_generic_identity(&identity, &cmeta_result_generic_desc,
                                               one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_pair_generic_desc,
                                               one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_tuple_generic_desc,
                                               one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, &cmeta_option_generic_desc,
                                               NULL, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(NULL, &cmeta_option_generic_desc,
                                               one_arg, 1u));
    }
  }
}
