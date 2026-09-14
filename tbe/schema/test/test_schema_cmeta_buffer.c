#include "tinytest.h"
#include "schema_cmeta.h"
#include "../src/schema_cmeta_buffer.h"

#include <salts_cmeta_data.h>

#include <stddef.h>
#include <string.h>

#define COUNT_OF(items_) (sizeof(items_) / sizeof((items_)[0]))

static const cmeta_data_buffer_shape owned_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_shape borrowed_shape = {CMETA_DATA_BUFFER_BORROWED};

typedef struct BufferCase {
  const char *stable_id;
  cmeta_data_kind kind;
} BufferCase;

static const BufferCase buffer_cases[] = {
  {"test.schema.string", CMETA_DATA_STRING},
  {"test.schema.bytes", CMETA_DATA_BYTES}
};

static void build_buffer(cmeta_data_desc *data, const BufferCase *item,
                         const cmeta_data_buffer_shape *shape,
                         const cmeta_data_buffer_ops *ops) {
  check_true(schema_cmeta_buffer_data(data, item->stable_id, "buffer",
                                      item->kind, ops->storage_type, shape, ops));
}

suite("schema_cmeta_buffer") {
  describe("explicit storage descriptor lowering") {
    it("keeps string and bytes semantics separate from the storage choice") {
      size_t i;
      for (i = 0u; i < COUNT_OF(buffer_cases); ++i) {
        cmeta_data_desc owned = {0};
        cmeta_data_desc borrowed = {0};
        build_buffer(&owned, &buffer_cases[i], &owned_shape,
                     &salts_tstr_cmeta_buffer_ops);
        build_buffer(&borrowed, &buffer_cases[i], &borrowed_shape,
                     &salts_vstr_cmeta_buffer_ops);
        check_true(cmeta_data_desc_valid(&owned));
        check_true(cmeta_data_desc_valid(&borrowed));
        check_equal(owned.kind, buffer_cases[i].kind);
        check_equal(borrowed.kind, buffer_cases[i].kind);
        check_true(owned.storage_type == &salts_tstr_cmeta_type);
        check_true(borrowed.storage_type == &salts_vstr_cmeta_type);
        check_true(owned.shape == &owned_shape);
        check_true(borrowed.shape == &borrowed_shape);
        check_true(cmeta_data_buffer_ops_of(&owned) == &salts_tstr_cmeta_buffer_ops);
        check_true(cmeta_data_buffer_ops_of(&borrowed) == &salts_vstr_cmeta_buffer_ops);
      }
      check_null(schema_cmeta_builtin_data("string"));
      check_null(schema_cmeta_builtin_data("bytes"));
    }

    it("accepts storage metadata copies by CMeta semantic identity") {
      cmeta_type_identity identity = salts_tstr_cmeta_identity;
      cmeta_type_desc storage = salts_tstr_cmeta_type;
      cmeta_data_buffer_ops ops = salts_tstr_cmeta_buffer_ops;
      cmeta_data_desc data = {0};
      const unsigned char input[] = {'a', 0, 'b'};
      tstr out = NULL;
      storage.identity = &identity;
      ops.storage_type = &storage;

      check_true(schema_cmeta_buffer_data(&data, "test.schema.copy", "copy",
          CMETA_DATA_BYTES, &salts_tstr_cmeta_type, &owned_shape, &ops));
      check_true(cmeta_type_identity_equal(data.storage_type->identity, &identity));
      check_equal(cmeta_data_buffer_assign(&data, &out, input, sizeof(input),
                                           sizeof(input)), CMETA_OK);
      check_equal(tstr_len(out), sizeof(input));
      check_equal(memcmp(out, input, sizeof(input)), 0);
      check_equal(cmeta_data_buffer_restore_zero(&data, &out), CMETA_OK);
      check_null(out);
    }

    it("rejects invalid or unsupported mappings without publishing a descriptor") {
      const cmeta_data_desc sentinel = {
        .struct_size = sizeof(cmeta_data_desc),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "sentinel", .display_name = "sentinel",
        .kind = CMETA_DATA_BOOL, .storage_type = &cmeta_type_bool
      };
      cmeta_data_desc out = sentinel;
      unsigned char original[sizeof(out)];
      cmeta_data_buffer_ops incomplete = salts_tstr_cmeta_buffer_ops;
      cmeta_data_buffer_ops custom = salts_tstr_cmeta_buffer_ops;
      const cmeta_data_buffer_shape custom_shape = {CMETA_DATA_BUFFER_CUSTOM};
      struct InvalidCase {
        const char *id;
        const char *name;
        cmeta_data_kind kind;
        const cmeta_type_desc *storage;
        const cmeta_data_buffer_shape *shape;
        const cmeta_data_buffer_ops *ops;
      };
      const struct InvalidCase cases[] = {
        {NULL, "buffer", CMETA_DATA_STRING, &salts_tstr_cmeta_type,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_SINT, &salts_tstr_cmeta_type,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_STRING, &cmeta_type_int,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &borrowed_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_STRING, &salts_tstr_cmeta_type,
         NULL, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &owned_shape, NULL},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &owned_shape, &incomplete},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &custom_shape, &custom}
      };
      size_t i;
      memcpy(original, &out, sizeof(out));
      incomplete.assign = NULL;
      custom.ownership = CMETA_DATA_BUFFER_CUSTOM;
      for (i = 0u; i < COUNT_OF(cases); ++i) {
        check_false(schema_cmeta_buffer_data(&out, cases[i].id, cases[i].name,
            cases[i].kind, cases[i].storage, cases[i].shape, cases[i].ops));
        check_true(memcmp(&out, original, sizeof(out)) == 0);
      }
      check_false(schema_cmeta_buffer_data(NULL, "id", "buffer", CMETA_DATA_BYTES,
          &salts_tstr_cmeta_type, &owned_shape, &salts_tstr_cmeta_buffer_ops));
    }
  }
}

#undef COUNT_OF
