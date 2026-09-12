#include "tinytest.h"
#include "schema_cmeta.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* RED contract for structural descriptor lowering. */
extern int schema_cmeta_struct_data(cmeta_data_desc *out_data,
                                    cmeta_data_struct_shape *out_shape,
                                    const char *stable_id,
                                    const char *display_name,
                                    const cmeta_type_desc *storage_type,
                                    const cmeta_struct_desc *layout,
                                    const cmeta_data_field_desc *fields,
                                    size_t field_count);
extern int schema_cmeta_enum_data(cmeta_data_desc *out_data,
                                  cmeta_data_enum_shape *out_shape,
                                  const char *stable_id,
                                  const char *display_name,
                                  const cmeta_type_desc *storage_type,
                                  const cmeta_enum_desc *meta);

/* RED contract for canonical finite-generic semantic lowering. */
extern int schema_cmeta_generic_identity(cmeta_type_identity *out_identity,
                                         const char *semantic,
                                         const cmeta_type_identity *const *args,
                                         size_t arity);

static void check_fixed_width_descriptor(const char *name,
                                         const char *stable_id,
                                         cmeta_data_kind kind,
                                         uint8_t bits) {
  const cmeta_data_desc *data = schema_cmeta_builtin_data(name);

  check_true(data != NULL);
  if (data == NULL) return;

  check_equal(data->struct_size, sizeof(cmeta_data_desc));
  check_equal(data->abi_version, CMETA_DATA_DESC_ABI_VERSION);
  check_true(data->stable_id != NULL);
  if (data->stable_id != NULL) check_true(strcmp(data->stable_id, stable_id) == 0);
  check_equal(data->kind, kind);
  check_true(data->storage_type != NULL);
  check_true(data->shape != NULL);
  if (data->shape != NULL)
    check_equal(((const cmeta_data_integer_shape *)data->shape)->bits, bits);
}

suite("schema_cmeta") {
  describe("canonical builtin scalar lowering") {
    it("maps signed integer aliases to exact-width Core descriptor semantics") {
      check_fixed_width_descriptor("int8", "salts.int8.data", CMETA_DATA_SINT, 8u);
      check_fixed_width_descriptor("i8", "salts.int8.data", CMETA_DATA_SINT, 8u);
      check_fixed_width_descriptor("int16", "salts.int16.data", CMETA_DATA_SINT, 16u);
      check_fixed_width_descriptor("i16", "salts.int16.data", CMETA_DATA_SINT, 16u);
      check_fixed_width_descriptor("int32", "salts.int32.data", CMETA_DATA_SINT, 32u);
      check_fixed_width_descriptor("i32", "salts.int32.data", CMETA_DATA_SINT, 32u);
      check_fixed_width_descriptor("int64", "salts.int64.data", CMETA_DATA_SINT, 64u);
      check_fixed_width_descriptor("i64", "salts.int64.data", CMETA_DATA_SINT, 64u);
    }

    it("maps unsigned integer aliases to exact-width Core descriptor semantics") {
      check_fixed_width_descriptor("uint8", "salts.uint8.data", CMETA_DATA_UINT, 8u);
      check_fixed_width_descriptor("u8", "salts.uint8.data", CMETA_DATA_UINT, 8u);
      check_fixed_width_descriptor("byte", "salts.uint8.data", CMETA_DATA_UINT, 8u);
      check_fixed_width_descriptor("uint16", "salts.uint16.data", CMETA_DATA_UINT, 16u);
      check_fixed_width_descriptor("u16", "salts.uint16.data", CMETA_DATA_UINT, 16u);
      check_fixed_width_descriptor("uint32", "salts.uint32.data", CMETA_DATA_UINT, 32u);
      check_fixed_width_descriptor("u32", "salts.uint32.data", CMETA_DATA_UINT, 32u);
      check_fixed_width_descriptor("uint64", "salts.uint64.data", CMETA_DATA_UINT, 64u);
      check_fixed_width_descriptor("u64", "salts.uint64.data", CMETA_DATA_UINT, 64u);
    }

    it("maps uuid to the process-wide canonical Core descriptor") {
      check_true(schema_cmeta_builtin_data("uuid") == &salts_uuid_cmeta_data);
      check_true(salts_uuid_cmeta_data_valid(schema_cmeta_builtin_data("uuid")));
    }

    it("rejects unsupported or invalid scalar names explicitly") {
      check_null(schema_cmeta_builtin_data("varint"));
      check_null(schema_cmeta_builtin_data("not-a-type"));
      check_null(schema_cmeta_builtin_data(NULL));
    }
  }

  describe("schema semantic lowering") {
    it("maps every supported scalar family to a CMeta semantic kind") {
      struct KindCase { const char *name; cmeta_data_kind kind; };
      static const struct KindCase cases[] = {
        {"bool", CMETA_DATA_BOOL}, {"i8", CMETA_DATA_SINT},
        {"int64", CMETA_DATA_SINT}, {"u8", CMETA_DATA_UINT},
        {"uint64", CMETA_DATA_UINT}, {"float", CMETA_DATA_FLOAT},
        {"double", CMETA_DATA_FLOAT}, {"string", CMETA_DATA_STRING},
        {"bytes", CMETA_DATA_BYTES}, {"uuid", CMETA_DATA_CUSTOM},
        {"datetime", CMETA_DATA_CUSTOM}, {"date", CMETA_DATA_CUSTOM},
        {"time", CMETA_DATA_CUSTOM}, {"duration", CMETA_DATA_CUSTOM},
        {"decimal", CMETA_DATA_CUSTOM}, {"bigint", CMETA_DATA_CUSTOM},
        {"money", CMETA_DATA_CUSTOM}
      };
      size_t i;
      for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cmeta_data_kind kind = CMETA_DATA_BOOL;
        check_true(schema_cmeta_data_kind(cases[i].name, &kind));
        check_equal(kind, cases[i].kind);
      }
    }

    it("maps structural and collection schema semantics without choosing CSTL storage") {
      struct KindCase { const char *name; cmeta_data_kind kind; };
      static const struct KindCase cases[] = {
        {"message", CMETA_DATA_STRUCT}, {"composite", CMETA_DATA_STRUCT},
        {"group", CMETA_DATA_STRUCT}, {"enum", CMETA_DATA_ENUM},
        {"flags", CMETA_DATA_ENUM}, {"union", CMETA_DATA_VARIANT},
        {"list", CMETA_DATA_SEQUENCE}, {"set", CMETA_DATA_SET},
        {"map", CMETA_DATA_MAP}
      };
      size_t i;
      for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cmeta_data_kind kind = CMETA_DATA_BOOL;
        check_true(schema_cmeta_data_kind(cases[i].name, &kind));
        check_equal(kind, cases[i].kind);
      }
    }

    it("fails unsupported semantics deterministically without changing output") {
      cmeta_data_kind kind = CMETA_DATA_MAP;
      check_false(schema_cmeta_data_kind("result", &kind));
      check_equal(kind, CMETA_DATA_MAP);
      check_false(schema_cmeta_data_kind("not-a-type", &kind));
      check_equal(kind, CMETA_DATA_MAP);
      check_false(schema_cmeta_data_kind(NULL, &kind));
      check_equal(kind, CMETA_DATA_MAP);
      check_false(schema_cmeta_data_kind("bool", NULL));
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
    it("lowers option pair and tuple through CMeta semantic applications") {
      static const cmeta_type_identity atom_a =
          CMETA_TYPE_ID_ATOM_INIT("schema.A");
      static const cmeta_type_identity atom_b =
          CMETA_TYPE_ID_ATOM_INIT("schema.B");
      const cmeta_type_identity *option_args[] = {&atom_a};
      const cmeta_type_identity *pair_args[] = {&atom_a, &atom_b};
      const cmeta_type_identity *tuple_args[] = {&atom_a, &atom_b, &atom_a};
      cmeta_type_identity option_identity = {0};
      cmeta_type_identity pair_identity = {0};
      cmeta_type_identity tuple_identity = {0};

      check_true(schema_cmeta_generic_identity(&option_identity, "optional",
                                               option_args, 1u));
      check_true(cmeta_type_identity_valid(&option_identity));
      check_true(strcmp(option_identity.constructor->stable_id, "cmeta.Option") == 0);
      check_equal(option_identity.arity, 1u);
      check_true(cmeta_type_identity_equal(option_identity.args[0], &atom_a));

      check_true(schema_cmeta_generic_identity(&pair_identity, "pair",
                                               pair_args, 2u));
      check_true(cmeta_type_identity_valid(&pair_identity));
      check_true(strcmp(pair_identity.constructor->stable_id, "cmeta.Pair") == 0);
      check_equal(pair_identity.arity, 2u);

      check_true(schema_cmeta_generic_identity(&tuple_identity, "tuple",
                                               tuple_args, 3u));
      check_true(cmeta_type_identity_valid(&tuple_identity));
      check_true(strcmp(tuple_identity.constructor->stable_id, "cmeta.Tuple") == 0);
      check_equal(tuple_identity.arity, 3u);
    }

    it("rejects unsupported generic semantics and invalid arity without publishing output") {
      static const cmeta_type_identity atom =
          CMETA_TYPE_ID_ATOM_INIT("schema.Value");
      const cmeta_type_identity *one_arg[] = {&atom};
      cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("sentinel.identity");
      cmeta_type_identity original = identity;

      check_false(schema_cmeta_generic_identity(&identity, "result", one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, "pair", one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, "tuple", one_arg, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(&identity, "optional", NULL, 1u));
      check_true(memcmp(&identity, &original, sizeof(identity)) == 0);
      check_false(schema_cmeta_generic_identity(NULL, "optional", one_arg, 1u));
    }
  }
}
