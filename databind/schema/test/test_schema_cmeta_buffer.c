#include "tinytest.h"
#include "schema_cmeta.h"
#include "../src/schema_cmeta_buffer.h"

#include <cmeta_cmeta_data.h>

#include <stddef.h>
#include <string.h>

#define COUNT_OF(items_) (sizeof(items_) / sizeof((items_)[0]))

static const cmeta_data_buffer_shape owned_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_shape borrowed_shape = {CMETA_DATA_BUFFER_BORROWED};

suite("schema_cmeta_buffer") {
  describe("explicit storage descriptor lowering") {
    it("rejects invalid or unsupported mappings without publishing a descriptor") {
      const cmeta_data_desc sentinel = {
        .struct_size = sizeof(cmeta_data_desc),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "sentinel", .display_name = "sentinel",
        .kind = CMETA_DATA_BOOL, .storage_type = &cmeta_type_bool
      };
      cmeta_data_desc out = sentinel;
      unsigned char original[sizeof(out)];
      cmeta_data_buffer_ops incomplete = cmeta_tstr_cmeta_buffer_ops;
      cmeta_data_buffer_ops custom = cmeta_tstr_cmeta_buffer_ops;
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
        {NULL, "buffer", CMETA_DATA_STRING, &cmeta_tstr_cmeta_type,
         &owned_shape, &cmeta_tstr_cmeta_buffer_ops},
        {"id", "", CMETA_DATA_BYTES, &cmeta_tstr_cmeta_type,
         &owned_shape, &cmeta_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_SINT, &cmeta_tstr_cmeta_type,
         &owned_shape, &cmeta_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_STRING, &cmeta_type_int,
         &owned_shape, &cmeta_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_BYTES, &cmeta_tstr_cmeta_type,
         &borrowed_shape, &cmeta_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_STRING, &cmeta_tstr_cmeta_type,
         NULL, &cmeta_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_BYTES, &cmeta_tstr_cmeta_type,
         &owned_shape, NULL},
        {"id", "buffer", CMETA_DATA_BYTES, &cmeta_tstr_cmeta_type,
         &owned_shape, &incomplete},
        {"id", "buffer", CMETA_DATA_BYTES, &cmeta_tstr_cmeta_type,
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
          &cmeta_tstr_cmeta_type, &owned_shape, &cmeta_tstr_cmeta_buffer_ops));
    }
  }
}

#undef COUNT_OF
